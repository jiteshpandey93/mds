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
#ifndef MDS_LIBMDSSERVER_MACROS_H
#define MDS_LIBMDSSERVER_MACROS_H


#include "config.h"
#include "macro-bits.h"

#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <stddef.h>
#include <sys/socket.h>

/*
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include <dirent.h>
#include <stdlib.h>
*/


/* # pragma GCC diagnostic ignored "-Wpedantic" */


/* CLOCK_MONOTONIC_RAW is a Linux-specific bug-fix */
#ifndef CLOCK_MONOTONIC_RAW
# define CLOCK_MONOTONIC_RAW  CLOCK_MONOTONIC
#endif

/* Define TEMP_FAILURE_RETRY if not defined, however
 * this version does not return a value, it will hoever
 * clear `errno` if no error occurs. */
#ifndef TEMP_FAILURE_RETRY
# define TEMP_FAILURE_RETRY(expression)			\
  do							\
    {							\
      ssize_t __result;					\
      do						\
	__result = (ssize_t)(expression);		\
      while ((__result < 0) && (errno == EINTR));	\
      if (__result >= 0)				\
	errno = 0;					\
    }							\
  while (0)
# define MDS_LIBMDSSERVER_MACROS_DEFINED_TEMP_FAILURE_RETRY
#endif


/* Ensure that all aliases for AF_UNIX are defined */
#if !defined(AF_LOCAL) && !defined(AF_UNIX) && defined(AF_FILE)
# define AF_LOCAL AF_FILE
# define AF_UNIX AF_FILE
#elif !defined(AF_LOCAL) && defined(AF_UNIX)
# define AF_LOCAL AF_UNIX
#elif defined(AF_LOCAL) && !defined(AF_UNIX)
# define AF_UNIX AF_LOCAL
#endif
#if !defined(AF_FILE) && defined(AF_LOCAL)
# define AF_FILE AF_LOCAL
#endif

/* Ensure that all aliases for PF_UNIX are defined */
#if !defined(PF_LOCAL) && !defined(PF_UNIX) && defined(PF_FILE)
# define PF_LOCAL PF_FILE
# define PF_UNIX PF_FILE
#elif !defined(PF_LOCAL) && defined(PF_UNIX)
# define PF_LOCAL PF_UNIX
#elif defined(PF_LOCAL) && !defined(PF_UNIX)
# define PF_UNIX PF_LOCAL
#endif
#if !defined(PF_FILE) && defined(PF_LOCAL)
# define PF_FILE PF_LOCAL
#endif

/* Ensure that all aliases for AF_NETLINK are defined */
#if !defined(AF_NETLINK) && defined(AF_ROUTE)
# define AF_NETLINK AF_ROUTE
#elif defined(AF_NETLINK) && !defined(AF_ROUTE)
# define AF_ROUTE AF_NETLINK
#endif

/* Ensure that all aliases for PF_NETLINK are defined */
#if !defined(PF_NETLINK) && defined(PF_ROUTE)
# define PF_NETLINK PF_ROUTE
#elif defined(PF_NETLINK) && !defined(PF_ROUTE)
# define PF_ROUTE PF_NETLINK
#endif



/**
 * Wrapper around `asprintf` that makes sure that first
 * argument gets set to `NULL` on error and that zero is
 * returned on success rather than the number of printed
 * characters
 * 
 * @param   VAR:char**            The output parameter for the string
 * @param   ...:const char*, ...  The format string and arguments
 * @return  :int                  Zero on success, -1 on error
 */
#define xasprintf(VAR, ...)					\
  (asprintf(&(VAR), __VA_ARGS__) < 0 ? (VAR = NULL, -1) : 0)
/*
#define xasprintf(VAR, ...)								\
  ({											\
    int _x_rc = (asprintf(&(VAR), __VA_ARGS__) < 0 ? (VAR = NULL, -1) : 0);		\
    fprintf(stderr, "xasprintf(%s, %s)(=%zu) @ %s:%i\n",				\
	    #VAR, #__VA_ARGS__, _x_rc ? 0 : (strlen(VAR) + 1), __FILE__, __LINE__);	\
    _x_rc;										\
  })
*/


/**
 * Wrapper for `snprintf` that allows you to forget about the buffer size
 * 
 * @param   buffer:char[]         The buffer, must be of the type `char[]` and not `char*`
 * @param   ...:const char*, ...  The format string and arguments
 * @return  :int                  The number of bytes written, including the NUL-termination, negative on error
 */
