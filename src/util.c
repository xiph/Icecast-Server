/* Icecast
 *
 *  Copyright 2000-2004 Jack Moffitt <jack@xiph.org,
 *                      Michael Smith <msmith@xiph.org>,
 *                      oddsock <oddsock@xiph.org>,
 *                      Karl Heyes <karl@xiph.org>
 *                      and others (see AUTHORS for details).
 *  Copyright 2012-2018 Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>
 *
 *  This program is distributed under the GNU General Public License, version 2.
 *  A copy of this license is included with this source.
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <sys/types.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#ifdef HAVE_WINSOCK2_H
#include <winsock2.h>
#endif

#ifndef _WIN32
#include <sys/time.h>
#include <unistd.h>
#ifdef HAVE_POLL
#include <sys/poll.h>
#endif
#else
#include <windows.h>
#endif

#include "common/net/sock.h"
#include "common/thread/thread.h"

#include "cfgfile.h"
#include "compat.h"
#include "refbuf.h"
#include "connection.h"
#include "client.h"
#include "util.h"
#include "source.h"
#include "admin.h"
#include "cors.h"

#define CATMODULE "util"

#include "logging.h"

/* first all static tables, then the code */

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


/* Abstract out an interface to use either poll or select depending on which
 * is available (poll is preferred) to watch a single fd.
 *
 * timeout is in milliseconds.
 *
 * returns > 0 if activity on the fd occurs before the timeout.
 *           0 if no activity occurs
 *         < 0 for error.
 */
int util_timed_wait_for_fd(sock_t fd, int timeout)
{
#ifdef HAVE_POLL
    struct pollfd ufds;

    ufds.fd = fd;
    ufds.events = POLLIN;
    ufds.revents = 0;

    return poll(&ufds, 1, timeout);
#else
    fd_set rfds;
    struct timeval tv, *p=NULL;

    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);

    if(timeout >= 0) {
        tv.tv_sec = timeout/1000;
        tv.tv_usec = (timeout % 1000)*1000;
        p = &tv;
    }
    return select(fd+1, &rfds, NULL, NULL, p);
#endif
}

int util_read_header(sock_t sock, char *buff, unsigned long len, int entire)
{
    int read_bytes, ret;
    unsigned long pos;
    char c;
    ice_config_t *config;
    int header_timeout;

    config = config_get_config();
    header_timeout = config->header_timeout;
    config_release_config();

    read_bytes = 1;
    pos = 0;
    ret = 0;

    while ((read_bytes == 1) && (pos < (len - 1))) {
        read_bytes = 0;

        if (util_timed_wait_for_fd(sock, header_timeout*1000) > 0) {
            if ((read_bytes = recv(sock, &c, 1, 0))) {
                if (c != '\r') buff[pos++] = c;
                if (entire) {
                    if ((pos > 1) && (buff[pos - 1] == '\n' &&
                                      buff[pos - 2] == '\n')) {
                        ret = 1;
                        break;
                    }
                }
                else {
                    if ((pos > 1) && (buff[pos - 1] == '\n')) {
                        ret = 1;
                        break;
                    }
                }
            }
        } else {
            break;
        }
    }

    if (ret) buff[pos] = '\0';

    return ret;
}

char *util_get_extension(const char *path) {
    char *ext = strrchr(path, '.');

    if(ext == NULL)
        return "";
    else
        return ext+1;
}

int util_check_valid_extension(const char *uri) {
    const char    *p2;

    if (!uri)
        return UNKNOWN_CONTENT;

    p2 = strrchr(uri, '.');
    if (!p2)
        return UNKNOWN_CONTENT;
    p2++;

    if (strcmp(p2, "xsl") == 0 || strcmp(p2, "xslt") == 0) {
        return XSLT_CONTENT;
    } else if (strcmp(p2, "htm") == 0 || strcmp(p2, "html") == 0) {
        return HTML_CONTENT;
    }

    return UNKNOWN_CONTENT;
}

static int hex(char c)
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

static int verify_path(char *path) {
    int dir = 0, indotseq = 0;

    while(*path) {
        if(*path == '/' || *path == '\\') {
            if(indotseq)
                return 0;
            if(dir)
                return 0;
            dir = 1;
            path++;
            continue;
        }

        if(dir || indotseq) {
            if(*path == '.')
                indotseq = 1;
            else
                indotseq = 0;
        }

        dir = 0;
        path++;
    }

    return 1;
}

