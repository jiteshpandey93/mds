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
#include "mds-vt.h"

#include <libmdsserver/config.h>
#include <libmdsserver/macros.h>
#include <libmdsserver/util.h>
#include <libmdsserver/mds-message.h>

#include <errno.h>
#include <inttypes.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <linux/kd.h>
#include <limits.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#define reconnect_fd_to_display(fd) -1
#define reconnect_to_display() reconnect_fd_to_display(&socket_fd)



#define MDS_VT_VARS_VERSION  0



/**
 * This variable should declared by the actual server implementation.
 * It must be configured before `main` is invoked.
 * 
 * This tells the server-base how to behave
 */
server_characteristics_t server_characteristics =
  {
    .require_privileges = 1,
    /* Required for acquiring a TTY and requesting a VT switch. */
    
    .require_display = 1,
    .require_respawn_info = 1,
    .sanity_check_argc = 1,
    .fork_for_safety = 0,
    .danger_is_deadly = 0
  };



/**
 * Value of the ‘Message ID’ header for the next message
 */
static uint32_t message_id = 1;

/**
 * Buffer for received messages
 */
static mds_message_t received;

/**
 * Whether the server is connected to the display
 */
static int connected = 1;

/**
 * The index of the VT used for the display
 */
static int display_vt;

/**
 * The file descriptor the display's TTY is opened on
 */
static int display_tty_fd = -1;

/**
 * Whether the display's TTY is in the foreground
 */
static int vt_is_active = 1;

/**
 * The stat for the TTY of the display's VT before we toke it
 */
static struct stat old_vt_stat;

/**
 * -1 if switching to our VT, 1 if switching to another VT, 0 otherwise
 */
static volatile sig_atomic_t switching_vt = 0;

/**
 * The pathname for the file containing VT information
 */
static char vtfile_path[PATH_MAX];

/**
 * The file descriptor for the secondary connection to the display
 */
static int secondary_socket_fd;

/**
 * Whether the secondary thread has been started
 */
static int secondary_thread_started = 0;

/**
 * The secondary thread
 */
static pthread_t secondary_thread;

/**
 * Whether the secondary thread failed
 */
static volatile sig_atomic_t secondary_thread_failed = 0;



/**
 * This function will be invoked before `initialise_server` (if not re-exec:ing)
 * or before `unmarshal_server` (if re-exec:ing)
 * 
 * @return  Non-zero on error
 */
int __attribute__((const)) preinitialise_server(void)
{
  return 0;
}


/**
 * Write persistent data about the VT and TTY to a file
 * 
 * @return  Zero on success, -1 on error
 */
static int write_vt_file(void)
{
  char buf[(sizeof(int) + sizeof(struct stat)) / sizeof(char)];
  int fd, r, old_errno;
  int* intbuf = (int*)buf;
  
  *intbuf = display_vt;
  *(struct stat*)(buf + sizeof(int) / sizeof(char)) = old_vt_stat;
  
  fd = open(vtfile_path, O_WRONLY | O_CREAT);
  if (fd < 0)
    return -1;
  
  r = full_write(fd, buf, sizeof(buf));
  old_errno = errno;
  close(fd);
  errno = old_errno;
  return r;
}


/**
 * Read persistent data about the VT and TTY from a file
 * 
 * @return  Zero on success, -1 on error
 */
static int read_vt_file(void)
{
  char* buf;
  size_t len;
  int fd;
  
  fd = open(vtfile_path, O_RDONLY);
  if (fd < 0)
    return -1;
  
  buf = full_read(fd, &len);
  if (buf == NULL)
    return -1;
  
  if (len != sizeof(int) + sizeof(struct stat))
    {
      eprint("VT file is of wrong size.");
      errno = 0;
      return -1;
    }
  
  display_vt = *(int*)buf;
  old_vt_stat = *(struct stat*)(buf + sizeof(int) / sizeof(char));
  return 0;
}


/**
 * This function should initialise the server,
 * and it not invoked after a re-exec.
 * 
 * @return  Non-zero on error
 */