#define xsnprintf(buffer, ...)  \
  snprintf(buffer, sizeof(buffer) / sizeof(char), __VA_ARGS__)


/**
 * Wrapper for `fprintf` that prints to `stderr` with
 * the program name prefixed and new line suffixed
 * 
 * @param   format:const char*  The format
 * @return  :int                The number of bytes written, including the NUL-termination, negative on error
 */
#define eprint(format)  \
  fprintf(stderr, "%s: " format "\n", *argv)


/**
 * Wrapper for `fprintf` that prints to `stderr` with
 * the program name prefixed and new line suffixed
 * 
 * @param   format:const char*  The format
 * @param   ...                 The arguments
 * @return  :int                The number of bytes written, including the NUL-termination, negative on error
 */
#define eprintf(format, ...)  \
  fprintf(stderr, "%s: " format "\n", *argv, __VA_ARGS__)


/**
 * Wrapper for `fprintf` that prints to `stderr` with
 * the prefixed with information telling the user that
 * the output is part of an state and statistics dump
 * and a new line suffixed
 * 
 * @param   format:const char*  The format
 * @return  :int                The number of bytes written, including the NUL-termination, negative on error
 */
#define iprint(format)  \
  fprintf(stderr, "%s: info: " format "\n", *argv)


/**
 * Wrapper for `fprintf` that prints to `stderr` with
 * the prefixed with information telling the user that
 * the output is part of an state and statistics dump
 * and a new line suffixed
 * 
 * @param   format:const char*  The format
 * @param   ...                 The arguments
 * @return  :int                The number of bytes written, including the NUL-termination, negative on error
 */
#define iprintf(format, ...)  \
  fprintf(stderr, "%s: info: " format "\n", *argv, __VA_ARGS__)


/**
 * Wrapper for `pthread_mutex_lock` and `pthread_mutex_unlock`
 * 
 * @param  mutex:pthread_mutex_t  The mutex
 * @param  instructions           The instructions to run while the mutex is locked
 */
#define with_mutex(mutex, instructions)		\
  do						\
    {						\
      errno = pthread_mutex_lock(&(mutex));	\
      do					\
	{					\
	  instructions ;			\
	}					\
      while (0);				\
      errno = pthread_mutex_unlock(&(mutex));	\
    }						\
  while (0)

/**
 * Wrapper for `pthread_mutex_lock` and `pthread_mutex_unlock` with an embedded if-statement
 * 
 * @param  mutex:pthread_mutex_t  The mutex
 * @parma  condition              The condition to test
 * @param  instructions           The instructions to run while the mutex is locked
 */
#define with_mutex_if(mutex, condition, instructions)	\
  do							\
    {							\
      errno = pthread_mutex_lock(&(mutex));		\
      if (condition)					\
        do						\
	  {						\
	    instructions ;				\
	  }						\
        while (0);					\
      errno = pthread_mutex_unlock(&(mutex));		\
    }							\
  while (0)


/**
 * Return the maximum value of two values
 * 
 * @param   a  One of the values
 * @param   b  The other one of the values
 * @return     The maximum value
 */
#define max(a, b)  \
  (a < b ? b : a)


/**
 * Return the minimum value of two values
 * 
 * @param   a  One of the values
 * @param   b  The other one of the values
 * @return     The minimum value
 */
#define min(a, b)  \
  (a < b ? a : b)


/**
 * Cast a buffer to another type and get the slot for an element
 * 
 * @param   buffer:char*  The buffer
 * @param   type          The data type of the elements for the data type to cast the buffer to
 * @param   index:size_t  The index of the element to address
 * @return  [type]        A slot that can be set or get
 */
#define buf_cast(buffer, type, index)  \
  (((type*)(buffer))[index])


/**
 * Set the value of an element a buffer that is being cast
 * 
 * @param   buffer:char*   The buffer
 * @param   type           The data type of the elements for the data type to cast the buffer to
 * @param   index:size_t   The index of the element to address
 * @param   variable:type  The new value of the element
 * @return  variable:      The new value of the element
 */
#define buf_set(buffer, type, index, variable)	\
  (((type*)(buffer))[index] = (variable))


/**
 * Get the value of an element a buffer that is being cast
 * 
 * @param   buffer:const char*  The buffer
 * @param   type                The data type of the elements for the data type to cast the buffer to
 * @param   index:size_t        The index of the element to address
 * @param   variable:type       Slot to set with the value of the element
 * @return  variable:           The value of the element
 */
