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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

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

REFOBJECT_DEFINE_TYPE(buffer_t,
        REFOBJECT_DEFINE_TYPE_FREE(__free)
        );

buffer_t *  buffer_new(ssize_t preallocation, void *userdata, const char *name, refobject_t associated)
{
    buffer_t *buffer = refobject_new__new(buffer_t, userdata, name, associated);

    if (!buffer)
        return NULL;

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

    if (!buffer)
        return;

    /* Remove the offset if it makes sense to do so. */
    if (buffer->offset == buffer->fill) {
        buffer->offset = 0;
        buffer->fill = 0;
    } else if ((2*buffer->offset) < buffer->fill || buffer->offset >= 512 || (buffer->offset > 128 && buffer->offset >= request)) {
        buffer->fill -= buffer->offset;
        memmove(buffer->buffer, buffer->buffer + buffer->offset, buffer->fill);
        buffer->offset = 0;
    }

    if (!request)
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
    *string = ret + buffer->offset;

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

    /* run cleanup */
    buffer_preallocate(buffer, 0);

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


int         buffer_push_printf(buffer_t *buffer, const char *format, ...)
{
    int ret;
    va_list ap;

    if (!buffer || !format)
        return -1;

    if (!*format)
        return 0;

    va_start(ap, format);
    ret = buffer_push_vprintf(buffer, format, ap);
    va_end(ap);

    return ret;
}

int         buffer_push_vprintf(buffer_t *buffer, const char *format, va_list ap)
{
    void *buf;
    int ret;
    size_t length = 1024;

    if (!buffer || !format)
        return -1;

    if (!*format)
        return 0;

    ret = buffer_zerocopy_push_request(buffer, &buf, length);
    if (ret != 0)
        return ret;

    ret = vsnprintf(buf, length, format, ap);
    if (ret >= 0 && (size_t)ret < length) {
        return buffer_zerocopy_push_complete(buffer, ret);
    } else if (ret < 0) {
        /* This vsnprintf() likely does not follow POSIX.
         * We don't know what length we need to asume. So asume a big one and hope for the best. */
        length = 8192;
    } else {
        /* Reallocate the buffer to the size reported plus one for '\0'-termination */
        length = ret + 1;
    }

    /* We have not written any data yet. */
    ret = buffer_zerocopy_push_complete(buffer, 0);
    if (ret != 0)
        return ret;

    /* Now let's try again. */
    ret = buffer_zerocopy_push_request(buffer, &buf, length);
    if (ret != 0)
        return ret;

    ret = vsnprintf(buf, length, format, ap);
    if (ret < 0 || (size_t)ret >= length) {
        /* This still didn't work. Giving up. */
        buffer_zerocopy_push_complete(buffer, 0);
        return -1;
    }

    return buffer_zerocopy_push_complete(buffer, ret);
}

int         buffer_push_buffer(buffer_t *buffer, buffer_t *source)
{
    const void *data;
    size_t length;
    int ret;

    if (!buffer || !source)
        return -1;

    ret = buffer_get_data(source, &data, &length);
    if (ret != 0)
        return ret;

    return buffer_push_data(buffer, data, length);
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
