/**
 * mds — A micro-display server
 * Copyright © 2014  Mattias Andrée (maandree@member.fsf.org)
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "mds-server.h"
#include "config.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <limits.h>
#include <unistd.h>
#include <pwd.h>
#include <errno.h>
#include <pthread.h>
#include <sys/socket.h>



/**
 * Number of elements in `argv`
 */
static int argc;

/**
 * Command line arguments
 */
static char** argv;


/**
 * The program run state, 1 when running,
 * 0 when shutting down
 */
static volatile int running = 1;

/**
 * The number of running slaves
 */
static int running_slaves = 0;

/**
 * Mutex for slave counter
 */
static pthread_mutex_t slave_mutex;

/**
 * Condition for slave counter
 */
static pthread_cond_t slave_cond;



/* TODO make the server update without all slaves dying on SIGUSR1 */


/**
 * Entry point of the server
 * 
 * @param   argc_  Number of elements in `argv_`
 * @param   argv_  Command line arguments
 * @return         Non-zero on error
 */
int main(int argc_, char** argv_)
{
  int is_respawn = -1;
  int socket_fd = -1;
  int unparsed_args_ptr = 1;
  char* unparsed_args[ARGC_LIMIT + LIBEXEC_ARGC_EXTRA_LIMIT + 1];
  int i;
  pid_t pid;
  pthread_t _slave_thread;
  
  
  argc = argc_;
  argv = argv_;
  
  
  /* Drop privileges like it's hot. */
  if ((geteuid() == getuid() ? 0 : seteuid(getuid())) ||
      (getegid() == getgid() ? 0 : setegid(getgid())))
    {
      perror(*argv);
      return 1;
    }
  
  
  /* Sanity check the number of command line arguments. */
  if (argc > ARGC_LIMIT + LIBEXEC_ARGC_EXTRA_LIMIT)
    {
      fprintf(stderr,
	      "%s: that number of arguments is ridiculous, I will not allow it.\n",
	      *argv);
      return 1;
    }
  
  
  /* Parse command line arguments. */
  for (i = 1; i < argc; i++)
    {
      char* arg = argv[i];
      if (!strcmp(arg, "--initial-spawn")) /* Initial spawn? */
	if (is_respawn == 1)
	  {
	    fprintf(stderr,
		    "%s: conflicting arguments %s and %s cannot be combined.\n",
		    *argv, "--initial-spawn", "--respawn");
	    return 1;
	  }
	else
	  is_respawn = 0;
      else if (!strcmp(arg, "--respawn")) /* Respawning after crash? */
	if (is_respawn == 0)
	  {
	    fprintf(stderr,
		    "%s: conflicting arguments %s and %s cannot be combined.\n",
		    *argv, "--initial-spawn", "--respawn");
	    return 1;
	  }
	else
	  is_respawn = 1;
      else if (strstr(arg, "--socket-fd=") == arg) /* Socket file descriptor. */
	{
	  long int r;
	  char* endptr;
	  if (socket_fd != -1)
	    {
	      fprintf(stderr, "%s: duplicate declaration of %s.\n", *argv, "--socket-fd");
	      return -1;
	    }
	  arg += strlen("--socket-fd=");
	  r = strtol(arg, &endptr, 10);
	  if ((*argv == '\0') || isspace(*argv) ||
	      (endptr - arg != (ssize_t)strlen(arg))
	      || (r < 0) || (r > INT_MAX))
	    {
	      fprintf(stderr, "%s: invalid value for %s: %s.\n", *argv, "--socket-fd", arg);
	      return 1;
	    }
	  socket_fd = (int)r;
	}
      else
	/* Not recognised, it is probably for another server. */
	unparsed_args[unparsed_args_ptr++] = arg;
    }
  unparsed_args[unparsed_args_ptr] = NULL;
  
  
  /* Check that manditory arguments have been specified. */
  if (is_respawn < 0)
    {
      fprintf(stderr,
	      "%s: missing state argument, require either %s or %s.\n",
	      *argv, "--initial-spawn", "--respawn");
      return 1;
    }
  if (socket_fd < 0)
    {
      fprintf(stderr, "%s: missing socket file descriptor argument.\n", *argv);
      return 1;
    }
  
  
  if (is_respawn == 0)
    {
      /* Run mdsinitrc. */
      pid = fork();
      if (pid == (pid_t)-1)
	{
	  perror(*argv);
	  return 1;
	}
      if (pid == 0) /* Child process exec:s, the parent continues without waiting for it. */
	{
	  run_initrc(unparsed_args);
	  return 1;
	}
    }
  
  
  /* Create mutex and condition for slave counter. */
  pthread_mutex_init(&slave_mutex, NULL);
  pthread_cond_init(&slave_cond, NULL);
  
  
  /* Accepting incoming connections. */
  while (running)
    {
      /* Accept connection. */
      int client_fd = accept(socket_fd, NULL, NULL);
      
      /* Handle errors and shutdown. */
      if (client_fd == -1)
	{
	  switch (errno)
	    {
	    case EINTR:
	      /* Interrupted. */
	      break;
	      
	    case ECONNABORTED:
	    case EINVAL:
	      /* Closing. */
	      running = 0;
	      break;
	      
	    default:
	      /* Error. */
	      perror(*argv);
	      break;
	    }
	  continue;
	}
      
      /* Increase number of running slaves. */
      pthread_mutex_lock(&slave_mutex);
      running_slaves++;
      pthread_mutex_unlock(&slave_mutex);
      
      /* Start slave thread. */
      errno = pthread_create(&_slave_thread, NULL, slave_loop, (void*)(intptr_t)client_fd);
      if (errno)
	{
	  perror(*argv);
	  pthread_mutex_lock(&slave_mutex);
	  running_slaves--;
	  pthread_mutex_unlock(&slave_mutex);
	}
    }
  
  
  /* Wait for all slaves to close. */
  pthread_mutex_lock(&slave_mutex);
  while (running_slaves > 0)
    pthread_cond_wait(&slave_cond, &slave_mutex);
  pthread_mutex_unlock(&slave_mutex);
  
  return 0;
}