#define buf_get(buffer, type, index, variable)	\
  (variable = ((const type*)(buffer))[index])


/**
 * Increase the pointer of a buffer
 * 
 * @param   buffer:char*  The buffer
 * @param   type          A data type
 * @param   count:size_t  The number elements of the data type `type` to increase the pointer with
 * @return  buffer:       The buffer
 */
#define buf_next(buffer, type, count)  \
  (buffer += (count) * sizeof(type) / sizeof(char))


/**
 * Decrease the pointer of a buffer
 * 
 * @param   buffer:char*  The buffer
 * @param   type          A data type
 * @param   count:size_t  The number elements of the data type `type` to decrease the pointer with
 * @return  buffer:       The buffer
 */
#define buf_prev(buffer, type, count)  \
  (buffer -= (count) * sizeof(type) / sizeof(char))


/**
 * This macro combines `buf_set` with `buf_next`, it sets
 * element zero and increase the pointer by one element
 * 
 * @param   buffer:char*   The buffer
 * @param   type           The data type of the elements for the data type to cast the buffer to
 * @param   variable:type  The new value of the element
 * @return  variable:      The new value of the element
 */
#define buf_set_next(buffer, type, variable)  \
  (buf_set(buffer, type, 0, variable),        \
   buf_next(buffer, type, 1))


/**
 * This macro combines `buf_set` with `buf_next`, it sets
 * element zero and increase the pointer by one element
 * 
 * @param   buffer:char*   The buffer
 * @param   type           The data type of the elements for the data type to cast the buffer to
 * @param   variable:type  Slot to set with the value of the element
 * @return  variable:      The value of the element
 */
#define buf_get_next(buffer, type, variable)  \
  (buf_get(buffer, type, 0, variable),        \
   buf_next(buffer, type, 1))


/**
 * Check whether two strings are equal
 * 
 * @param   a:const char*  One of the strings
 * @param   b:const char*  The other of the strings
 * @return  :int           Whether the strings are equal
 */
#define strequals(a, b)  \
  (strcmp(a, b) == 0)


/**
 * Check whether a string starts with another string
 * 
 * @param   haystack:const char*  The string to inspect
 * @param   needle:const char*    The string `haystack` should start with
 * @return  :int                  Whether `haystack` starts with `needle`
 */
#define startswith(haystack, needle)  \
  (strstr(haystack, needle) == haystack)


/**
 * Set effective user and the effective group to the
 * real user and the real group, respectively. If the
 * group cannot be set, the user till not be set either.
 * 
 * @return  :int  Non-zero on error
 */
#define drop_privileges()                              \
  ((getegid() == getgid() ? 0 : setegid(getgid())) ||  \
   (geteuid() == getuid() ? 0 : seteuid(getuid())))


/**
 * Wrapper for `clock_gettime` that gets some kind of
 * monotonic time, the exact clock ID is not specified
 * 
 * @param   time_slot:struct timespec*  Pointer to the variable in which to store the time
 * @return  :int                        Zero on success, -1 on error
 */
#define monotone(time_slot)  \
  clock_gettime(CLOCK_MONOTONIC_RAW, time_slot)


/**
 * Wrapper for `close` that will retry if it gets
 * interrupted
 * 
 * @param  fd:int  The file descriptor
 */
#if 1 /* For kernels that ensure that close(2) always closes valid file descriptors. */
# define xclose(fd)  \
  close(fd)
#else /* For kernels that ensure that close(2) never closes valid file descriptors on interruption. */
# ifdef MDS_LIBMDSSERVER_MACROS_DEFINED_TEMP_FAILURE_RETRY
#  define xclose(fd)  \
  TEMP_FAILURE_RETRY(close(fd))
# else
#  define xclose(fd)  \
  (TEMP_FAILURE_RETRY(close(fd)) < 0 ? 0 : (errno = 0))
# endif
#endif


/**
 * Wrapper for `fclose` that will retry if it gets
 * interrupted
 * 
 * @param  f:FILE*  The stream
 */
#if 1 /* For kernels that ensure that close(2) always closes valid file descriptors. */
# define xfclose(f)  \
  fclose(f)
#else /* For kernels that ensure that close(2) never closes valid file descriptors on interruption. */
# ifdef MDS_LIBMDSSERVER_MACROS_DEFINED_TEMP_FAILURE_RETRY
#  define xfclose(f)  \
  TEMP_FAILURE_RETRY(fclose(f))
