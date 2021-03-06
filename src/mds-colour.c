/**
 * mds — A micro-display server
 * Copyright © 2014, 2015  Mattias Andrée (maandree@member.fsf.org)
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
#include "mds-colour.h"
/* TODO reload settings on SIGUSR2 */
/* TODO --save-changes */

#include <libmdsserver/macros.h>
#include <libmdsserver/util.h>
#include <libmdsserver/mds-message.h>
#include <libmdsserver/hash-list.h>
#include <libmdsserver/hash-help.h>

#include <errno.h>
#include <inttypes.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#define reconnect_to_display() -1



#define MDS_COLOUR_VARS_VERSION  0



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
 * Buffer for sending messages
 */
static char* send_buffer = NULL;

/**
 * The size allocated to `send_buffer` divided by `sizeof(char)`
 */
static size_t send_buffer_size = 0;

/**
 * List of all colours
 */
static colour_list_t colours;

/**
 * Textual list of all colours that can be sent
 * to clients upon query. This list only includes names.
 */
static char* colour_list_buffer_without_values = NULL;

/**
 * Textual list of all colours that can be sent
 * to clients upon query. This list include value.
 */
static char* colour_list_buffer_with_values = NULL;

/**
 * The length of the message in `colour_list_buffer_without_values`
 * assuming it is not `NULL`, if it is `NULL` this variable has no
 * meaning and is not guaranteed to be zero.
 */
static size_t colour_list_buffer_without_values_length = 0;

/**
 * The length of the message in `colour_list_buffer_with_values`
 * assuming it is not `NULL`, if it is `NULL` this variable has no
 * meaning and is not guaranteed to be zero.
 */
static size_t colour_list_buffer_with_values_length = 0;



/**
 * Send a full message even if interrupted
 * 
 * @param   message:const char*  The message to send
 * @param   length:size_t        The length of the message
 * @return  :int                 Zero on success, -1 on error
 */
#define full_send(message, length)  \
  ((full_send)(socket_fd, message, length))

/**
 * Send an error message, message ID will be incremented
 * 
 * @param   recv_client_id:const char*   The client ID attached on the message that was received,
 *                                       must not be `NULL`
 * @param   recv_message_id:const char*  The message ID attached on the message that was received,
 *                                       must not be `NULL`
 * @param   recv_command:const char*     The value of the `Command`-header on the message that was
 *                                       received, must not be `NULL`
 * @param   custom:int                   Non-zero if the error is a custom error
 * @param   errnum:int                   The error number, `errno` should be used if the error
 *                                       is not a custom error, zero should be used on success,
 *                                       a negative integer, such as -1, indicates that the error
 *                                       message should not include an error number,
 *                                       it is not allowed to have this value be negative and
 *                                       `custom` be zero at the same time
 * @param   message:const char*          The description of the error, the line feed at the end
 *                                       is added automatically, `NULL` if the description should
 *                                       be omitted
 * @return                               Zero on success, -1 on error
 */
