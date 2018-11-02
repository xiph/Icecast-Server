/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2018,      Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

/*
 * This file contains the API for a refobject based buffer object.
 * It can be used to store data and allows on the fly re-allocation.
 */

#ifndef __BUFFER_H__
#define __BUFFER_H__

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdarg.h>

#include "icecasttypes.h"
#include "compat.h"
#include <igloo/ro.h>

/* About thread safety:
 * This set of functions is intentinally not thread safe.
 */

igloo_RO_FORWARD_TYPE(buffer_t);

/* This creates a new buffer object.
 * Parameters:
 *  preallocation
 *      The number of bytes to allocate for use later on. See buffer_preallocate() for details.
 *  userdata, name, associated
 *      See refobject_new().
 */
buffer_t *  buffer_new(ssize_t preallocation, const char *name, igloo_ro_t associated);

/* Depreciated: This creates a new buffer with defaults.
 * Do NOT use this. Use refobject_new(buffer_t)
 *
 * This is the same as:
 *  buffer_new(-1, NULL, NULL, REFOBJECT_NULL)
 */
buffer_t *  buffer_new_simple(void);

/* This function preallocates space for later use.
 * Parameters:
 *  buffer
 *      The buffer to operate on.
 *  request
 *      Number of bytes to additionally allocate.
 * Notes:
 *  This function is very usedful when adding a large number of smaller buffers to avoid
 *  internal reallocation calls happening to often. However it is not required to call
 *  this function before adding data to the buffer.
 */
void        buffer_preallocate(buffer_t *buffer, size_t request);

/* Gets data and length of the buffer.
 * Parameters:
 *  buffer
 *      The buffer to operate on.
 *  data
 *      Pointer to the stored data. If NULL the pointer is not returned.
 *  length
 *      Pointer to the length of how many bytes are in the buffer. If NULL
 *      length is not returned.
 */
int         buffer_get_data(buffer_t *buffer, const void **data, size_t *length);

/* Gets data as a string. The string is '\0'-terminated.
 * Parameters:
 *  buffery
 *      The buffer to operate on.
 *  string
 *      The string representing the data hold by the buffer.
 */
int         buffer_get_string(buffer_t *buffer, const char **string);

/* Sets the length of the buffer.
 * Parameters:
 *  buffer
 *      The buffer to operate on.
 *  length
 *      New length of the buffer.
 * Notes:
 *  This can only be used to reduce the size of the buffer. To add data to
 *  the buffer use buffer_push_*().
 *
 *  Calling this with length set to 0 clears the buffer but does not deallocate it.
 */
int         buffer_set_length(buffer_t *buffer, size_t length);

/* Shifts data out of the buffer.
 * Parameters:
 *  buffer
 *      The buffer to operate on.
 *  amount
 *      The amount of bytes to be removed from the begin of the buffer.
 * Notes:
 *  This function can be useful for skipping some small header. However this
 *  must not be used to implement a kind of ring buffer as it will result in
 *  poor performance caused by massive reallocations and memory copies.
 */
int         buffer_shift(buffer_t *buffer, size_t amount);

/* This pushes data to the end of the buffer.
 * Parameters:
 *  buffer
 *      The buffer to operate on.
 *  data
 *      The data to push.
 *  length
 *      The length of the data to push in byte.
 * Notes:
 *  Consider using buffer_zerocopy_*().
 */
int         buffer_push_data(buffer_t *buffer, const void *data, size_t length);

/* This pushes a string to the end of the buffer.
 * Parameters:
 *  buffer
 *      The buffer to operate on.
 *  string
 *      The string to be pushed. The tailing '\0'-termination will not be
 *      part of the buffer.
 * Notes:
 *  Consider using buffer_zerocopy_*().
 */
int         buffer_push_string(buffer_t *buffer, const char *string);

/* This pushes a formated string to the end of the buffer.
 * Parameters:
 *  buffer
 *      The buffer to operate on.
 *  format
 *      The format string as for printf() family functions.
 *  ...
 *      The parameters according to the format string.
 */
int         buffer_push_printf(buffer_t *buffer, const char *format, ...);

/* This pushes a formated string to the end of the buffer using a va_list.
 * Parameters:
 *  buffer
 *      The buffer to operate on.
 *  format
 *      The format string as for printf() family functions.
 *  ap
 *      The parameters according to the format string as va_list.
 * See also:
 *  vprintf(3).
 */
int         buffer_push_vprintf(buffer_t *buffer, const char *format, va_list ap);

/* This pushes the content of another buffer to the end of the buffer.
 * Parameters:
 *  buffer
 *      The buffer to operate on.
 *  source
 *      The buffer which's content is to be copied.
 */
int         buffer_push_buffer(buffer_t *buffer, buffer_t *source);

/* This requests for a memory buffer that can be pushed to without the need for copy.
 * Parameters:
 *  buffer
 *      The buffer to operate on.
 *  data
 *      Pointer to memory that can be written and will become part of the buffer object.
 *  request
 *      Size of the memory area that is returned by data in bytes.
 * Notes:
 *  This is the first step of the zero copy push. After the memory returned by data has been
 *  written (e.g. used in a call to read(2)) buffer_zerocopy_push_complete() must be called.
 */
int         buffer_zerocopy_push_request(buffer_t *buffer, void **data, size_t request);

/* This is the final step of a zero copy push.
 * Parameters:
 *  buffer
 *      The buffer to operate on.
 *  done
 *      Amount of data in bytes that has actually been written into the memory area.
 *      May be zero to what has been requested with request.
 */
int         buffer_zerocopy_push_complete(buffer_t *buffer, size_t done);

#endif