int initialise_server(void)
{
  struct vt_mode mode;
  char* display_env;
  int primary_socket_fd;
  const char* const message =
    "Command: intercept\n"
    "Message ID: 0\n"
    "Length: 38\n"
    "\n"
    "Command: get-vt\n"
    "Command: configure-vt\n";
  const char* const secondary_message =
    "Command: intercept\n"
    "Message ID: 0\n"
    "Priority: -4611686018427387904\n" /* −2⁶² */
    "Length: 22\n"
    "\n"
    "Command: switching-vt\n";
  
  primary_socket_fd = socket_fd;
  if (connect_to_display())
    return 1;
  secondary_socket_fd = socket_fd;
  socket_fd = primary_socket_fd;
  
  display_env = getenv("MDS_DISPLAY");
  display_env = display_env ? strchr(display_env, ':') : NULL;
  if ((display_env == NULL) || (strlen(display_env) < 2))
    goto no_display;
  
  memset(vtfile_path, 0, sizeof(vtfile_path));
  xsnprintf(vtfile_path, "%s/%s.vt", MDS_RUNTIME_ROOT_DIRECTORY, display_env + 1);
  
  if (is_respawn == 0)
    {
      display_vt = vt_get_next_available();
      if (display_vt == 0)
	{
	  eprint("out of available virtual terminals, I am stymied.");
	  goto fail;
	}
      else if (display_vt < 0)
	goto pfail;
      display_tty_fd = vt_open(display_vt, &old_vt_stat);
      fail_if (write_vt_file() < 0);
      fail_if (vt_set_active(display_vt) < 0);
    }
  else
    {
      fail_if (read_vt_file() < 0);
      vt_is_active = (display_vt == vt_get_active());
      fail_if (vt_is_active < 0);
    }
  
  if (full_send(secondary_socket_fd, secondary_message, strlen(secondary_message)))
    return 1;
  if (full_send(socket_fd, message, strlen(message)))
    return 1;
  fail_if (server_initialised() < 0);
  fail_if (mds_message_initialise(&received));
  
  fail_if (xsigaction(SIGRTMIN + 1, received_switch_vt) < 0);
  fail_if (xsigaction(SIGRTMIN + 2, received_switch_vt) < 0);
  vt_construct_mode(1, SIGRTMIN + 1, SIGRTMIN + 2, &mode);
  fail_if (vt_get_set_mode(display_tty_fd, 1, &mode) < 0);
  if (vt_set_exclusive(display_tty_fd, 1) < 0)
    xperror(*argv);
  
  return 0;
 no_display:
  eprint("no display has been set, how did this happen.");
  return 1;
 pfail:
  xperror(*argv);
 fail:
  unlink(vtfile_path);
  if (display_tty_fd >= 0)
    vt_close(display_tty_fd, &old_vt_stat);
  mds_message_destroy(&received);
  return 1;
}


/**
 * This function will be invoked after `initialise_server` (if not re-exec:ing)
 * or after `unmarshal_server` (if re-exec:ing)
 * 
 * @return  Non-zero on error
 */
int postinitialise_server(void)
{
  if (connected)
    return 0;
  
  if (reconnect_to_display())
    {
      mds_message_destroy(&received);
      return 1;
    }
  connected = 1;
  
  if ((errno = pthread_create(&secondary_thread, NULL, secondary_loop, NULL)))
    return 1;
  
  return 0;
}


/**
 * Calculate the number of bytes that will be stored by `marshal_server`
 * 
 * On failure the program should `abort()` or exit by other means.
 * However it should not be possible for this function to fail.
 * 
 * @return  The number of bytes that will be stored by `marshal_server`
 */
size_t marshal_server_size(void)
{
  size_t rc = 6 * sizeof(int) + sizeof(uint32_t);
  rc += sizeof(struct stat);
  rc += PATH_MAX * sizeof(char);
  rc += mds_message_marshal_size(&received);
  return rc;
}


/**
 * Marshal server implementation specific data into a buffer
 * 
 * @param   state_buf  The buffer for the marshalled data
 * @return             Non-zero on error
 */
int marshal_server(char* state_buf)
{
  buf_set_next(state_buf, int, MDS_VT_VARS_VERSION);
  buf_set_next(state_buf, int, connected);
  buf_set_next(state_buf, uint32_t, message_id);
  buf_set_next(state_buf, int, display_vt);
  buf_set_next(state_buf, int, display_tty_fd);
  buf_set_next(state_buf, int, vt_is_active);
  buf_set_next(state_buf, struct stat, old_vt_stat);
  buf_set_next(state_buf, int, secondary_socket_fd);
  memcpy(state_buf, vtfile_path, PATH_MAX * sizeof(char));
  state_buf += PATH_MAX;
  mds_message_marshal(&received, state_buf);
  
  mds_message_destroy(&received);
  return 0;
}


/**
 * Unmarshal server implementation specific data and update the servers state accordingly
 * 
 * On critical failure the program should `abort()` or exit by other means.
 * That is, do not let `reexec_failure_recover` run successfully, if it unrecoverable
 * error has occurred or one severe enough that it is better to simply respawn.
 * 
 * @param   state_buf  The marshalled data that as not been read already
 * @return             Non-zero on error
 */
