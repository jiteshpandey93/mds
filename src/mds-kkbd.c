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
#include "mds-kkbd.h"

#include <libmdsserver/macros.h>
#include <libmdsserver/util.h>
#include <libmdsserver/mds-message.h>

#include <inttypes.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/kd.h>
#define reconnect_to_display() -1 /* TODO */



#ifdef __sparc__
# define GET_LED  KIOCGLED
# define SET_LED  KIOCSLED
#else
# define GET_LED  KDGETLED
# define SET_LED  KDSETLED
#endif


#ifdef __sparc__
# define LED_NUM_LOCK   1
# define LED_CAPS_LOCK  8
# define LED_SCRL_LOCK  4
# define LED_COMPOSE    2
#else
# define LED_NUM_LOCK   LED_NUM
# define LED_CAPS_LOCK  LED_CAP
# define LED_SCRL_LOCK  LED_SCR
#endif



#define MDS_KKBD_VARS_VERSION  0



/**
 * This variable should declared by the actual server implementation.
 * It must be configured before `main` is invoked.
 * 
 * This tells the server-base how to behave
 */
server_characteristics_t server_characteristics =
  {
    .require_privileges = 0,
    .require_display = 1,
    .require_respawn_info = 0,
    .sanity_check_argc = 1,
    .fork_for_safety = 1
  };



/**
 * Value of the ‘Message ID’ header for the next message
 */
static int32_t message_id = 1;

/**
 * Buffer for received messages
 */
static mds_message_t received;

/**
 * Whether the server is connected to the display
 */
static int connected = 1;

/**
 * File descriptor for accessing the keyboard LED:s
 */
static int ledfd = 0;

/**
 * Saved LED states
 */
static int saved_leds;

/**
 * Saved TTY settings
 */
static struct termios saved_stty;

/**
 * Save keyboard mode
 */
static int saved_kbd_mode;




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
 * This function should initialise the server,
 * and it not invoked after a re-exec.
 * 
 * @return  Non-zero on error
 */
int initialise_server(void)
{
  int stage = 0;
  const char* const message =
    "Command: intercept\n"
    "Message ID: 0\n"
    "Length: 18\n"
    "\n"
    "Command: keyboard\n";
  
  if (full_send(message, strlen(message)))
    return 1;
  
  open_leds();
  stage = 1;
  open_input();
  stage = 2;
  
  fail_if (server_initialised());
  stage = 0;
  fail_if (mds_message_initialise(&received));
  
  return 0;
  
 pfail:
  xperror(*argv);
  if (stage >= 2)  close_input();
  if (stage >= 1)  close_leds();
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
  return 0;
}


/**
 * This function is called by the parent server process when the
 * child server process exits, if the server has completed its
 * initialisation
 * 
 * @param  status  The status the child died with
 */