#define send_error(recv_client_id, recv_message_id, recv_command, custom, errnum, message)	\
  ((send_error)(recv_client_id, recv_message_id, recv_command, custom, errnum,			\
		message, &send_buffer, &send_buffer_size, message_id, socket_fd)		\
   ? -1 : ((message_id = message_id == INT32_MAX ? 0 : (message_id + 1)), 0))


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
    "Length: 62\n"
    "\n"
    "Command: list-colours\n"
    "Command: get-colour\n"
    "Command: set-colour\n";
  
  fail_if (full_send(message, strlen(message)));
  fail_if (server_initialised() < 0);  stage++;;
  fail_if (colour_list_create(&colours, 64) < 0);  stage++;
  fail_if (mds_message_initialise(&received));
  
  return 0;
 fail:
  xperror(*argv);
  if (stage >= 2)  colour_list_destroy(&colours);
  if (stage >= 1)  mds_message_destroy(&received);
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
  colours.freer = colour_list_entry_free;
  colours.hasher = string_hash;
  
  if (connected)
    return 0;
  
  fail_if (reconnect_to_display());
  connected = 1;
  return 0;
 fail:
  mds_message_destroy(&received);
  return 1;
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
  size_t rc = 2 * sizeof(int) + sizeof(uint32_t);
  rc += mds_message_marshal_size(&received);
  rc += colour_list_marshal_size(&colours);
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
  buf_set_next(state_buf, int, MDS_COLOUR_VARS_VERSION);
  buf_set_next(state_buf, int, connected);
  buf_set_next(state_buf, uint32_t, message_id);
  
  mds_message_marshal(&received, state_buf);
  state_buf += mds_message_marshal_size(&received) / sizeof(char*);
  
  colour_list_marshal(&colours, state_buf);
  
  mds_message_destroy(&received);
  colour_list_destroy(&colours);
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
  int stage = 0;
  
  /* buf_get_next(state_buf, int, MDS_COLOUR_VARS_VERSION); */
  buf_next(state_buf, int, 1);
  buf_get_next(state_buf, int, connected);
  buf_get_next(state_buf, uint32_t, message_id);
  
  fail_if (mds_message_unmarshal(&received, state_buf));
  state_buf += mds_message_marshal_size(&received) / sizeof(char*);
  stage++;
  
  fail_if (colour_list_unmarshal(&colours, state_buf));
  
  return 0;
 fail:
  xperror(*argv);
  if (stage >= 0)  mds_message_destroy(&received);
  if (stage >= 1)  colour_list_destroy(&colours);
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
  int rc = 1, r;
  
  while (!reexecing && !terminating)
    {
      if (danger)
	{
	  danger = 0;
	  free(send_buffer), send_buffer = NULL;
	  send_buffer_size = 0;
	  colour_list_pack(&colours);
	  free(colour_list_buffer_without_values),
	    colour_list_buffer_without_values = NULL;
	  free(colour_list_buffer_with_values),
	    colour_list_buffer_with_values = NULL;
	}
      
      if (r = mds_message_read(&received, socket_fd), r == 0)
	if (r = handle_message(), r == 0)
	  continue;
      
      if (r == -2)
	{
	  eprint("corrupt message received, aborting.");
	  goto done;
	}
      else if (errno == EINTR)
	continue;
      else
	fail_if (errno != ECONNRESET);
      
      eprint("lost connection to server.");
      mds_message_destroy(&received);
      mds_message_initialise(&received);
      connected = 0;
      fail_if (reconnect_to_display());
      connected = 1;
    }
  
  rc = 0;
  goto done;
 fail:
  xperror(*argv);
 done:
  if (rc || !reexecing)
    {
      mds_message_destroy(&received);
      colour_list_destroy(&colours);
    }
  free(send_buffer);
  free(colour_list_buffer_without_values);
  free(colour_list_buffer_with_values);
  return rc;
}


/**
 * Handle the received message
 * 
 * @return  Zero on success, -1 on error
 */
int handle_message(void)
{
  const char* recv_command = NULL;
  const char* recv_client_id = "0:0";
  const char* recv_message_id = NULL;
  const char* recv_include_values = NULL;
  const char* recv_name = NULL;
  const char* recv_remove = NULL;
  const char* recv_bytes = NULL;
  const char* recv_red = NULL;
  const char* recv_green = NULL;
  const char* recv_blue = NULL;
  size_t i;
  
#define __get_header(storage, header)			\
  (startswith(received.headers[i], header))		\
    storage = received.headers[i] + strlen(header)
  
  for (i = 0; i < received.header_count; i++)
    {
      if      __get_header(recv_command,        "Command: ");
      else if __get_header(recv_client_id,      "Client ID: ");
      else if __get_header(recv_message_id,     "Message ID: ");
      else if __get_header(recv_include_values, "Include values: ");
      else if __get_header(recv_name,           "Name: ");
      else if __get_header(recv_remove,         "Remove: ");
      else if __get_header(recv_bytes,          "Bytes: ");
      else if __get_header(recv_red,            "Red: ");
      else if __get_header(recv_green,          "Green: ");
      else if __get_header(recv_blue,           "Blue: ");
    }
  
#undef __get_header
  
  if (recv_message_id == NULL)
    {
      eprint("received message without ID, ignoring, master server is misbehaving.");
      return 0;
    }
  
  if (recv_command == NULL)
    return 0; /* How did that get here, not matter, just ignore it? */

#define t(expr)  do { fail_if (expr); return 0; } while (0)
  if (strequals(recv_command, "list-colours"))
    t (handle_list_colours(recv_client_id, recv_message_id, recv_include_values));
  if (strequals(recv_command, "get-colour"))
    t (handle_get_colour(recv_client_id, recv_message_id, recv_name));
  if (strequals(recv_command, "set-colour"))
    t (handle_set_colour(recv_name, recv_remove, recv_bytes, recv_red, recv_green, recv_blue));
#undef t
  
  return 0; /* How did that get here, not matter, just ignore it? */
 fail:
  return -1;
}


