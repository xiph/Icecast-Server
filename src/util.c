/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2000-2004, Jack Moffitt <jack@xiph.org, 
 *                      Michael Smith <msmith@xiph.org>,
 *                      oddsock <oddsock@xiph.org>,
 *                      Karl Heyes <karl@xiph.org>
 *                      and others (see AUTHORS for details).
 * Copyright 2012-2014, Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <sys/types.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifndef _WIN32
#include <sys/time.h>
#include <sys/socket.h>
#include <unistd.h>
#ifdef HAVE_POLL
#include <sys/poll.h>
#endif
#else
#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#define snprintf _snprintf
#define strcasecmp stricmp
#define strncasecmp strnicmp
#endif

#include "net/sock.h"
#include "thread/thread.h"

#include "cfgfile.h"
#include "util.h"
#include "compat.h"
#include "refbuf.h"
#include "connection.h"
#include "client.h"
#include "source.h"

#define CATMODULE "util"

#include "logging.h"

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
    int    ret = 0;
    char    *p2;

    if (uri) {
        p2 = strrchr(uri, '.');
        if (p2) {
            p2++;
            if (strncmp(p2, "xsl", strlen("xsl")) == 0) {
                /* Build the full path for the request, concatenating the webroot from the config.
                ** Here would be also a good time to prevent accesses like '../../../../etc/passwd' or somesuch.
                */
                ret = XSLT_CONTENT;
            }
            if (strncmp(p2, "htm", strlen("htm")) == 0) {
                /* Build the full path for the request, concatenating the webroot from the config.
                ** Here would be also a good time to prevent accesses like '../../../../etc/passwd' or somesuch.
                */
                ret = HTML_CONTENT;
            }
            if (strncmp(p2, "html", strlen("html")) == 0) {
                /* Build the full path for the request, concatenating the webroot from the config.
                ** Here would be also a good time to prevent accesses like '../../../../etc/passwd' or somesuch.
                */
                ret = HTML_CONTENT;
            }

        }
    }
    return ret;
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

static char hexchars[16] = {
    '0','1','2','3','4','5','6','7','8','9','a','b','c','d','e','f'
};

static char safechars[256] = {
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

static char base64table[64] = {
    'A','B','C','D','E','F','G','H','I','J','K','L','M','N','O','P',
    'Q','R','S','T','U','V','W','X','Y','Z','a','b','c','d','e','f',
    'g','h','i','j','k','l','m','n','o','p','q','r','s','t','u','v',
    'w','x','y','z','0','1','2','3','4','5','6','7','8','9','+','/'
};

static signed char base64decode[256] = {
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

char *util_bin_to_hex(unsigned char *data, int len)
{
    char *hex = malloc(len*2 + 1);
    int i;

    for(i = 0; i < len; i++) {
        hex[i*2] = hexchars[(data[i]&0xf0) >> 4];
        hex[i*2+1] = hexchars[data[i]&0x0f];
    }

    hex[len*2] = 0;

    return hex;
}

/* This isn't efficient, but it doesn't need to be */
char *util_base64_encode(const char *data)
{
    int len = strlen(data);
    char *out = malloc(len*4/3 + 4);
    char *result = out;
    int chunk;

    while(len > 0) {
        chunk = (len >3)?3:len;
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

/* TODO, FIXME: handle memory allocation errors better. */
static inline void   _build_headers_loop(char **ret, size_t *len, ice_config_http_header_t *header, int status) {
    size_t headerlen;
    const char *name;
    const char *value;
    char * r = *ret;

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
        r = realloc(r, *len);
        strcat(r, name);
        strcat(r, ": ");
        strcat(r, value);
        strcat(r, "\r\n");
    } while ((header = header->next));
    *ret = r;
}
static inline char * _build_headers(int status, ice_config_t *config, source_t *source) {
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

    return ret;
}

ssize_t util_http_build_header(char * out, size_t len, ssize_t offset,
        int cache,
        int status, const char * statusmsg,
        const char * contenttype, const char * charset,
        const char * datablock,
        struct source_tag * source) {
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

    if (!out)
        return -1;

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
	        case 200: statusmsg = "OK"; break;
		case 206: statusmsg = "Partial Content"; http_version = "1.1"; break;
		case 400: statusmsg = "Bad Request"; break;
		case 401: statusmsg = "Authentication Required"; break;
		case 403: statusmsg = "Forbidden"; break;
		case 404: statusmsg = "File Not Found"; break;
		case 416: statusmsg = "Request Range Not Satisfiable"; break;
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
    extra_headers = _build_headers(status, config, source);
    ret = snprintf (out, len, "%sServer: %s\r\n%s%s%s%s%s%s%s",
                              status_buffer,
			      config->server_id,
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
        if (!strcmp(key, dict->key))
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

/* given a dictionary, URL-encode each val and 
   stringify it in order as key=val&key=val... if val 
   is set, or just key&key if val is NULL.
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