int unmarshal_server(char* state_buf)
{
  int r;
  /* buf_get_next(state_buf, int, MDS_VT_VARS_VERSION); */
  buf_next(state_buf, int, 1);
  buf_get_next(state_buf, int, connected);
  buf_get_next(state_buf, uint32_t, message_id);
  buf_get_next(state_buf, int, display_vt);
  buf_get_next(state_buf, int, display_tty_fd);
  buf_get_next(state_buf, int, vt_is_active);
  buf_get_next(state_buf, struct stat, old_vt_stat);
  buf_set_next(state_buf, int, secondary_socket_fd);
  memcpy(vtfile_path, state_buf, PATH_MAX * sizeof(char));
  state_buf += PATH_MAX;
  r = mds_message_unmarshal(&received, state_buf);
  if (r)
    {
      xperror(*argv);
      mds_message_destroy(&received);
    }
  return r;
}


/**
 * Attempt to recover from a re-exec failure that has been
 * detected after the server successfully updated it execution image
 * 
 * @return  Non-zero on error
 */
int __attribute__((const)) reexec_failure_recover(void)
{
  return -1;
}


/**
 * Perform the server's mission
 * 
 * @return  Non-zero on error
 */
int master_loop(void)
{
  int rc = 1, r;
  
  while (!reexecing && !terminating)
    {
      if (switching_vt)
	{
	  int leaving = switching_vt == 1;
	  switching_vt = 0;
	  r = switch_vt(leaving);
	}
      else
	if (r = mds_message_read(&received, socket_fd), r == 0)
	  r = handle_message();
      if (r == 0)
	continue;
      
      if (r == -2)
	{
	  eprint("corrupt message received, aborting.");
	  goto fail;
	}
      else if (errno == EINTR)
	continue;
      else if (errno != ECONNRESET)
	goto pfail;
      
      eprint("lost primary connection to server.");
      mds_message_destroy(&received);
      mds_message_initialise(&received);
      connected = 0;
      if (reconnect_to_display())
	goto fail;
      connected = 1;
    }
  
  rc = 0;
  if (vt_set_exclusive(display_tty_fd, 0) < 0)  xperror(*argv);
  if (vt_set_graphical(display_tty_fd, 0) < 0)  xperror(*argv);
  if (unlink(vtfile_path) < 0)
    xperror(*argv);
  vt_close(display_tty_fd, &old_vt_stat);
  goto fail;
 pfail:
  xperror(*argv);
 fail:
  rc |= secondary_thread_failed;
  if (rc || !reexecing)
    mds_message_destroy(&received);
  if ((errno = pthread_join(secondary_thread, NULL)))
    xperror(*argv);
  return rc;
}


/**
 * Wait for confirmation that we may switch virtual terminal
 * 
 * @param   data  Thread input parameter, will always be `NULL`
 * @return        Thread return value, will always be `NULL`
 */
void* secondary_loop(void* data)
{
  mds_message_t secondary_received;
  int r;
  (void) data;
  
  secondary_thread_started = 1;
  fail_if (mds_message_initialise(&secondary_received) < 0);
  
  while (!reexecing && !terminating)
    {
      if (r = mds_message_read(&secondary_received, secondary_socket_fd), r == 0)
	r = vt_accept_switch(display_tty_fd);
      if (r == 0)
	continue;
      
      if (r == -2)
	{
	  eprint("corrupt message received, aborting.");
	  goto fail;
	}
      else if (errno == EINTR)
	continue;
      else if (errno != ECONNRESET)
	goto pfail;
      
      eprint("lost secondary connection to server.");
      mds_message_destroy(&secondary_received);
      mds_message_initialise(&secondary_received);
      if (reconnect_fd_to_display(&secondary_socket_fd) < 0)
	goto fail;
    }
  
  goto done;
 pfail:
  xperror(*argv);
 fail:
  secondary_thread_failed = 1;
 done:
  secondary_thread_started = 0;
  mds_message_destroy(&secondary_received);
  if (!reexecing && !terminating)
    pthread_kill(master_thread, SIGTERM);
  return NULL;
}


/**
 * Perform a VT switch requested by the OS kernel
 * 
 * @param   leave_foreground  Whether the display is leaving the foreground
 * @return                    Zero on success, -1 on error
 */
