/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2018,      Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <string.h>

#include "buffer.h"
#include "refobject.h"

struct buffer_tag {
    refobject_base_t __base;
    /* Buffer itself */
    void *buffer;
    /* Length in bytes of buffer */
    size_t length;
    /* Amount of bytes in use of the buffer. This includes offset bytes */
    size_t fill;
    /* Bytes of offset at the start of the buffer */
    size_t offset;
};

static void __free(refobject_t self, void **userdata)
{
    buffer_t *buffer = REFOBJECT_TO_TYPE(self, buffer_t*);

    free(buffer->buffer);
}

buffer_t *  buffer_new(ssize_t preallocation, void *userdata, const char *name, refobject_t associated)
{
    buffer_t *buffer = NULL;
    refobject_t refobject = refobject_new(sizeof(*buffer), __free, userdata, name, associated);

    if (REFOBJECT_IS_NULL(refobject))
        return NULL;

    buffer = REFOBJECT_TO_TYPE(refobject, buffer_t *);

    if (preallocation > 0)
        buffer_preallocate(buffer, preallocation);

    return buffer;
}

buffer_t *  buffer_new_simple(void)
{
    return buffer_new(-1, NULL, NULL, REFOBJECT_NULL);
}

void        buffer_preallocate(buffer_t *buffer, size_t request)
{
    void *n;
    size_t newlen;

    /* TODO: use this function to clean up the effects of buffer->offset */

    if (!buffer || !request)
        return;

    newlen = buffer->fill + request;

    if (buffer->length >= newlen)
        return;

    /* Make sure we at least add 64 bytes and are 64 byte aligned */
    newlen = newlen + 64 - (newlen % 64);

    n = realloc(buffer->buffer, newlen);

    /* Just return if this failed */
    if (!n)
        return;

    buffer->buffer = n;
    buffer->length = newlen;
}

int         buffer_get_data(buffer_t *buffer, const void **data, size_t *length)
{
    if (!buffer)
        return -1;

    if (data) {
        *data = buffer->buffer + buffer->offset;
    }

    if (length) {
        *length = buffer->fill - buffer->offset;
    }

    return 0;
}

int         buffer_get_string(buffer_t *buffer, const char **string)
{
    char *ret;

    if (!buffer || !string)
        return -1;

    /* Ensure we have space for one additional byte ('\0'-termination). */
    if (buffer->length == buffer->fill) {
        buffer_preallocate(buffer, 1);
        if (buffer->length == buffer->fill)
            return -1;
    }

    /* Actually add a '\0'-termination. */
    ret = buffer->buffer;
    ret[buffer->fill] = 0;
    *string = ret;

    return 0;
}

int         buffer_set_length(buffer_t *buffer, size_t length)
{
    if (!buffer)
        return -1;

    if (length > (buffer->fill - buffer->offset))
        return -1;

    buffer->fill = length + buffer->offset;

    return 0;
}

int         buffer_shift(buffer_t *buffer, size_t amount)
{
    if (!buffer)
        return -1;

    if (amount > (buffer->fill - buffer->offset))
        return -1;

    buffer->offset += amount;

    return 0;
}

int         buffer_push_data(buffer_t *buffer, const void *data, size_t length)
{
    void *buf;
    int ret;

    if (!buffer)
        return -1;

    if (!length)
        return 0;

    if (!data)
        return -1;

    ret = buffer_zerocopy_push_request(buffer, &buf, length);
    if (ret != 0)
        return ret;

    memcpy(buf, data, length);

    ret = buffer_zerocopy_push_complete(buffer, length);

    return ret;
}

int         buffer_push_string(buffer_t *buffer, const char *string)
{
    if (!buffer || !string)
        return -1;

    return buffer_push_data(buffer, string, strlen(string));
}

int         buffer_zerocopy_push_request(buffer_t *buffer, void **data, size_t request)
{
    if (!buffer || !data)
        return -1;

    buffer_preallocate(buffer, request);

    if (request > (buffer->length - buffer->fill))
        return -1;

    *data = buffer->buffer + buffer->fill;

    return 0;
}

int         buffer_zerocopy_push_complete(buffer_t *buffer, size_t done)
{
    if (!buffer)
        return -1;

    if (done > (buffer->length - buffer->fill))
        return -1;

    buffer->fill += done;

    return 0;
}