# else
#  define xfclose(f)  \
  (TEMP_FAILURE_RETRY(fclose(f)) < 0 ? 0 : (errno = 0))
# endif
#endif


/**
 * Close all file descriptors that satisfies a condition
 * 
 * @param  condition  The condition, it should evaluate the variable `fd`
 */
#define close_files(condition)									\
  do												\
    {												\
      DIR* dir = opendir(SELF_FD);								\
      struct dirent* file;									\
												\
      if (dir == NULL)										\
	perror(*argv); /* Well, that is just unfortunate, but we cannot really do anything. */	\
      else											\
	while ((file = readdir(dir)) != NULL)							\
	  if (strcmp(file->d_name, ".") && strcmp(file->d_name, ".."))				\
	    {											\
	      int fd = atoi(file->d_name);							\
	      if (condition)									\
		xclose(fd);									\
	    }											\
												\
      closedir(dir);										\
    }												\
  while (0)


/**
 * Free an array and all elements in an array
 * 
 * @param  array:void**     The array to free
 * @param  elements:size_t  The number of elements, in the array, to free
 * @scope  i:size_t         The variable `i` must be declared as `size_t` and avaiable for use
 */
#define xfree(array, elements)		\
  do					\
    {					\
      for (i = 0; i < (elements); i++)	\
	free((array)[i]);		\
      free(array), (array) = NULL;	\
    }					\
  while (0)


/**
 * `malloc` wrapper that returns whether the allocation was not successful
 *  
 * @param   var:type*        The variable to which to assign the allocation
 * @param   elements:size_t  The number of elements to allocate
 * @param   type             The data type of the elements for which to create an allocation
 * @return  :int             Evaluates to true if an only if the allocation failed
 */
#define xmalloc(var, elements, type)  \
  ((var = malloc((elements) * sizeof(type))) == NULL)
/*
#define xmalloc(var, elements, type)					\
  ({									\
    size_t _x_elements = (elements);					\
    size_t _x_size = _x_elements * sizeof(type);			\
    fprintf(stderr, "xmalloc(%s, %zu, %s)(=%zu) @ %s:%i\n",		\
	    #var, _x_elements, #type, _x_size, __FILE__, __LINE__);	\
    ((var = malloc(_x_size)) == NULL);					\
  })
*/


/**
 * `malloc` wrapper that returns whether the allocation was not successful
 *  
 * @param   var:type*     The variable to which to assign the allocation
 * @param   bytes:size_t  The number of bytes to allocate
 * @return  :int          Evaluates to true if an only if the allocation failed
 */
#define xbmalloc(var, bytes)  \
  ((var = malloc(bytes)) == NULL)
/*
#define xbmalloc(var, bytes)					\
  ({								\
    size_t _x_bytes = (bytes);					\
    fprintf(stderr, "xbmalloc(%s, %zu) @ %s:%i\n",		\
	    #var, _x_bytes, __FILE__, __LINE__);		\
    ((var = malloc(_x_bytes)) == NULL);				\
  })
*/


/**
 * `calloc` wrapper that returns whether the allocation was not successful
 *  
 * @param   var:type*        The variable to which to assign the allocation
 * @param   elements:size_t  The number of elements to allocate
 * @param   type             The data type of the elements for which to create an allocation
 * @return  :int             Evaluates to true if an only if the allocation failed
 */
#define xcalloc(var, elements, type)  \
  ((var = calloc(elements, sizeof(type))) == NULL)
/*
#define xcalloc(var, elements, type)					\
  ({									\
    size_t _x_elements = (elements);					\
    size_t _x_size = _x_elements * sizeof(type);			\
    fprintf(stderr, "xcalloc(%s, %zu, %s)(=%zu) @ %s:%i\n",		\
	    #var, _x_elements, #type, _x_size, __FILE__, __LINE__);	\
    ((var = calloc(_x_elements, sizeof(type))) == NULL);		\
  })
*/


/**
 * `calloc` wrapper that returns whether the allocation was not successful
 *  
 * @param   var:type*     The variable to which to assign the allocation
 * @param   bytes:size_t  The number of bytes to allocate
 * @return  :int          Evaluates to true if an only if the allocation failed
 */
#define xbcalloc(var, bytes)  \
  ((var = calloc(bytes, sizeof(char))) == NULL)
