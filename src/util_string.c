/* Icecast
 *
 *  Copyright 2000-2004 Jack Moffitt <jack@xiph.org,
 *                      Michael Smith <msmith@xiph.org>,
 *                      oddsock <oddsock@xiph.org>,
 *                      Karl Heyes <karl@xiph.org>
 *                      and others (see AUTHORS for details).
 *  Copyright 2012-2022 Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>
 *
 *  This program is distributed under the GNU General Public License, version 2.
 *  A copy of this license is included with this source.
 *
 */


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <string.h>

#include <ctype.h>

#include "util_string.h"

static const char hexchars[16] = {
    '0','1','2','3','4','5','6','7','8','9','a','b','c','d','e','f'
};

static const char safechars[256] = {
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  0,  0,  0,  0,  0,  0,
    0,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
    1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  0,  0,  0,  0,  0,
    0,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
    1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
};

static const char base64table[64] = {
    'A','B','C','D','E','F','G','H','I','J','K','L','M','N','O','P',
    'Q','R','S','T','U','V','W','X','Y','Z','a','b','c','d','e','f',
    'g','h','i','j','k','l','m','n','o','p','q','r','s','t','u','v',
    'w','x','y','z','0','1','2','3','4','5','6','7','8','9','+','/'
};

static const signed char base64decode[256] = {
     -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2,
     -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2,
     -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, 62, -2, -2, -2, 63,
     52, 53, 54, 55, 56, 57, 58, 59, 60, 61, -2, -2, -2, -1, -2, -2,
     -2,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
     15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, -2, -2, -2, -2, -2,
     -2, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
     41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, -2, -2, -2, -2, -2,
     -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2,
     -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2,
     -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2,
     -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2,
     -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2,
     -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2,
     -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2,
     -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2
};

static inline int hex(char c)
{
    if(c >= '0' && c <= '9')
        return c - '0';
    else if(c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    else if(c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    else
        return -1;
}

char *util_bin_to_hex(unsigned char *data, int len)
{
    char *hexstr = malloc(len*2 + 1);
    int i;

    for (i = 0; i < len; i++) {
        hexstr[i*2] = hexchars[(data[i]&0xf0) >> 4];
        hexstr[i*2+1] = hexchars[data[i]&0x0f];
    }

    hexstr[len*2] = 0;

    return hexstr;
}

/* This isn't efficient, but it doesn't need to be */
char *util_base64_encode(const char *data, size_t len) {
    char *out = malloc(len*4/3 + 4);
    char *result = out;
    size_t chunk;

    while(len > 0) {
        chunk = (len > 3) ? 3 : len;
        *out++ = base64table[(*data & 0xFC)>>2];

        switch(chunk) {
            case 3:
                *out++ = base64table[((*data & 0x03)<<4) | ((*(data+1) & 0xF0) >> 4)];
                *out++ = base64table[((*(data+1) & 0x0F)<<2) | ((*(data+2) & 0xC0)>>6)];
                *out++ = base64table[(*(data+2)) & 0x3F];
            break;
            case 2:
                *out++ = base64table[((*data & 0x03)<<4) | ((*(data+1) & 0xF0) >> 4)];
                *out++ = base64table[((*(data+1) & 0x0F)<<2)];
                *out++ = '=';
            break;
            case 1:
                *out++ = base64table[((*data & 0x03)<<4)];
                *out++ = '=';
                *out++ = '=';
            break;
        }
        data += chunk;
        len -= chunk;
    }
    *out = 0;

    return result;
}

char *util_base64_decode(const char *data)
{
    const unsigned char *input = (const unsigned char *)data;
    int len = strlen (data);
    char *out = malloc(len*3/4 + 5);
    char *result = out;
    signed char vals[4];

    while(len > 0) {
        if(len < 4)
        {
            free(result);
            return NULL; /* Invalid Base64 data */
        }

        vals[0] = base64decode[*input++];
        vals[1] = base64decode[*input++];
        vals[2] = base64decode[*input++];
        vals[3] = base64decode[*input++];

        if(vals[0] < 0 || vals[1] < 0 || vals[2] < -1 || vals[3] < -1) {
            len -= 4;
            continue;
        }

        *out++ = vals[0]<<2 | vals[1]>>4;
        /* vals[3] and (if that is) vals[2] can be '=' as padding, which is
           looked up in the base64decode table as '-1'. Check for this case,
           and output zero-terminators instead of characters if we've got
           padding. */
        if(vals[2] >= 0)
            *out++ = ((vals[1]&0x0F)<<4) | (vals[2]>>2);
        else
            *out++ = 0;

        if(vals[3] >= 0)
            *out++ = ((vals[2]&0x03)<<6) | (vals[3]);
        else
            *out++ = 0;

        len -= 4;
    }
    *out = 0;

    return result;
}

char *util_url_escape (const char *src)
{
    size_t len;
    char *dst;
    unsigned char *source = (unsigned char *)src;
    size_t i, j;

    if (!src)
        return NULL;

    len = strlen(src);
    /* Efficiency not a big concern here, keep the code simple/conservative */
    dst = calloc(1, len*3 + 1);

    for(i = 0, j = 0; i < len; i++) {
        if(safechars[source[i]]) {
            dst[j++] = source[i];
        } else {
            dst[j++] = '%';
            dst[j++] = hexchars[(source[i] >> 4) & 0x0F];
            dst[j++] = hexchars[ source[i]       & 0x0F];
        }
    }

    dst[j] = 0;
    return dst;
}

char *util_url_unescape (const char *src)
{
    int len = strlen(src);
    char *decoded;
    int i;
    char *dst;
    int done = 0;

    decoded = calloc(1, len + 1);

    dst = decoded;

    for(i=0; i < len; i++) {
        switch(src[i]) {
            case '%':
                if(i+2 >= len) {
                    free(decoded);
                    return NULL;
                }
                if(hex(src[i+1]) == -1 || hex(src[i+2]) == -1 ) {
                    free(decoded);
                    return NULL;
                }

                *dst++ = hex(src[i+1]) * 16  + hex(src[i+2]);
                i+= 2;
                break;
            case '#':
                done = 1;
                break;
            case 0:
                free(decoded);
                return NULL;
                break;
            default:
                *dst++ = src[i];
                break;
        }
        if(done)
            break;
    }

    *dst = 0; /* null terminator */

    return decoded;
}

int util_replace_string(char **dst, const char *src)
{
    char *n;

    if (!dst)
        return -1;

    if (src) {
        n = strdup(src);
        if (!n)
            return -1;
    } else {
        n = NULL;
    }

    free(*dst);
    *dst = n;

    return 0;
}

bool util_replace_string_url_escape(char **dst, const char *src)
{
    char *n;

    if (!dst)
        return false;

    if (src) {
        n = util_url_escape(src);
        if (!n)
            return false;
    } else {
        n = NULL;
    }

    free(*dst);
    *dst = n;

    return true;
}

int util_strtolower(char *str)
{
    if (!str)
        return -1;

    for (; *str; str++)
        *str = tolower(*str);

    return 0;
}

bool util_is_in_list(const char *list, const char *needle)
{
    while (*list) {
        const char *t = needle;
        bool positive = true;

        for (; *list == ','; list++);

        for (; *list == '!'; list++)
            positive = !positive;

        for (; *list; list++) {
            if (*list == ',')
                break;
            if (*list == '*')
                return positive;

            if (*list == *t) {
                t++;
            } else {
                break;
            }
        }
        if ((*list == 0 || *list == ',') && *t == 0) {
            return positive;
        }

        for (; *list && *list != ','; list++);
    }

    return false;
}