int switch_vt(int leave_foreground)
{
  char buf[46 + 12 + 3 * sizeof(int)];
  
  sprintf(buf,
	  "Command: switching-vt\n"
	  "Message ID: %" PRIu32 "\n"
	  "Status: %s\n"
	  "\n",
	  message_id,
	  leave_foreground ? "deactivating" : "activating");
  
  message_id = message_id == UINT32_MAX ? 0 : (message_id + 1);
  
  return -!!full_send(socket_fd, buf, strlen(buf));
}


/**
 * Handle the received message
 * 
 * @return  Zero on success, -1 on error
 */
int handle_message(void)
{
  /* Fetch message headers. */
  
  const char* recv_client_id = "0:0";
  const char* recv_message_id = NULL;
  const char* recv_graphical = "neither";
  const char* recv_exclusive = "neither";
  const char* recv_command = NULL;
  size_t i;
  
#define __get_header(storage, header)  \
  (startswith(received.headers[i], header))  \
    storage = received.headers[i] + strlen(header)
  
  for (i = 0; i < received.header_count; i++)
    {
      if      __get_header(recv_client_id,  "Client ID: ");
      else if __get_header(recv_message_id, "Message ID: ");
      else if __get_header(recv_graphical,  "Graphical: ");
      else if __get_header(recv_exclusive,  "Exclusive: ");
      else if __get_header(recv_command,    "Command: ");
    }
  
#undef __get_header
  
  
  /* Validate headers. */
  
  if (recv_message_id == NULL)
    {
      eprint("received message with ID, ignoring, master server is misbehaving.");
      return 0;
    }
  
  if (strequals(recv_client_id, "0:0"))
    {
      eprint("received information request from an anonymous client, ignoring.");
      return 0;
    }
  
  if (strlen(recv_client_id) > 21)
    {
      eprint("received invalid client ID, ignoring.");
      return 0;
    }
  if (strlen(recv_message_id) > 10)
    {
      eprint("received invalid message ID, ignoring.");
      return 0;
    }
  
  
  /* Take appropriate action. */
  
  if (recv_command == NULL)
    return 0; /* How did that get here, not matter, just ignore it? */
  
  if (strequals(recv_command, "get-vt"))
    return handle_get_vt(recv_client_id, recv_message_id);
  
  if (strequals(recv_command, "configure-vt"))
    return handle_configure_vt(recv_client_id, recv_message_id, recv_graphical, recv_exclusive);
  
  return 0; /* How did that get here, not matter, just ignore it? */
}


/**
 * Handle a received `Command: get-vt` message
 * 
 * @param   client   The value of the header `Client ID` in the received message
 * @param   message  The value of the header `Message ID` in the received message
 * @return           Zero on success, -1 on error
 */
int handle_get_vt(const char* client, const char* message)
{
  char buf[57 + 44 + 3 * sizeof(int)];
  int active = vt_get_active();
  int r;
  
  sprintf(buf,
	  "To: %s\n"
	  "In response to: %s\n"
	  "Message ID: %" PRIu32 "\n"
	  "VT index: %i\n"
	  "Active: %s\n"
	  "\n",
	  client, message, message_id, display_vt,
	  active == display_vt ? "yes" : "no");
  
  message_id = message_id == UINT32_MAX ? 0 : (message_id + 1);
  
  r = full_send(socket_fd, buf, strlen(buf));
  return ((active < 0) || r) ? -1 : 0;
}


/**
 * Handle a received `Command: configure-vt` message
 * 
 * @param   client     The value of the header `Client ID` in the received message
 * @param   message    The value of the header `Message ID` in the received message
 * @param   graphical  The value of the header `Graphical` in the received message
 * @param   exclusive  The value of the header `Exclusive` in the received message
 * @return             Zero on success, -1 on error
 */
int handle_configure_vt(const char* client, const char* message, const char* graphical, const char* exclusive)
{
  char buf[60 + 41 + 3 * sizeof(int)];
  int r = 0;
  
  if (strequals(exclusive, "yes") || strequals(exclusive, "no"))
    r |= vt_set_graphical(display_tty_fd, strequals(exclusive, "yes"));
  
  if (strequals(graphical, "yes") || strequals(graphical, "no"))
    r |= vt_set_exclusive(display_tty_fd, strequals(graphical, "yes"));
  
  sprintf(buf,
	  "Command: error\n"
	  "To: %s\n"
	  "In response to: %s\n"
	  "Message ID: %" PRIu32 "\n"
	  "Error: %i\n"
	  "\n",
	  client, message, message_id, r);
  
  message_id = message_id == UINT32_MAX ? 0 : (message_id + 1);
  
  return -!!full_send(socket_fd, buf, strlen(buf));
}


/**
 * Send a singal to all threads except the current thread
 * 
 * @param  signo  The signal
 */