/**
 * Create a textual list of all colours that can be sent
 * to clients upon query. This list will only include names,
 * and will be stored as `colour_list_buffer_without_values`.
 * 
 * @return  Zero on success, -1 on error
 */
static int create_colour_list_buffer_without_values(void)
{
  size_t i, length = 1;
  colour_list_entry_t* entry;
  char* temp;
  int saved_errno;
  
  foreach_hash_list_entry (colours, i, entry)
    length += strlen(entry->key) + 1;
  
  fail_if (yrealloc(temp, colour_list_buffer_without_values, length, char));
  temp = colour_list_buffer_without_values;
  colour_list_buffer_without_values_length = length - 1;
  
  *temp = '\0';
  foreach_hash_list_entry (colours, i, entry)
    temp = stpcpy(temp, entry->key++), *temp++ = '\n';
  *temp = '\0';
  
  return 0;
 fail:
  saved_errno = errno;
  free(colour_list_buffer_without_values);
  colour_list_buffer_without_values = NULL;
  return errno = saved_errno, -1;
}


/**
 * Create a textual list of all colours that can be sent
 * to clients upon query. This list will include value,
 * and will be stored as `colour_list_buffer_with_values`.
 * 
 * @return  Zero on success, -1 on error
 */
static int create_colour_list_buffer_with_values(void)
{
  size_t i, length = 1;
  ssize_t part_length;
  colour_list_entry_t* entry;
  char* temp;
  int saved_errno;
  
  foreach_hash_list_entry (colours, i, entry)
    snprintf(NULL, 0, "%i %"PRIu64" %"PRIu64" %"PRIu64" %s\n%zn",
	     entry->value.bytes, entry->value.red, entry->value.green,
	     entry->value.blue, entry->key, &part_length),
      length += (size_t)part_length;
  
  fail_if (yrealloc(temp, colour_list_buffer_with_values, length, char));
  temp = colour_list_buffer_with_values;
  colour_list_buffer_with_values_length = length - 1;
  
  foreach_hash_list_entry (colours, i, entry)
    sprintf(temp, "%i %"PRIu64" %"PRIu64" %"PRIu64" %s\n%zn",
	    entry->value.bytes, entry->value.red, entry->value.green,
	    entry->value.blue, entry->key, &part_length),
      temp += (size_t)part_length;
  
  return 0;
 fail:
  saved_errno = errno;
  free(colour_list_buffer_with_values);
  colour_list_buffer_with_values = NULL;
  return errno = saved_errno, -1;
}


/**
 * Handle the received message after it has been
 * identified to contain `Command: list-colours`
 * 
 * @param   recv_client_id       The value of the `Client ID`-header, "0:0" if omitted
 * @param   recv_message_id      The value of the `Message ID`-header
 * @param   recv_include_values  The value of the `Include values`-header, `NULL` if omitted
 * @return                       Zero on success, -1 on error
 */
int handle_list_colours(const char* recv_client_id, const char* recv_message_id,
			const char* recv_include_values)
{
  int include_values = 0;
  char* payload;
  size_t payload_length;
  size_t length;
  char* temp;
  
  if (strequals(recv_client_id, "0:0"))
    return eprint("got a query from an anonymous client, ignoring."), 0;
  
  if      (recv_include_values == NULL)            include_values = 0;
  else if (strequals(recv_include_values, "yes"))  include_values = 1;
  else if (strequals(recv_include_values, "no"))   include_values = 0;
  else
    {
      fail_if (send_error(recv_client_id, recv_message_id, "list-colours", 0, EPROTO, NULL));
      return 0;
    }
  
  if ((colour_list_buffer_with_values == NULL) && include_values)
    fail_if (create_colour_list_buffer_with_values());
  else if ((colour_list_buffer_without_values == NULL) && !include_values)
    fail_if (create_colour_list_buffer_without_values());
  
  payload = include_values
    ? colour_list_buffer_with_values
    : colour_list_buffer_without_values;
  
  payload_length = include_values
    ? colour_list_buffer_with_values_length
    : colour_list_buffer_without_values_length;
  
  length = sizeof("To: \n"
		  "In response to: \n"
		  "Message ID: \n"
		  "Origin command: list-colours\n"
		  "\n") / sizeof(char);
  length += strlen(recv_client_id) + strlen(recv_message_id) + 10 + payload_length;
  
  if (length > send_buffer_size)
    {
      fail_if (yrealloc(temp, send_buffer, length, char));
      send_buffer_size = length;
    }
  
  sprintf(send_buffer,
	  "To: %s\n"
	  "In response to: %s\n"
	  "Message ID: %"PRIu32"\n"
	  "Origin command: list-colours\n"
	  "\n%zn",
	  recv_client_id, recv_message_id, message_id,
	  (ssize_t*)&length);
  memcpy(send_buffer + length, payload, payload_length * sizeof(char));
  length += payload_length;
  
  fail_if (full_send(send_buffer, length));
  return 0;
 fail:
  return -1;
}


