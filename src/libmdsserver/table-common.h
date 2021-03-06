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
#ifndef MDS_LIBMDSSERVER_TABLE_COMMON_H
#define MDS_LIBMDSSERVER_TABLE_COMMON_H


#include <stddef.h>


/**
 * A function that performs a comparison of two objects
 * 
 * @param   a  The first object
 * @param   b  The second object
 * @return     Whether the objects are equal
 */
typedef int compare_func(size_t a, size_t b);

/**
 * A function that hashes an object or a value
 * 
 * @param   value  The object or value
 * @return         The hash of `value`
 */
typedef size_t hash_func(size_t value);

/**
 * A function that releases an object's resources and `free`:s it
 * 
 * @param  obj  The object
 */
typedef void free_func(size_t obj);

/**
 * A function that translates a object into a new object
 * 
 * @param   obj  The object
 * @return       The new object
 */
typedef size_t remap_func(size_t obj);


#endif