/*
#define xbcalloc(var, bytes)					\
  ({								\
    size_t _x_bytes = (bytes);					\
    fprintf(stderr, "xbcalloc(%s, %zu) @ %s:%i\n",		\
	    #var, _x_bytes, __FILE__, __LINE__);		\
    ((var = calloc(_x_bytes, sizeof(char))) == NULL);		\
  })
*/


/**
 * `realloc` wrapper that returns whether the allocation was not successful
 *  
 * @param   var:type*        The variable to which to assign the reallocation
 * @param   elements:size_t  The number of elements to allocate
 * @param   type             The data type of the elements for which to create an allocation
 * @return  :int             Evaluates to true if an only if the allocation failed
 */
#define xrealloc(var, elements, type)  \
  ((var = realloc(var, (elements) * sizeof(type))) == NULL)
/*
#define xrealloc(var, elements, type)					\
  ({									\
    size_t _x_elements = (elements);					\
    size_t _x_size = _x_elements * sizeof(type);			\
    fprintf(stderr, "xrealloc(%s, %zu, %s)(=%zu) @ %s:%i\n",		\
	    #var, _x_elements, #type, _x_size, __FILE__, __LINE__);	\
    ((var = realloc(var, _x_size)) == NULL);				\
  })
*/


/**
 * `xrealloc` that stores the old variable
 *  
 * @param   old:type*        The variable to which to store with the old variable that needs
 *                           to be `free`:ed on failure, and set to `NULL` on success.
 * @param   var:type*        The variable to which to assign the reallocation
 * @param   elements:size_t  The number of elements to allocate
 * @param   type             The data type of the elements for which to create an allocation
 * @return  :int             Evaluates to true if an only if the allocation failed
 */
#define xxrealloc(old, var, elements, type)  \
  (old = var, (((var = realloc(var, (elements) * sizeof(type))) == NULL) ? 1 : (old = NULL, 0)))
/*
#define xxrealloc(old, var, elements, type)						\
  ({											\
    size_t _x_elements = (elements);							\
    size_t _x_size = _x_elements * sizeof(type);					\
    fprintf(stderr, "xxrealloc(%s, %s, %zu, %s)(=%zu) @ %s:%i\n",			\
	    #old, #var, _x_elements, #type, _x_size, __FILE__, __LINE__);		\
    (old = var, (((var = realloc(var, _x_size)) == NULL) ? 1 : (old = NULL, 0)));	\
  })
*/


/**
 * `xrealloc` that restores the variable on failure
 *  
 * @param   tmp:type*        The variable to which to store with the old variable temporarily
 * @param   var:type*        The variable to which to assign the reallocation
 * @param   elements:size_t  The number of elements to allocate
 * @param   type             The data type of the elements for which to create an allocation
 * @return  :int             Evaluates to true if an only if the allocation failed
 */
#define yrealloc(tmp, var, elements, type)                               \
  ((tmp = var, (var = realloc(var, (elements) * sizeof(type))) == NULL)  \
   ? (var = tmp, tmp = NULL, 1) : (tmp = NULL, 0))
/*
#define yrealloc(tmp, var, elements, type)					\
  ({										\
    size_t _x_elements = (elements);						\
    size_t _x_size = _x_elements * sizeof(type);				\
    fprintf(stderr, "yrealloc(%s, %s, %zu, %s)(=%zu) @ %s:%i\n",		\
	    #tmp, #var, _x_elements, #type, _x_size, __FILE__, __LINE__);	\
    ((tmp = var, (var = realloc(var, _x_size)) == NULL)				\
     ? (var = tmp, tmp = NULL, 1) : (tmp = NULL, 0));				\
  })
*/


/**
 * Double to the size of an allocation on the heap
 * 
 * @param   old:type*        Variable in which to store the old value temporarily
 * @param   var:type*        The variable to which to assign the reallocation
 * @param   elements:size_t  The number of elements to allocate
 * @param   type             The data type of the elements for which to create an allocation
 * @return  :int             Evaluates to true if an only if the allocation failed
 */
#define growalloc(old, var, elements, type)  \
  (old = var, xrealloc(var, (elements) <<= 1, type) ? (var = old, (elements) >>= 1, 1) : 0)
/*
#define growalloc(old, var, elements, type)							\
  ({												\
    size_t _x_elements_ = (elements);								\
    size_t _x_size_ = _x_elements_ * sizeof(type);						\
    fprintf(stderr, "growalloc(%s, %s, %zu, %s)(=%zu)\n-->  ",					\
	    #old, #var, _x_elements_, #type, _x_size_, __FILE__, __LINE__); 			\
    (old = var, xrealloc(var, (elements) <<= 1, type) ? (var = old, (elements) >>= 1, 1) : 0);	\
  })
*/