/**
 * Handle the received message after it has been
 * identified to contain `Command: get-colour`
 * 
 * @param   recv_client_id   The value of the `Client ID`-header, "0:0" if omitted
 * @param   recv_message_id  The value of the `Message ID`-header
 * @param   recv_name        The value of the `Name`-header, `NULL` if omitted
 * @return                   Zero on success, -1 on error
 */
int handle_get_colour(const char* recv_client_id, const char* recv_message_id, const char* recv_name)
{
  colour_t colour;
  size_t length;
  char* temp;
  
  if (strequals(recv_client_id, "0:0"))
    return eprint("got a query from an anonymous client, ignoring."), 0;
  
  if (recv_name == NULL)
    {
      fail_if (send_error(recv_client_id, recv_message_id, "get-colour", 0, EPROTO, NULL));
      return 0;
    }
  
  if (!colour_list_get(&colours, recv_name, &colour))
    {
      fail_if (send_error(recv_client_id, recv_message_id, "get-colour", 1, -1, "not defined"));
      return 0;
    }
  
  length = sizeof("To: \n"
		  "In response to: \n"
		  "Message ID: \n"
		  "Origin command: get-colour\n"
		  "Bytes: \n"
		  "Red: \n"
		  "Green; \n"
		  "Blue: \n"
		  "\n") / sizeof(char);
  length += strlen(recv_client_id) + strlen(recv_message_id) + 10 + 1 + 3 * 3 * (size_t)(colour.bytes);
  
  if (length > send_buffer_size)
    {
      fail_if (yrealloc(temp, send_buffer, length, char));
      send_buffer_size = length;
    }
  
  sprintf(send_buffer,
	  "To: %s\n"
	  "In response to: %s\n"
	  "Message ID: %"PRIu32"\n"
	  "Origin command: get-colour\n"
	  "Bytes: %i\n"
	  "Red: %"PRIu64"\n"
	  "Green; %"PRIu64"\n"
	  "Blue: %"PRIu64"\n"
	  "\n",
	  recv_client_id, recv_message_id, message_id, colour.bytes,
	  colour.red, colour.green, colour.blue);
  
  message_id = message_id == UINT32_MAX ? 0 : (message_id + 1);
  
  fail_if (full_send(send_buffer, (size_t)length));
  return 0;
 fail:
  return -1;
}


/**
 * Handle the received message after it has been
 * identified to contain `Command: set-colour`
 * 
 * @param   recv_name    The value of the `Name`-header, `NULL` if omitted
 * @param   recv_remove  The value of the `Remove`-header, `NULL` if omitted
 * @param   recv_bytes   The value of the `Bytes`-header, `NULL` if omitted
 * @param   recv_red     The value of the `Red`-header, `NULL` if omitted
 * @param   recv_green   The value of the `Green`-header, `NULL` if omitted
 * @param   recv_blue    The value of the `Blue`-header, `NULL` if omitted
 * @return               Zero on success, -1 on error
 */