void fork_cleanup(int status)
{
  (void) status;
  close_input();
  close_leds();
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
  size_t rc = 5 * sizeof(int) + sizeof(int32_t) + sizeof(struct termios);
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
  buf_set_next(state_buf, int, MDS_KKBD_VARS_VERSION);
  buf_set_next(state_buf, int, connected);
  buf_set_next(state_buf, int32_t, message_id);
  buf_set_next(state_buf, int, ledfd);
  buf_set_next(state_buf, int, saved_leds);
  buf_set_next(state_buf, struct termios, saved_stty);
  buf_set_next(state_buf, int, saved_kbd_mode);
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
  /* buf_get_next(state_buf, int, MDS_KKBDOARD_VARS_VERSION); */
  buf_next(state_buf, int, 1);
  buf_get_next(state_buf, int, connected);
  buf_get_next(state_buf, int32_t, message_id);
  buf_get_next(state_buf, int, ledfd);
  buf_get_next(state_buf, int, saved_leds);
  buf_get_next(state_buf, struct termios, saved_stty);
  buf_get_next(state_buf, int, saved_kbd_mode);
  fail_if (mds_message_unmarshal(&received, state_buf));
  
  return 0;
 pfail:
  xperror(*argv);
  mds_message_destroy(&received);
  abort(); /* We must abort on failure to not risk the keyboard
	      getting stuck and freeze up the computer until
	      someone ssh:es into it and kill the server. */
  return -1;
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
  int rc = 1;
  int c, keycode, released;
  int scancode[3];
  
  while ((c = getchar()) != 1) /* Exit with ESCAPE */
    {
    redo:
      keycode = c & 0x7F;
      released = !!(c & 0x80);
      scancode[0] = keycode;
      
      if (keycode == 0)
	{
	  scancode[1] = getchar();
	  if ((scancode[1] & 0x80) == 0)
	    {
	      c = scancode[1];
	      goto redo;
	    }
	  scancode[2] = getchar();
	  if ((scancode[2] & 0x80) == 0)
	    {
	      printf("scancode: %i\n", scancode[1]);
	      printf("keycode: %i\n", scancode[1]);
	      printf("released: no\n");
	      c = scancode[2];
	      goto redo;
	    }
	  keycode = (scancode[1] & 0x7F) << 7;
	  keycode |= (scancode[2] & 0x7F);
	  printf("scancode: %i %i %i\n",
		 scancode[0], scancode[1], scancode[2]);
	}
      else
	printf("scancode: %i\n", scancode[0]);
      printf("keycode: %i\n", keycode);
      printf("released: %s\n", released ? "yes" : "no");
    }
  
  /*
  while (!reexecing && !terminating)
    {
      int r = mds_message_read(&received, socket_fd);
      if (r == 0)
	{
	  if (r = 0, r == 0) \/* TODO handle_message() *\/
	    continue;
	}
      
      if (r == -2)
	{
	  eprint("corrupt message received, aborting.");
	  goto fail;
	}
      else if (errno == EINTR)
	continue;
      else if (errno != ECONNRESET)
	goto pfail;
      
      eprint("lost connection to server.");
      mds_message_destroy(&received);
      mds_message_initialise(&received);
      connected = 0;
      if (reconnect_to_display())
	goto fail;
      connected = 1;
    }
  */
  
  rc = 0;
  goto fail;
 pfail:
  xperror(*argv);
 fail:
  if (!rc && reexecing)
    return 0;
  mds_message_destroy(&received);
  return rc;
}


/**
 * Send a full message even if interrupted
 * 
 * @param   message  The message to send
 * @param   length   The length of the message
 * @return           Non-zero on success
 */
int full_send(const char* message, size_t length)
{
  size_t sent;
  
  while (length > 0)
    {
      sent = send_message(socket_fd, message, length);
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
 * Acquire access of the keyboard's LED:s
 * 
 * @return  Zero on success, -1 on error
 */
int open_leds(void)
{
#ifdef __sparc__
  if ((ledfd = open(SPARC_KBD, O_RDONLY)) < 0)
    return -1;
  if (ioctl(ledfd, GET_LED, &saved_leds) < 0)
    {
      close(ledfd);
      return -1;
    }
  return 0;
#else
  return ioctl(ledfd, GET_LED, &saved_leds);
#endif
}


/**
 * Release access of the keyboard's LED:s
 */
void close_leds(void)
{
  if (ioctl(ledfd, SET_LED, saved_leds) < 0)
    xperror(*argv);
#ifdef __sparc__
  close(ledfd);
#endif
}


/**
 * Get active LED:s on the keyboard
 * 
 * @return  Active LED:s, -1 on error
 */
int get_leds(void)
{
  int leds;
  if (ioctl(ledfd, GET_LED, &leds) < 0)
    return -1;
#ifdef __sparc__
  leds &= 15;
#endif
  return leds;
}


/**
 * Set active LED:s on the keyboard
 * 
 * @param   leds  Active LED:s
 * @return        Zero on success, -1 on error
 */
int set_leds(int leds)
{
  return ioctl(ledfd, SET_LED, leds);
}


/**
 * Acquire access of keyboard input
 * 
 * @return  Zero on success, -1 on error
 */
int open_input(void)
{
  struct termios stty;
  if (tcgetattr(STDIN_FILENO, &saved_stty) < 0)
    return -1;
  stty = saved_stty;
  stty.c_lflag &= (tcflag_t)~(ECHO | ICANON | ISIG);
  stty.c_iflag = 0;
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &stty) < 0)
    return -1;
  /* K_MEDIUMRAW: utilise keyboard drivers, but not layout */
  if ((ioctl(STDIN_FILENO, KDGKBMODE, &saved_kbd_mode) < 0) ||
      (ioctl(STDIN_FILENO, KDSKBMODE, K_MEDIUMRAW) < 0))
    {
      xperror(*argv);
      return tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved_stty);
    }
  return 0;
}


/**
 * Release access of keyboard input
 */
void close_input(void)
{
  if (ioctl(STDIN_FILENO, KDSKBMODE, saved_kbd_mode) < 0)
    xperror(*argv);
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved_stty) < 0)
    xperror(*argv);
}


/* TODO delay and repetition */
