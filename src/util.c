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

#include "sock.h"

#include "config.h"
#include "util.h"
#include "os.h"
#include "refbuf.h"
#include "connection.h"
#include "client.h"

#define CATMODULE "util"

#include "log.h"
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
int util_timed_wait_for_fd(int fd, int timeout)
{
#ifdef HAVE_POLL
    struct pollfd ufds;

    ufds.fd = fd;
    ufds.events = POLLIN;
    ufds.revents = 0;

    return poll(&ufds, 1, timeout);
#else
    fd_set rfds;
    struct timeval tv;

    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);

    tv.tv_sec = timeout/1000;
    tv.tv_usec = (timeout - tv.tv_sec)*1000;

    return select(fd+1, &rfds, NULL, NULL, &tv);
#endif
}

int util_read_header(int sock, char *buff, unsigned long len)
{
	int read_bytes, ret;
	unsigned long pos;
	char c;
	ice_config_t *config;

	config = config_get_config();

	read_bytes = 1;
	pos = 0;
	ret = 0;

	while ((read_bytes == 1) && (pos < (len - 1))) {
		read_bytes = 0;

        if (util_timed_wait_for_fd(sock, config->header_timeout*1000) > 0) {

			if ((read_bytes = recv(sock, &c, 1, 0))) {
				if (c != '\r') buff[pos++] = c;
				if ((pos > 1) && (buff[pos - 1] == '\n' && buff[pos - 2] == '\n')) {
					ret = 1;
					break;
				}
			}
		} else {
			break;
		}
	}

	if (ret) buff[pos] = '\0';
	
	return ret;
}

char *util_get_extension(char *path) {
    char *ext = strrchr(path, '.');

    if(ext == NULL)
        return "";
    else
        return ext+1;
}

int util_check_valid_extension(char *uri) {
	int	ret = 0;
	char	*p2;

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

char *util_get_path_from_normalised_uri(char *uri) {
    char *fullpath;

    fullpath = malloc(strlen(uri) + strlen(config_get_config()->webroot_dir) + 1);
    strcpy(fullpath, config_get_config()->webroot_dir);

    strcat(fullpath, uri);

    return fullpath;
}


    

/* Get an absolute path (from the webroot dir) from a URI. Return NULL if the
 * path contains 'disallowed' sequences like foo/../ (which could be used to
 * escape from the webroot) or if it cannot be URI-decoded.
 * Caller should free the path.
 */
char *util_normalise_uri(char *uri) {
    int urilen = strlen(uri);
    unsigned char *path;
    char *dst;
    int i;
    int done = 0;

    if(uri[0] != '/')
        return NULL;

    path = calloc(1, urilen + 1);

    dst = path;

    for(i=0; i < urilen; i++) {
        switch(uri[i]) {
            case '%':
                if(i+2 >= urilen) {
                    free(path);
                    return NULL;
                }
                if(hex(uri[i+1]) == -1 || hex(uri[i+2]) == -1 ) {
                    free(path);
                    return NULL;
                }

                *dst++ = hex(uri[i+1]) * 16  + hex(uri[i+2]);
                i+= 2;
                break;
            case '#':
                done = 1;
                break;
            case 0:
                ERROR0("Fatal internal logic error in util_get_path_from_uri()");
                free(path);
                return NULL;
                break;
            default:
                *dst++ = uri[i];
                break;
        }
        if(done)
            break;
    }

    *dst = 0; /* null terminator */

    /* We now have a full URI-decoded path. Check it for allowability */
    if(verify_path(path))
        return (char *)path;
    else {
        WARN1("Rejecting invalid path \"%s\"", path);
        free(path);
        return NULL;
    }
}