int handle_set_colour(const char* recv_name, const char* recv_remove, const char* recv_bytes,
		      const char* recv_red, const char* recv_green, const char* recv_blue)
{
  uint64_t limit = UINT64_MAX;
  int remove_colour = 0;
  colour_t colour;
  int bytes;
  
  if      (recv_remove == NULL)            remove_colour = 0;
  else if (strequals(recv_remove, "yes"))  remove_colour = 1;
  else if (strequals(recv_remove, "no"))   remove_colour = 0;
  else
    return eprint("got an invalid value on the Remove-header, ignoring."), 0;
  
  if (recv_name == NULL)
    return eprint("did not get all required headers, ignoring."), 0;
  
  if (remove_colour == 0)
    {
      if ((recv_bytes == NULL) || (recv_red == NULL) || (recv_green == NULL) || (recv_blue == NULL))
	return eprint("did not get all required headers, ignoring."), 0;
      
      if (strict_atoi(recv_bytes, &bytes, 1, 8))
	return eprint("got an invalid value on the Bytes-header, ignoring."), 0;
      if ((bytes != 1) && (bytes != 2) && (bytes != 4) && (bytes != 8))
	return eprint("got an invalid value on the Bytes-header, ignoring."), 0;
      
      if (bytes < 8)
	limit = (((uint64_t)1) << (bytes * 8)) - 1;
      
      colour.bytes = bytes;
      if (strict_atou64(recv_red, &(colour.red), 0, limit))
	return eprint("got an invalid value on the Red-header, ignoring."), 0;
      if (strict_atou64(recv_green, &(colour.green), 0, limit))
	return eprint("got an invalid value on the Green-header, ignoring."), 0;
      if (strict_atou64(recv_blue, &(colour.blue), 0, limit))
	return eprint("got an invalid value on the Blue-header, ignoring."), 0;
      
      fail_if (set_colour(recv_name, &colour));
    }
  else
    fail_if (set_colour(recv_name, NULL));
  
  return 0;
 fail:
  return -1;
}


/**
 * Check whether two colours are identical
 * 
 * @param   colour_a  One of the colours
 * @param   colour_b  The other colour
 * @return            1 if the colours are equal, 0 otherwise
 */
static inline int colourequals(const colour_t* colour_a, const colour_t* colour_b)
{
  size_t rc = (size_t)(colour_a->bytes - colour_b->bytes);
  rc |= colour_a->red   - colour_b->red;
  rc |= colour_a->green - colour_b->green;
  rc |= colour_a->blue  - colour_b->blue;
  return !rc;
}


/**
 * Add, remove or modify a colour
 * 
 * @param   name    The name of the colour, must not be `NULL`
 * @param   colour  The colour, `NULL` to remove
 * @return          Zero on success, -1 on error, removal of
 *                  non-existent colour does not constitute an error
 */
int set_colour(const char* name, const colour_t* colour)
{
  char* name_ = NULL;
  int found;
  colour_t old_colour;
  int saved_errno;
  
  /* Find out whether the colour is already defined,
     and retrieve its value. This is required so we
     can broadcast the proper event. */
  found = colour_list_get(&colours, name, &old_colour);
  
  if (colour == NULL)
    {
      /* We have been asked to remove the colour. */
      
      /* If the colour does not exist, there is no
         point in removing it, and an event should
         not be broadcasted. */
      if (found == 0)
	return 0;
      
      /* Remove the colour. */
      colour_list_remove(&colours, name);
      
      /* Broadcast update event. */
      fail_if (broadcast_update("colour-removed", name, NULL, "yes"));
    }
  else
    {
      /* We have been asked to add or modify the colour. */
      
      /* `colour_list_put` will store the name of the colour,
         so we have to make a copy that will not disappear. */
      fail_if (xstrdup(name_, name));
      
      /* Add or modify the colour. */
      fail_if (colour_list_put(&colours, name_, colour));
      
      /* Broadcast update event. */
      if (found == 0)
	fail_if (broadcast_update("colour-added", name, colour, "yes"));
      else if (!colourequals(colour, &old_colour))
	fail_if (broadcast_update("colour-changed", name, colour, "yes"));
    }
  
  return 0;
 fail:
  saved_errno = errno;
  /* On failure, `colour_list_put` does not
     free the colour name we gave it. */
  free(name_);
  return errno = saved_errno, -1;
}


/**
 * Broadcast a colour list update event
 * 
 * @param   event        The event, that is, the value for the `Command`-header, must not be `NULL`
 * @param   name         The name of the colour, must not be `NULL`
 * @param   colour       The new colour, `NULL` if and only if removed
 * @param   last_update  The value on the `Last update`-header
 * @return               Zero on success, -1 on error
 */