/**
 * `strdup` wrapper that returns whether the allocation was not successful
 *  
 * @param   var:char*             The variable to which to assign the duplicate
 * @param   original:const char*  The string to duplicate
 * @return  :int                  Evaluates to true if an only if the allocation failed
 */
#define xstrdup(var, original)  \
  (original ? ((var = strdup(original)) == NULL) : (var = NULL, 0))
/*
#define xstrdup(var, original)									\
  ({												\
    size_t _x_size = original ? strlen(original) : 0;						\
    fprintf(stderr, "xstrdup(%s, %s(“%s”=%zu))(=%zu) @ %s:%i\n",				\
	    #var, #original, original, _x_size, _x_size + !!_x_size, __FILE__, __LINE__); 	\
    (original ? ((var = strdup(original)) == NULL) : (var = NULL, 0));				\
  })
*/


/**
 * `malloc` and `memcpy` wrapper that creates a duplicate of a pointer and
 * returns whether the allocation was not successful
 *  
 * @param   var:void*             The variable to which to assign the duplicate
 * @param   original:const void*  The buffer to duplicate
 * @param   elements:size_t       The number of elements to duplicate
 * @param   type                  The data type of the elements to duplicate
 * @return  :int                  Evaluates to true if an only if the allocation failed
 */
#define xmemdup(var, original, elements, type)	              \
  (((var = malloc((elements) * sizeof(type))) == NULL) ? 1 :  \
   (memcpy(var, original, (elements) * sizeof(type)), 0))
/*
#define xmemdup(var, original, elements, type)						\
  ({											\
    size_t _x_elements = (elements);							\
    size_t _x_size = _x_elements * sizeof(type);					\
    fprintf(stderr, "xmemdup(%s, %s, %zu, %s)(=%zu) @ %s:%i\n",				\
	    #var, #original, _x_elements, #type, _x_size, __FILE__, __LINE__);		\
    (((var = malloc(_x_size)) == NULL) ? 1 : (memcpy(var, original, _x_size), 0));	\
  })
*/



/**
 * Call `perror` if `errno` is non-zero and set `errno` to zero
 * 
 * @param  str:const char*  The argument passed to `perror`
 */
#define xperror(str)  \
  (errno ? perror(str), errno = 0 : 0)


/**
 * Go to the label `fail` if a condition is met
 * 
 * @param  ...  The condition
 */
#define fail_if(...)								\
  do										\
    if (__VA_ARGS__)								\
      {										\
	int _fail_if_saved_errno = errno;					\
	if ((errno != EMSGSIZE) && (errno != ECONNRESET) && (errno != EINTR))	\
	  fprintf(stderr, "failure at %s:%i\n", __FILE__, __LINE__);		\
	errno = _fail_if_saved_errno;						\
	goto fail;					       			\
      }										\
  while (0)


/**
 * Run a set of instructions and return 1 if a condition is met
 * 
 * @param  condition     The condition
 * @param  instructions  The instruction (semicolon-terminated)
 */
#define exit_if(condition, instructions)  \
  do { if (condition) { instructions return 1; } } while (0)


/**
 * The way to get a pointer to the end of a string
 * 
 * `strchr(str, '\0')` is faster than `str + strlen(str)`,
 * at least in the GNU C Library. If this is not true for
 * the compiler you are using, you may want to edit this
 * macro.
 */
#define STREND(str)  \
  (strchr(str, '\0'))


/**
 * The system is running out of memory.
 * Quick, free up all your unused memory or kill yourself!
 */
#ifndef SIGDANGER
# define SIGDANGER  (SIGRTMIN + 1)
#endif


/**
 * The user want the server to dump information
 * about the server's state or statistics
 */
#ifndef SIGINFO
# define SIGINFO  (SIGRTMIN + 2)
#endif


/**
 * The user wants the program to re-exec.
 * into an updated binary
 */
#ifndef SIGUPDATE
# define SIGUPDATE  SIGUSR1
#endif


/**
 * Normal signal handlers should place this macro
 * at the top of the function
 */
#define SIGHANDLER_START  \
  int sighandler_saved_errno = errno


/**
 * Normal signal handlers should place this macro
 * at the bottom of the function, or just before
 * any `return`
 */
#define SIGHANDLER_END  \
  (errno = sighandler_saved_errno)


#endif

