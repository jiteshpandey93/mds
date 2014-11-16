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
#ifndef MDS_MDS_KBDC_RAW_DATA_H
#define MDS_MDS_KBDC_RAW_DATA_H


#include <stddef.h>


/**
 * Source code by lines, with and without comments
 */
typedef struct source_code
{
  /**
   * Source code by lines without comments,
   * `NULL`-terminated.
   */
  char** restrict lines;
  
  /**
   * Source code by lines with comments,
   * `NULL`-terminated.
   */
  char** restrict real_lines;
  
  /**
   * Data for `lines` (internal data)
   */
  char* content;
  
  /**
   * Data for `real_lines` (internal data)
   */
  char* real_content;
  
  /**
   * The number of lines, that is, the number of
   * elements in `lines` and `real_lines`.
   */
  size_t line_count;
  
} source_code_t;


/**
 * Initialise a `source_code_t*`
 * 
 * @param  this  The `source_code_t*`
 */
void source_code_initialise(source_code_t* restrict this);

/**
 * Release all data in a `source_code_t*`
 * 
 * @param  this  The `source_code_t*`
 */
void source_code_destroy(source_code_t* restrict this);

/**
 * Release all data in a `source_code_t*`, and free it
 * 
 * @param  this  The `source_code_t*`
 */
void source_code_free(source_code_t* restrict this);


/**
 * Find the end of a function call
 * 
 * @param   content  The code
 * @param   offset   The index after the first character after the backslash
 *                   that triggered this call
 * @param   size     The length of `code`
 * @return           The index of the character after the bracket that closes
 *                   the function call (may be outside the code by one character),
 *                   or `size` if the call do not end (that is, the code ends
 *                   prematurely), or zero if there is no function call at `offset`
 */
size_t get_end_of_call(char* restrict content, size_t offset, size_t size) __attribute__((pure));

/**
 * Read lines of a source file
 * 
 * @param   pathname     The pathname of the source file
 * @param   source_code  Output parameter for read data
 * @return               Zero on success, -1 on error
 */
int read_source_lines(const char* restrict pathname, source_code_t* restrict source_code);


#endif