char *util_get_path_from_uri(char *uri) {
    char *path = util_normalise_uri(uri);
    char *fullpath;

    if(!path)
        return NULL;
    else {
        fullpath = util_get_path_from_normalised_uri(path);
        free(path);
        return fullpath;
    }
}

char *util_get_path_from_normalised_uri(const char *uri) {
    char *fullpath;
    char *webroot;
    ice_config_t *config = config_get_config();

    webroot = config->webroot_dir;

    fullpath = malloc(strlen(uri) + strlen(webroot) + 1);
    if (fullpath)
        sprintf (fullpath, "%s%s", webroot, uri);
    config_release_config();

    return fullpath;
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
                ICECAST_LOG_ERROR("Fatal internal logic error in util_url_unescape()");
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

/* Get an absolute path (from the webroot dir) from a URI. Return NULL if the
 * path contains 'disallowed' sequences like foo/../ (which could be used to
 * escape from the webroot) or if it cannot be URI-decoded.
 * Caller should free the path.
 */
char *util_normalise_uri(const char *uri) {
    char *path;
#ifdef _WIN32
    size_t len;
#endif

    if(uri[0] != '/')
        return NULL;

    path = util_url_unescape(uri);

    if(path == NULL) {
        ICECAST_LOG_WARN("Error decoding URI: %s\n", uri);
        return NULL;
    }

#ifdef _WIN32
    /* If we are on Windows, strip trailing dots, as Win API strips it anyway */
    for (len = strlen(path); len > 0 && path[len-1] == '.'; len--)
        path[len-1] = '\0';
#endif

    /* We now have a full URI-decoded path. Check it for allowability */
    if(verify_path(path))
        return path;
    else {
        ICECAST_LOG_WARN("Rejecting invalid path \"%s\"", path);
        free(path);
        return NULL;
    }
}

char *util_bin_to_hex(unsigned char *data, int len)
{
    char *hex = malloc(len*2 + 1);
    int i;

    for (i = 0; i < len; i++) {
        hex[i*2] = hexchars[(data[i]&0xf0) >> 4];
        hex[i*2+1] = hexchars[data[i]&0x0f];
    }

    hex[len*2] = 0;

    return hex;
}

/* This isn't efficient, but it doesn't need to be */
char *util_base64_encode(const char *data, size_t len) {
    char *out = malloc(len*4/3 + 4);
    char *result = out;
    size_t chunk;

    while(len > 0) {
        chunk = (len > 3) ? 3 : len;
        *out++ = base64table[(*data & 0xFC)>>2];
        *out++ = base64table[((*data & 0x03)<<4) | ((*(data+1) & 0xF0) >> 4)];

        switch(chunk) {
            case 3:
                *out++ = base64table[((*(data+1) & 0x0F)<<2) | ((*(data+2) & 0xC0)>>6)];
                *out++ = base64table[(*(data+2)) & 0x3F];
            break;
            case 2:
                *out++ = base64table[((*(data+1) & 0x0F)<<2)];
                *out++ = '=';
            break;
            case 1:
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

util_hostcheck_type util_hostcheck(const char *hostname) {
    const char * p;
    size_t colon_count;

    if (!hostname)
        return HOSTCHECK_ERROR;

    if (strcmp(hostname, "localhost") == 0 ||
        strcmp(hostname, "localhost.localdomain") == 0 ||
        strcmp(hostname, "localhost.localnet") == 0)
        return HOSTCHECK_IS_LOCALHOST;

    for (p = hostname; *p; p++)
        if (!( (*p >= '0' && *p <= '9') || *p == '.'))
            break;
    if (!*p)
        return HOSTCHECK_IS_IPV4;

    for (p = hostname, colon_count = 0; *p; p++) {
        if (*p == ':') {
            colon_count++;
            continue;
        }
        if (!((*p >= 'a' && *p <= 'f') || (*p >= '0' && *p <= '9') || *p == ':'))
            break;
    }
    if (!*p && colon_count)
        return HOSTCHECK_IS_IPV6;

    for (p = hostname; *p; p++)
        if (!( (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '.' || *p == '-' ))
            return HOSTCHECK_BADCHAR;

    for (p = hostname, colon_count = 0; *p && *p != '.'; p++);
    if (!*p)
        return HOSTCHECK_NOT_FQDN;

    return HOSTCHECK_SANE;
}

int util_str_to_bool(const char *str) {
    /* consider NULL and empty strings false */
    if (!str || !*str)
        return 0;

    /* common words for true values */
    if (strcasecmp(str, "true") == 0 ||
        strcasecmp(str, "yes")  == 0 ||
        strcasecmp(str, "on")   == 0 )
        return 1;

    /* old style numbers: consider everyting non-zero true */
    if (atoi(str))
        return 1;

    /* we default to no */
    return 0;
}

int util_str_to_loglevel(const char *str) {
    if (strcasecmp(str, "debug") == 0 || strcasecmp(str, "DBUG") == 0)
        return ICECAST_LOGLEVEL_DEBUG;
    if (strcasecmp(str, "information") == 0 || strcasecmp(str, "INFO") == 0)
        return ICECAST_LOGLEVEL_INFO;
    if (strcasecmp(str, "warning") == 0 || strcasecmp(str, "WARN") == 0)
        return ICECAST_LOGLEVEL_WARN;
    if (strcasecmp(str, "error") == 0 || strcasecmp(str, "EROR") == 0)
        return ICECAST_LOGLEVEL_ERROR;

    /* gussing it is old-style numerical setting */
    return atoi(str);
}

int util_str_to_int(const char *str, const int default_value)
{
    /* consider NULL and empty strings default */
    if (!str || !*str)
        return default_value;
    return atoi(str);
}

unsigned int util_str_to_unsigned_int(const char *str, const unsigned int default_value)
{
    long int val;
    char *rem = NULL;

    /* consider NULL and empty strings default */
    if (!str || !*str)
        return default_value;

    val = strtol(str, &rem, 10);

    /* There is a left over */
    if (rem && *rem)
        return default_value;

    if (val < 0)
        return default_value;

    return (unsigned int)(unsigned long int)val;
}

/* TODO, FIXME: handle memory allocation errors better. */
static inline void   _build_headers_loop(char **ret, size_t *len, ice_config_http_header_t *header, int status) {
    size_t headerlen;
    const char *name;
    const char *value;
    char *r = *ret;
    char *n;

    if (!header)
        return;

    do {
        /* filter out header's we don't use. */
        if (header->status != 0 && header->status != status) continue;

        /* get the name of the header */
        name = header->name;

        /* handle type of the header */
        value = NULL;
        switch (header->type) {
            case HTTP_HEADER_TYPE_STATIC:
                value = header->value;
                break;
        }

        /* check data */
        if (!name || !value)
            continue;

        /* append the header to the buffer */
        headerlen = strlen(name) + strlen(value) + 4;
        *len += headerlen;
        n = realloc(r, *len);
        if (n) {
            r = n;
            strcat(r, name);
            strcat(r, ": ");
            strcat(r, value);
            strcat(r, "\r\n");
        } else {
            /* FIXME: we skip this header. We should do better. */
            *len -= headerlen;
        }
    } while ((header = header->next));
    *ret = r;
}
static inline char * _build_headers(int                 status,
                                    ice_config_t       *config,
                                    source_t           *source,
                                    struct _client_tag *client)
{
    mount_proxy *mountproxy = NULL;
    char *ret = NULL;
    size_t len = 1;

    if (source)
        mountproxy = config_find_mount(config, source->mount, MOUNT_TYPE_NORMAL);

    ret = calloc(1, 1);
    *ret = 0;

    _build_headers_loop(&ret, &len, config->http_headers, status);
    if (mountproxy && mountproxy->http_headers)
        _build_headers_loop(&ret, &len, mountproxy->http_headers, status);

    cors_set_headers(&ret, &len, config->cors_paths, client);

    return ret;
}

ssize_t util_http_build_header(char * out, size_t len, ssize_t offset,
        int cache,
        int status, const char * statusmsg,
        const char * contenttype, const char * charset,
        const char * datablock,
        struct source_tag * source, struct _client_tag * client) {
    const char * http_version = "1.0";
    ice_config_t *config;
    time_t now;
    struct tm result;
    struct tm *gmtime_result;
    char currenttime_buffer[80];
    char status_buffer[80];
    char contenttype_buffer[80];
    ssize_t ret;
    char * extra_headers;
    const char *connection_header = "Close";
    const char *upgrade_header = "";

    if (!out)
        return -1;

    if (client) {
        if (client->con->tlsmode != ICECAST_TLSMODE_DISABLED)
            upgrade_header = "Upgrade: TLS/1.0\r\n";
        switch (client->reuse) {
            case ICECAST_REUSE_CLOSE:      connection_header = "Close"; break;
            case ICECAST_REUSE_KEEPALIVE:  connection_header = "Keep-Alive"; break;
            case ICECAST_REUSE_UPGRADETLS: connection_header = "Upgrade"; upgrade_header = ""; break;
        }
    }

    if (offset == -1)
        offset = strlen (out);

    out += offset;
    len -= offset;

    if (status == -1)
    {
        status_buffer[0] = '\0';
    }
    else
    {
        if (!statusmsg)
        {
            switch (status)
            {
                case 100: statusmsg = "Continue"; http_version = "1.1"; break;
                case 101: statusmsg = "Switching Protocols"; http_version = "1.1"; break;
                case 200: statusmsg = "OK"; break;
                case 206: statusmsg = "Partial Content"; http_version = "1.1"; break;
                case 400: statusmsg = "Bad Request"; break;
                case 401: statusmsg = "Authentication Required"; break;
                case 403: statusmsg = "Forbidden"; break;
                case 404: statusmsg = "File Not Found"; break;
                case 405: statusmsg = "Method Not Allowed"; break;
                case 409: statusmsg = "Conflict"; break;
                case 415: statusmsg = "Unsupported Media Type"; break;
                case 416: statusmsg = "Request Range Not Satisfiable"; break;
                case 426: statusmsg = "Upgrade Required"; http_version = "1.1"; break;
                case 429: statusmsg = "Too Many Requests"; break;
                /* case of 500 is handled differently. No need to list it here. -- ph3-der-loewe, 2018-05-05 */
                case 501: statusmsg = "Unimplemented"; break;
                case 503: statusmsg = "Service Unavailable"; break;
                default:  statusmsg = "(unknown status code)"; break;
            }
        }
        snprintf (status_buffer, sizeof (status_buffer), "HTTP/%s %d %s\r\n", http_version, status, statusmsg);
    }

    if (contenttype)
    {
        if (charset)
            snprintf (contenttype_buffer, sizeof (contenttype_buffer), "Content-Type: %s; charset=%s\r\n",
                                                                       contenttype, charset);
        else
            snprintf (contenttype_buffer, sizeof (contenttype_buffer), "Content-Type: %s\r\n",
                                                                       contenttype);
    }
    else
    {
        contenttype_buffer[0] = '\0';
    }

    time(&now);
#ifndef _WIN32
    gmtime_result = gmtime_r(&now, &result);
#else
    /* gmtime() on W32 breaks POSIX and IS thread-safe (uses TLS) */
    gmtime_result = gmtime (&now);
    if (gmtime_result)
        memcpy (&result, gmtime_result, sizeof (result));
#endif

    if (gmtime_result)
        strftime(currenttime_buffer, sizeof(currenttime_buffer), "Date: %a, %d %b %Y %X GMT\r\n", gmtime_result);
    else
        currenttime_buffer[0] = '\0';

    config = config_get_config();
    extra_headers = _build_headers(status, config, source, client);
    ret = snprintf (out, len, "%sServer: %s\r\nConnection: %s\r\nAccept-Encoding: identity\r\nAllow: %s\r\n%s%s%s%s%s%s%s%s",
                              status_buffer,
                              config->server_id,
                              connection_header,
                              (client && client->admin_command == ADMIN_COMMAND_ERROR ?
                                  "GET, OPTIONS, SOURCE" : "GET, OPTIONS"),
                              upgrade_header,
                              currenttime_buffer,
                              contenttype_buffer,
                              (status == 401 ? "WWW-Authenticate: Basic realm=\"Icecast2 Server\"\r\n" : ""),
                              (cache     ? "" : "Cache-Control: no-cache\r\n"
                                                "Expires: Mon, 26 Jul 1997 05:00:00 GMT\r\n"
                                                "Pragma: no-cache\r\n"),
                              extra_headers,
                              (datablock ? "\r\n" : ""),
                              (datablock ? datablock : ""));
    free(extra_headers);
    config_release_config();

    return ret;
}

#define __SELECT_BEST_MAX_ARGS  8

struct __args {
    const char *comp;
    char *group;
    int best_comp;
    int best_group;
    int best_all;
};

static inline int __fill_arg(struct __args *arg, const char *str)
{
    char *delm;
    size_t len;

    arg->comp = str;
    arg->best_comp = 0;
    arg->best_group = 0;
    arg->best_all = 0;

    len = strlen(str);
    arg->group = malloc(len + 2);
    if (!arg->group)
        return -1;

    memcpy(arg->group, str, len + 1);

    delm = strstr(arg->group, "/");
    if (delm) {
        delm[0] = '/';
        delm[1] = '*';
        delm[2] = 0;
    }

    return 0;
}

static inline void __free_args(struct __args *arg, size_t len)
{
    size_t i;

    for (i = 0; i < len; i++) {
        free(arg[i].group);
    }
}

static inline int __parse_q(const char *str)
{
    int ret = 0;
    int mul = 1000;

    for (; *str; str++) {
        if (*str >= '0' && *str <= '9') {
            ret += mul * (*str - '0');
            mul /= 10;
        } else if (*str == '.') {
            mul = 100;
        } else {
            ICECAST_LOG_ERROR("Badly formated quality parameter found.");
            return -1;
        }
    }

    return ret;
}

static inline int __find_q_in_index(icecast_kva_t *kv, size_t idx)
{
    size_t i = kv->index[idx] + 1;
    size_t last;

    if (kv->indexlen <= (idx + 1)) {
        last = kv->kvlen - 1;
    } else {
        last = kv->index[idx + 1] - 1;
    }

    for (; i <= last; i++) {
        if (kv->kv[i].key && kv->kv[i].key && strcasecmp(kv->kv[i].key, "q") == 0) {
            return __parse_q(kv->kv[i].value);
        }
    }

    return 1000;
}

const char *util_http_select_best(const char *input, const char *first, ...)
{
    struct __args arg[__SELECT_BEST_MAX_ARGS];
    icecast_kva_t *kv;
    const char *p;
    size_t arglen = 1;
    size_t i, h;
    va_list ap;
    int q;
    int best_q = 0;

    if (__fill_arg(&(arg[0]), first) == -1) {
        ICECAST_LOG_ERROR("Can not allocate memory. Selecting first option.");
        return first;
    }
    va_start(ap, first);
    while ((p = (const char*)va_arg(ap, const char*))) {
        if (arglen == __SELECT_BEST_MAX_ARGS) {
            ICECAST_LOG_ERROR("More arguments given than supported. Currently %zu args are supported.", (size_t)__SELECT_BEST_MAX_ARGS);
            break;
        }
        if (__fill_arg(&(arg[arglen]), p) == -1) {
            ICECAST_LOG_ERROR("Can not allocate memory. Selecting first option.");
            __free_args(arg, arglen);
            return first;
        }

        arglen++;
    }
    va_end(ap);

    kv = util_parse_http_cn(input);
    if (!kv) {
        ICECAST_LOG_ERROR("Input string does not parse as KVA. Selecting first option.");
        __free_args(arg, arglen);
        return first;
    }

    ICECAST_LOG_DEBUG("--- DUMP ---");
    for (i = 0; i < kv->kvlen; i++) {
        ICECAST_LOG_DEBUG("kv[%zu] = {.key='%H', .value='%H'}", i, kv->kv[i].key, kv->kv[i].value);
    }
    for (i = 0; i < kv->indexlen; i++) {
        ICECAST_LOG_DEBUG("index[%zu] = %zu", i, kv->index[i]);
    }
    ICECAST_LOG_DEBUG("--- END OF DUMP ---");

    for (h = 0; h < arglen; h++) {
        for (i = 0; i < kv->indexlen; i++) {
            p = kv->kv[kv->index[i]].key;
            if (!p) {
                continue;
            }

            q = __find_q_in_index(kv, i);
            if (best_q < q) {
                best_q = q;
            }

            if (strcasecmp(p, arg[h].comp) == 0) {
                if (arg[h].best_comp < q) {
                    arg[h].best_comp = q;
                }
            }

            if (strcasecmp(p, arg[h].group) == 0) {
                if (arg[h].best_group < q) {
                    arg[h].best_group = q;
                }
            }
        }
    }

    util_kva_free(kv);

    p = NULL;
    for (h = 0; p == NULL && h < arglen; h++) {
        if (arg[h].best_comp == best_q) {
            p = arg[h].comp;
        }
    }
    for (h = 0; p == NULL && h < arglen; h++) {
        if (arg[h].best_group == best_q) {
            p = arg[h].comp;
        }
    }

    __free_args(arg, arglen);

    if (p == NULL) {
        p = first;
    }

    return p;
}

static inline void __skip_space(char **p)
{
    for (; **p == ' '; (*p)++);
}

static inline int __is_token(const char p)
{
    return (p >= 'a' && p <= 'z') || (p >= 'A' && p <= 'Z') || (p >= '0' && p <= '9') ||
            p == '!' || p == '#'  ||  p == '$' || p == '%'  ||  p == '&' || p == '\'' ||
            p == '*' || p == '+'  ||  p == '-' || p == '.'  ||  p == '^' || p == '_'  ||
            p == '|' || p == '~'  ||  p == '/';

}

enum __tokenizer_result {
    __TOKENIZER_RESULT_COMMA,
    __TOKENIZER_RESULT_EQ,
    __TOKENIZER_RESULT_SEMICOLON,
    __TOKENIZER_RESULT_ILSEQ,
    __TOKENIZER_RESULT_EOS
};

static inline enum __tokenizer_result __tokenizer_res_from_char(const char p)
{
    switch (p) {
        case 0:
            return __TOKENIZER_RESULT_EOS;
        break;
        case ',':
            return __TOKENIZER_RESULT_COMMA;
        break;
        case '=':
            return __TOKENIZER_RESULT_EQ;
        break;
        case ';':
            return __TOKENIZER_RESULT_SEMICOLON;
        break;
        default:
            return __TOKENIZER_RESULT_ILSEQ;
        break;
    }
}

static enum __tokenizer_result __tokenizer_str(char **out, char **in)
{
    char *p, *o;
    char c;

    __skip_space(in);

    p = *in;
    if (*p != '"')
        return __TOKENIZER_RESULT_ILSEQ;

    p++;
    o = p;

    for (; (c = *p); p++) {
        if (c == '\t' || c == ' ' || c == 0x21 || (c >= 0x23 && c <= 0x5B) || (c >= 0x5D && c <= 0x7E) || (c >= 0x80 && c <= 0xFF)) {
            *(o++) = c;
        } else if (c == '\\') {
            p++;
            c = *p;
            if (c == 0) {
                return __TOKENIZER_RESULT_ILSEQ;
            } else {
                *(o++) = c;
            }
        } else if (c == '"') {
            *o = 0;
            break;
        } else {
            return __TOKENIZER_RESULT_ILSEQ;
        }
    }

    if (*p == '"') {
        p++;
        *in = p + 1;
        __skip_space(in);
        return __tokenizer_res_from_char(*p);
    } else {
        return __TOKENIZER_RESULT_ILSEQ;
    }
}

static enum __tokenizer_result __tokenizer(char **out, char **in)
{
    char *p;
    char c = 0;

    __skip_space(in);

    p = *in;

    switch (*p) {
        case '=':
        /* fall through */
        case ',':
        /* fall through */
        case ';':
            return __TOKENIZER_RESULT_ILSEQ;
        break;
        case 0:
            return __TOKENIZER_RESULT_EOS;
        break;
        case '"':
            return __tokenizer_str(out, in);
        break;
    }

    *out = p;
    for (; __is_token(*p); p++);

    *in = p;
    if (*p) {
        __skip_space(in);
        c = **in;
        if (c != 0) {
            (*in)++;
        }
    }
    *p = 0;

    return __tokenizer_res_from_char(c);
}

#define HTTP_CN_INDEX_INCREMENT     8
#define HTTP_CN_KV_INCREMENT        8

static inline int __resize_array(void **also_ptr, void **array, size_t size, size_t *len, size_t revlen, size_t inc)
{
    void *n;

    if (*len > revlen)
        return 0;

    n = realloc(*array, size*(*len + inc));
    if (!n) {
        return -1;
    }

    memset(n + size * *len, 0, size * inc);

    *also_ptr = *array = n;
    *len += inc;

    return 0;
}

icecast_kva_t * util_parse_http_cn(const char *cnstr)
{
    icecast_kva_t *ret;
    char *in;
    int eos = 0;
    size_t indexphylen = HTTP_CN_INDEX_INCREMENT;
    size_t kvphylen = HTTP_CN_KV_INCREMENT;

    if (!cnstr || !*cnstr)
        return NULL;

    ret = calloc(1, sizeof(*ret));

    if (!ret)
        return NULL;

    ret->_tofree[0] = in = strdup(cnstr);
    ret->_tofree[1] = ret->index = calloc(HTTP_CN_INDEX_INCREMENT, sizeof(*(ret->index)));
    ret->_tofree[2] = ret->kv = calloc(HTTP_CN_KV_INCREMENT, sizeof(*(ret->kv)));
    if (!ret->_tofree[0] || !ret->_tofree[1] || !ret->_tofree[2]) {
        util_kva_free(ret);
        return NULL;
    }


    /* we have at minimum one token */
    ret->indexlen = 1;
    ret->kvlen = 1;

    while (!eos) {
        char *out = NULL;
        enum __tokenizer_result res = __tokenizer(&out, &in);

        switch (res) {
            case __TOKENIZER_RESULT_ILSEQ:
                ICECAST_LOG_DEBUG("Illegal byte sequence error from tokenizer.");
                util_kva_free(ret);
                return NULL;
            break;
            case __TOKENIZER_RESULT_EOS:
            /* fall through */
            case __TOKENIZER_RESULT_COMMA:
            /* fall through */
            case __TOKENIZER_RESULT_EQ:
            /* fall through */
            case __TOKENIZER_RESULT_SEMICOLON:
                ICECAST_LOG_DEBUG("OK from tokenizer.");
                /* no-op */
            break;
        }

        if (__resize_array(&(ret->_tofree[2]), (void**)&(ret->kv), sizeof(*(ret->kv)), &kvphylen, ret->kvlen, HTTP_CN_KV_INCREMENT) == -1 ||
            __resize_array(&(ret->_tofree[1]), (void**)&(ret->index), sizeof(*(ret->index)), &indexphylen, ret->indexlen, HTTP_CN_INDEX_INCREMENT) == -1) {
            util_kva_free(ret);
            return NULL;
        }

        if (ret->kv[ret->kvlen-1].key == NULL) {
            ret->kv[ret->kvlen-1].key = out;
        } else if (ret->kv[ret->kvlen-1].value == NULL) {
            ret->kv[ret->kvlen-1].value = out;
        } else {
            util_kva_free(ret);
            return NULL;
        }

        switch (res) {
            case __TOKENIZER_RESULT_EOS:
                ICECAST_LOG_DEBUG("End of string from tokenizer.");
                eos = 1;
                continue;
            break;
            case __TOKENIZER_RESULT_COMMA:
                ICECAST_LOG_DEBUG("Comma from tokenizer.");
                ret->index[ret->indexlen++] = ret->kvlen;
                ret->kvlen++;
            break;
            case __TOKENIZER_RESULT_EQ:
                ICECAST_LOG_DEBUG("Eq from tokenizer.");
                /* no-op */
            break;
            case __TOKENIZER_RESULT_SEMICOLON:
                ICECAST_LOG_DEBUG("Semicolon from tokenizer.");
                ret->kvlen++;
            break;
            default:
                util_kva_free(ret);
                return NULL;
            break;
        }

        ICECAST_LOG_DEBUG("next...");
    }

    return ret;
}

void util_kva_free(icecast_kva_t *kva)
{
    size_t i;

    if (!kva)
        return;

    for (i = 0; i < (sizeof(kva->_tofree)/sizeof(*(kva->_tofree))); i++)
        free(kva->_tofree[i]);

    free(kva);
}

util_dict *util_dict_new(void)
{
    return (util_dict *)calloc(1, sizeof(util_dict));
}

void util_dict_free(util_dict *dict)
{
    util_dict *next;

    while (dict) {
        next = dict->next;

        if (dict->key)
            free (dict->key);
        if (dict->val)
            free (dict->val);
        free (dict);

        dict = next;
    }
}

const char *util_dict_get(util_dict *dict, const char *key)
{
    while (dict) {
        if (dict->key && !strcmp(key, dict->key))
            return dict->val;
        dict = dict->next;
    }
    return NULL;
}

int util_dict_set(util_dict *dict, const char *key, const char *val)
{
    util_dict *prev;

    if (!dict || !key) {
        ICECAST_LOG_ERROR("NULL values passed to util_dict_set()");
        return 0;
    }

    prev = NULL;
    while (dict) {
        if (!dict->key || !strcmp(dict->key, key))
            break;
        prev = dict;
        dict = dict->next;
    }

    if (!dict) {
        dict = util_dict_new();
        if (!dict) {
            ICECAST_LOG_ERROR("unable to allocate new dictionary");
            return 0;
        }
        if (prev)
            prev->next = dict;
    }

    if (dict->key)
        free (dict->val);
    else if (!(dict->key = strdup(key))) {
        if (prev)
            prev->next = NULL;
        util_dict_free (dict);

        ICECAST_LOG_ERROR("unable to allocate new dictionary key");
        return 0;
    }

    dict->val = strdup(val);
    if (!dict->val) {
        ICECAST_LOG_ERROR("unable to allocate new dictionary value");
        return 0;
    }

    return 1;
}

/* given a dictionary, URL-encode each val and stringify it in order as
   key=val&key=val... if val is set, or just key&key if val is NULL.
   TODO: Memory management needs overhaul. */
char *util_dict_urlencode(util_dict *dict, char delim)
{
    char *res, *tmp;
    char *enc;
    int start = 1;

    for (res = NULL; dict; dict = dict->next) {
        /* encode key */
        if (!dict->key)
            continue;
        if (start) {
            if (!(res = malloc(strlen(dict->key) + 1))) {
                return NULL;
            }
            sprintf(res, "%s", dict->key);
            start = 0;
        } else {
            if (!(tmp = realloc(res, strlen(res) + strlen(dict->key) + 2))) {
                free(res);
                return NULL;
            } else
                res = tmp;
            sprintf(res + strlen(res), "%c%s", delim, dict->key);
        }

        /* encode value */
        if (!dict->val)
            continue;
        if (!(enc = util_url_escape(dict->val))) {
            free(res);
            return NULL;
        }

        if (!(tmp = realloc(res, strlen(res) + strlen(enc) + 2))) {
            free(enc);
            free(res);
            return NULL;
        } else
            res = tmp;
        sprintf(res + strlen(res), "=%s", enc);
        free(enc);
    }

    return res;
}

#ifndef HAVE_LOCALTIME_R
struct tm *localtime_r (const time_t *timep, struct tm *result)
{
     static mutex_t localtime_lock;
     static int initialised = 0;
     struct tm *tm;

     if (initialised == 0)
     {
         thread_mutex_create (&localtime_lock);
         initialised = 1;
     }
     thread_mutex_lock (&localtime_lock);
     tm = localtime (timep);
     memcpy (result, tm, sizeof (*result));
     thread_mutex_unlock (&localtime_lock);
     return result;
}
#endif


/* helper function for converting a passed string in one character set to another
 * we use libxml2 for this
 */
char *util_conv_string (const char *string, const char *in_charset, const char *out_charset)
{
    xmlCharEncodingHandlerPtr in, out;
    char *ret = NULL;

    if (string == NULL || in_charset == NULL || out_charset == NULL)
        return NULL;

    in  = xmlFindCharEncodingHandler (in_charset);
    out = xmlFindCharEncodingHandler (out_charset);

    if (in && out)
    {
        xmlBufferPtr orig = xmlBufferCreate ();
        xmlBufferPtr utf8 = xmlBufferCreate ();
        xmlBufferPtr conv = xmlBufferCreate ();

        ICECAST_LOG_INFO("converting metadata from %s to %s", in_charset, out_charset);
        xmlBufferCCat (orig, string);
        if (xmlCharEncInFunc (in, utf8, orig) > 0)
        {
            xmlCharEncOutFunc (out, conv, NULL);
            if (xmlCharEncOutFunc (out, conv, utf8) >= 0)
                ret = strdup ((const char *)xmlBufferContent (conv));
        }
        xmlBufferFree (orig);
        xmlBufferFree (utf8);
        xmlBufferFree (conv);
    }
    xmlCharEncCloseFunc (in);
    xmlCharEncCloseFunc (out);

    return ret;
}


int get_line(FILE *file, char *buf, size_t siz)
{
    if(fgets(buf, (int)siz, file)) {
        size_t len = strlen(buf);
        if(len > 0 && buf[len-1] == '\n') {
            buf[--len] = 0;
            if(len > 0 && buf[len-1] == '\r')
                buf[--len] = 0;
        }
        return 1;
    }
    return 0;
}
