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
#ifndef MDS_LIBMDSCLIENT_COMM_H
#define MDS_LIBMDSCLIENT_COMM_H


#include <stdint.h>
#include <stddef.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>



/**
 * A connection to the display server
 */
typedef struct libmds_connection
{
  /**
   * The file descriptor of the socket
   * connected to the display server,
   * -1 if not connected
   */
  int socket_fd;
  
  /**
   * The ID of the _previous_ message
   */
  uint32_t message_id;
  
  /**
   * The client ID, `NULL` if anonymous
   */
  char* client_id;
  
  /**
   * Mutex used to hinder concurrent modification
   * and concurrent message passing
   */
  pthread_mutex_t mutex;
  
} libmds_connection_t;


/**
 * Initialise a connection descriptor with the the default values
 * 
 * @param  this  The connection descriptor
 */
__attribute__((nonnull))
void libmds_connection_initialise(libmds_connection_t* restrict this);

/**
 * Allocate and initialise a connection descriptor
 * 
 * @return  The connection descriptor, `NULL` on error,
 *          `errno` will have been set accordingly on error
 * 
 * @throws  ENOMEM  Out of memory, Possibly, the process hit the RLIMIT_AS or
 *                  RLIMIT_DATA limit described in getrlimit(2).
 */
libmds_connection_t* libmds_connection_create(void);

/**
 * Release all resources held by a connection descriptor
 * 
 * @param  this  The connection descriptor, may be `NULL`
 */
void libmds_connection_destroy(libmds_connection_t* restrict this);

/**
 * Release all resources held by a connection descriptor,
 * and release the allocation of the connection descriptor
 * 
 * @param  this  The connection descriptor, may be `NULL`
 */
void libmds_connection_free(libmds_connection_t* restrict this);

/**
 * TODO doc
 */
__attribute__((nonnull))
int libmds_connection_send(libmds_connection_t* restrict this, const char* message, size_t length);

/**
 * TODO doc
 */
__attribute__((nonnull))
int libmds_connection_send_unlocked(libmds_connection_t* restrict this, const char* message, size_t length);

/**
 * Lock the connection descriptor for being modified,
 * or used to send data to the display, by another thread
 * 
 * @param   this:libmds_connection_t*  The connection descriptor
 * @return  :int                       Zero on success, -1 on error, `errno`
 *                                     will have been set accordingly on error
 * @throws                             See pthread_mutex_lock(3)
 */
#define libmds_connection_lock(this) \
  (errno = pthread_mutex_lock(&((this)->mutex)), (errno ? 0 : -1))

/**
 * Lock the connection descriptor for being modified,
 * or used to send data to the display, by another thread
 * 
 * @param   this:libmds_connection_t*  The connection descriptor
 * @return  :int                       Zero on success, -1 on error, `errno`
 *                                     will have been set accordingly on error
 * @throws                             See pthread_mutex_trylock(3)
 */
#define libmds_connection_trylock(this) \
  (errno = pthread_mutex_trylock(&((this)->mutex)), (errno ? 0 : -1))

/**
 * Lock the connection descriptor for being modified,
 * or used to send data to the display, by another thread
 * 
 * @param   this:libmds_connection_t*                 The connection descriptor
 * @param   deadline:const struct timespec *restrict  The CLOCK_REALTIME time when the function shall fail
 * @return  :int                                      Zero on success, -1 on error, `errno`
 *                                                    will have been set accordingly on error
 * @throws                                            See pthread_mutex_timedlock(3)
 */
#define libmds_connection_timedlock(this, deadline)  \
  (errno = pthread_mutex_timedlock(&((this)->mutex), deadline), (errno ? 0 : -1))

/**
 * Undo the action of `libmds_connection_lock`, `libmds_connection_trylock`
 * or `libmds_connection_timedlock`
 * 
 * @param   this:libmds_connection_t*  The connection descriptor
 * @return  :int                       Zero on success, -1 on error, `errno`
 *                                     will have been set accordingly on error
 * @throws                             See pthread_mutex_unlock(3)
 */
#define libmds_connection_unlock(this)  \
  (errno = pthread_mutex_unlock(&((this)->mutex)), (errno ? 0 : -1))


#endif