/**
 * Master function for slave threads
 * 
 * @param   data  Input data
 * @return        Outout data
 */
void* slave_loop(void* data)
{
  int socket_fd = (int)(intptr_t)data;
  
  /* TODO */
  
  /* Close socket. */
  close(socket_fd);
  
  /* Decrease the slave count. */
  pthread_mutex_lock(&slave_mutex);
  running_slaves--;
  pthread_cond_signal(&slave_cond);
  pthread_mutex_unlock(&slave_mutex);
  
  return NULL;
}


/**
 * Read an environment variable, but handle it as undefined if empty
 * 
 * @param   var  The environment variable's name
 * @return       The environment variable's value, `NULL` if empty or not defined
 */
char* getenv_nonempty(const char* var)
{
  char* rc = getenv(var);
  if ((rc == NULL) || (*rc == '\0'))
    return NULL;
  return rc;
}


/**
 * Exec into the mdsinitrc script
 * 
 * @param  args  The arguments to the child process
 */
void run_initrc(char** args)
{
  char pathname[PATH_MAX];
  struct passwd* pwd;
  char* env;
  char* home;
  args[0] = pathname;
  
  
  /* Test $XDG_CONFIG_HOME. */
  if ((env = getenv_nonempty("XDG_CONFIG_HOME")) != NULL)
    {
      snprintf(pathname, sizeof(pathname) / sizeof(char), "%s/.%s", env, INITRC_FILE);
      execv(args[0], args);
    }
  
  /* Test $HOME. */
  if ((env = getenv_nonempty("HOME")) != NULL)
    {
      snprintf(pathname, sizeof(pathname) / sizeof(char), "%s/.config/%s", env, INITRC_FILE);
      execv(args[0], args);
      
      snprintf(pathname, sizeof(pathname) / sizeof(char), "%s/.%s", env, INITRC_FILE);
      execv(args[0], args);
    }
  
  /* Test ~. */
  pwd = getpwuid(getuid()); /* Ignore error. */
  if (pwd != NULL)
    {
      home = pwd->pw_dir;
      if ((home != NULL) && (*home != '\0'))
	{
	  snprintf(pathname, sizeof(pathname) / sizeof(char), "%s/.config/%s", home, INITRC_FILE);
	  execv(args[0], args);
	  
	  snprintf(pathname, sizeof(pathname) / sizeof(char), "%s/.%s", home, INITRC_FILE);
	  execv(args[0], args);
	}
    }
  
  /* Test $XDG_CONFIG_DIRS. */
  if ((env = getenv_nonempty("XDG_CONFIG_DIRS")) != NULL)
    {
      char* begin = env;
      char* end;
      int len;
      for (;;)
	{
	  end = strchrnul(begin, ':');
	  len = (int)(end - begin);
	  if (len > 0)
	    {
	      snprintf(pathname, sizeof(pathname) / sizeof(char), "%.*s/%s", len, begin, INITRC_FILE);
	      execv(args[0], args);
	    }
	  if (*end == '\0')
	    break;
	  begin = end + 1;
	}
    }
  
  /* Test /etc. */
  snprintf(pathname, sizeof(pathname) / sizeof(char), "%s/%s", SYSCONFDIR, INITRC_FILE);
  execv(args[0], args);
  
  
  /* Everything failed. */
  fprintf(stderr, "%s: unable to run %s file, you might as well kill me.\n", *argv, INITRC_FILE);
}