int broadcast_update(const char* event, const char* name, const colour_t* colour, const char* last_update)
{
  ssize_t part_length;
  size_t length;
  char* temp;
  
  free(colour_list_buffer_without_values), colour_list_buffer_without_values = NULL;
  free(colour_list_buffer_with_values),    colour_list_buffer_with_values = NULL;
  
  length = sizeof("Command: \nMessage ID: \nName: \nLast update: \n\n") / sizeof(char) - 1;
  length += strlen(event) + 10 + strlen(name) + strlen(last_update);
  
  if (colour != NULL)
    {
      length += sizeof("Bytes: \nRed: \nBlue: \nGreen: \n") / sizeof(char) - 1;
      length += 1 + 3 * 3 * (size_t)(colour->bytes);
    }
  
  if (++length > send_buffer_size)
    {
      fail_if (yrealloc(temp, send_buffer, length, char));
      send_buffer_size = length;
    }
  
  length = 0;
  
  sprintf(send_buffer + length,
	  "Command: %s\n"
	  "Message ID: %"PRIu32"\n"
	  "Name: %s\n%zn",
	  event, message_id, name, &part_length),
    length += (size_t)part_length;
  
  if (colour != NULL)
    sprintf(send_buffer + length,
	    "Bytes: %i\n"
	    "Red: %"PRIu64"\n"
	    "Blue: %"PRIu64"\n"
	    "Green: %"PRIu64"\n%zn",
	    colour->bytes, colour->red, colour->green,
	    colour->blue, &part_length),
      length += (size_t)part_length;
  
  sprintf(send_buffer + length,
	  "Last update: %s\n"
	  "\n%zn",
	  last_update, &part_length),
    length += (size_t)part_length;
  
  message_id = message_id == UINT32_MAX ? 0 : (message_id + 1);
  
  fail_if (full_send(send_buffer, length));
  return 0;
 fail:
  return -1;
}


/**
 * This function is called when a signal that
 * signals that the system to dump state information
 * and statistics has been received
 * 
 * @param  signo  The signal that has been received
 */
void received_info(int signo)
{
  SIGHANDLER_START;
  size_t i;
  colour_list_entry_t* entry;
  (void) signo;
  iprintf("next message ID: %" PRIu32, message_id);
  iprintf("connected: %s", connected ? "yes" : "no");
  iprintf("send buffer size: %zu bytes", send_buffer_size);
  iprint("DEFINED COLOURS (bytes red green blue name-hash name)");
  foreach_hash_list_entry (colours, i, entry)
    iprintf("%i %"PRIu64" %"PRIu64" %"PRIu64" %zu %s",
	    entry->value.bytes, entry->value.red, entry->value.green,
	    entry->value.blue, entry->key_hash, entry->key);
  iprint("END DEFINED COLOURS");
  SIGHANDLER_END;
}


/**
 * Comparing keys
 * 
 * @param   key_a  The first key, will never be `NULL`
 * @param   key_b  The second key, will never be `NULL`
 * @return         Whether the keys are equal
 */
static inline int colour_list_key_comparer(const char* key_a, const char* key_b)
{
  return !strcmp(key_a, key_b);
}


/**
 * Determine the marshal-size of an entry's key and value
 * 
 * @param   entry  The entry, will never be `NULL`, any only used entries will be passed
 * @return         The marshal-size of the entry's key and value
 */
static inline size_t colour_list_submarshal_size(const colour_list_entry_t* entry)
{
  return sizeof(colour_t) + (strlen(entry->key) + 1) * sizeof(char);
}


/**
 * Marshal an entry's key and value
 * 
 * @param   entry  The entry, will never be `NULL`, any only used entries will be passed
 * @param   data   The buffer where the entry's key and value will be stored
 * @return         The marshal-size of the entry's key and value
 */
static inline size_t colour_list_submarshal(const colour_list_entry_t* entry, char* restrict data)
{
  size_t n = (strlen(entry->key) + 1) * sizeof(char);
  memcpy(data, &(entry->value), sizeof(colour_t));
  data += sizeof(colour_t) / sizeof(char);
  memcpy(data, entry->key, n);
  return sizeof(colour_t) + n;
}


/**
 * Unmarshal an entry's key and value
 * 
 * @param   entry  The entry, will never be `NULL`, any only used entries will be passed
 * @param   data   The buffer where the entry's key and value is stored
 * @return         The number of read bytes, zero on error
 */
static inline size_t colour_list_subunmarshal(colour_list_entry_t* entry, char* restrict data)
{
  size_t n = 0;
  memcpy(&(entry->value), data, sizeof(colour_t));
  data += sizeof(colour_t) / sizeof(char);
  while (data[n++]);
  n *= sizeof(char);
  fail_if (xbmalloc(entry->key, n));
  memcpy(entry->key, data, n);
  return sizeof(colour_t) + n;
 fail:
  return 0;
}


/**
 * Free the key and value of an entry in a colour list
 * 
 * @param  entry  The entry
 */
void colour_list_entry_free(colour_list_entry_t* entry)
{
  free(entry->key);
}