void signal_all(int signo)
{      
  pthread_t current_thread = pthread_self();
  
  if (pthread_equal(current_thread, master_thread) == 0)
    pthread_kill(master_thread, signo);
  else if (secondary_thread_started)
    pthread_kill(secondary_thread, signo);
}


/**
 * This function is called when the kernel wants
 * to switch foreground virtual terminal
 * 
 * @param  signo  The received signal number
 */
void received_switch_vt(int signo)
{
  int leaving = signo == (SIGRTMIN + 1);
  switching_vt = leaving ? 1 : -1;
}


/**
 * Send a full message even if interrupted
 * 
 * @param   socket   The file descriptor for the socket to use
 * @param   message  The message to send
 * @param   length   The length of the message
 * @return           Zero on success, -1 on error
 */
int full_send(int socket, const char* message, size_t length)
{
  size_t sent;
  
  while (length > 0)
    {
      sent = send_message(socket, message, length);
      if (sent > length)
	{
	  eprint("Sent more of a message than exists in the message, aborting.");
	  return -1;
	}
      else if ((sent < length) && (errno != EINTR))
	{
	  xperror(*argv);
	  return -1;
	}
      message += sent;
      length -= sent;
    }
  return 0;
}


/**
 * Get the index of the next available virtual terminal
 * 
 * @return  -1 on error, 0 if the terminals are exhausted, otherwise the next terminal
 */
int vt_get_next_available(void)
{
  int next_vt = -1;
  int r = ioctl(STDIN_FILENO, VT_OPENQRY, &next_vt);
  if (r < 0)
    return r;
  return ((next_vt < 0) || (MAX_NR_CONSOLES < next_vt)) ? 0 : next_vt;
}


/**
 * Get the currently active virtual terminal
 * 
 * @return  -1 on error, otherwise the current terminal
 */
int vt_get_active(void)
{
  struct vt_stat state;
  if (ioctl(STDIN_FILENO, VT_GETSTATE, &state) < 0)
    return -1;
  return state.v_active;
}


/**
 * Change currently active virtual terminal and wait for it to complete the switch
 * 
 * @param   vt  The index of the terminal
 * @return      Zero on success, -1 on error
 */
int vt_set_active(int vt)
{
  if (ioctl(STDIN_FILENO, VT_ACTIVATE, vt) < 0)
    return -1;
  
  if (ioctl(STDIN_FILENO, VT_WAITACTIVE, vt) < 0)
    xperror(*argv);
  
  return 0;
}


/**
 * Open a virtual terminal
 * 
 * @param   vt        The index of the terminal
 * @param   old_stat  Output parameter for the old file stat for the terminal
 * @return            The file descriptor for the terminal, -1 on error
 */
int vt_open(int vt, struct stat* restrict old_stat)
{
  char vtpath[64]; /* Should be small enought and large enought for any
		      lunatic alternative to /dev/ttyNNN, if not you
		      will need to apply a patch (or fix your system.) */
  int fd;
  sprintf(vtpath, VT_PATH_PATTERN, vt);
  fd = open(vtpath, O_RDWR);
  if (fd < 0)
    return -1;
  if ((fstat(fd, old_stat) < 0) ||
      (fchown(fd, getuid(), getgid()) < 0))
    {
      close(fd);
      return -1;
    }
  return fd;
}


/**
 * Close a virtual terminal
 * 
 * @param  vt        The index of the terminal
 * @param  old_stat  The old file stat for the terminal
 */
void vt_close(int fd, struct stat* restrict old_stat)
{
  if (fchown(fd, old_stat->st_uid, old_stat->st_gid) < 0)
    {
      xperror(*argv);
      eprint("while resetting TTY ownership.");
    }
  close(fd);
}


/**
 * Construct a virtual terminal mode that can be used in `vt_get_set_mode`
 * 
 * @param  vt_switch_control  Whether we want to be able to block and delay VT switches
 * @param  vt_leave_signal    The signal that should be send to us we a process is trying
 *                            to switch terminal to another terminal
 * @param  vt_enter_signal    The signal that should be send to us we a process is trying
 *                            to switch terminal to our terminal
 * @param  mode               Output parameter
 */
void vt_construct_mode(int vt_switch_control, int vt_leave_signal,
		       int vt_enter_signal, struct vt_mode* restrict mode)
{
  mode->mode = vt_switch_control ? VT_PROCESS : VT_AUTO;
  mode->waitv = 0;
  mode->relsig = (short int)vt_leave_signal;
  mode->acqsig = (short int)vt_enter_signal;
}

