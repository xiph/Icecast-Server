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
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <libxml/xmlmemory.h>
#include <libxml/debugXML.h>
#include <libxml/HTMLtree.h>
#include <libxml/xmlIO.h>
#include <libxml/xinclude.h>
#include <libxml/catalog.h>
#include <libxslt/xslt.h>
#include <libxslt/xsltInternals.h>
#include <libxslt/transform.h>
#include <libxslt/xsltutils.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>

#ifdef  HAVE_SYS_TIME_H
#include <sys/time.h>
#endif

#ifdef WIN32
#define snprintf _snprintf
#endif

#include "thread/thread.h"
#include "avl/avl.h"
#include "httpp/httpp.h"
#include "net/sock.h"

#include "connection.h"

#include "global.h"
#include "refbuf.h"
#include "client.h"
#include "stats.h"
#include "fserve.h"
#include "util.h"

#define CATMODULE "xslt"

#include "logging.h"

typedef struct {
    char              *filename;
    time_t             last_modified;
    time_t             cache_age;
    xsltStylesheetPtr  stylesheet;
} stylesheet_cache_t;

#ifndef HAVE_XSLTSAVERESULTTOSTRING
int xsltSaveResultToString(xmlChar **doc_txt_ptr, int * doc_txt_len, xmlDocPtr result, xsltStylesheetPtr style) {
    xmlOutputBufferPtr buf;

    *doc_txt_ptr = NULL;
    *doc_txt_len = 0;
    if (result->children == NULL)
	return(0);

	buf = xmlAllocOutputBuffer(NULL);

    if (buf == NULL)
		return(-1);
    xsltSaveResultTo(buf, result, style);
    if (buf->conv != NULL) {
		*doc_txt_len = buf->conv->use;
		*doc_txt_ptr = xmlStrndup(buf->conv->content, *doc_txt_len);
    } else {
		*doc_txt_len = buf->buffer->use;
		*doc_txt_ptr = xmlStrndup(buf->buffer->content, *doc_txt_len);
    }
    (void)xmlOutputBufferClose(buf);
    return 0;
}
#endif

/* Keep it small... */
#define CACHESIZE 3

static stylesheet_cache_t cache[CACHESIZE];
static mutex_t xsltlock;

void xslt_initialize(void)
{
    memset(cache, 0, sizeof(stylesheet_cache_t)*CACHESIZE);
    thread_mutex_create(&xsltlock);
    xmlInitParser();
    LIBXML_TEST_VERSION
    xmlSubstituteEntitiesDefault(1);
    xmlLoadExtDtdDefaultValue = 1;
}

void xslt_shutdown(void) {
    int i;

    for(i=0; i < CACHESIZE; i++) {
        if(cache[i].filename)
            free(cache[i].filename);
        if(cache[i].stylesheet)
            xsltFreeStylesheet(cache[i].stylesheet);
    }

    thread_mutex_destroy (&xsltlock);
    xmlCleanupParser();
    xsltCleanupGlobals();
}

static int evict_cache_entry(void) {
    int i, age=0, oldest=0;

    for(i=0; i < CACHESIZE; i++) {
        if(cache[i].cache_age > age) {
            age = cache[i].cache_age;
            oldest = i;
        }
    }

    xsltFreeStylesheet(cache[oldest].stylesheet);
    free(cache[oldest].filename);

    return oldest;
}

static xsltStylesheetPtr xslt_get_stylesheet(const char *fn) {
    int i;
    int empty = -1;
    struct stat file;

    if(stat(fn, &file)) {
        ICECAST_LOG_WARN("Error checking for stylesheet file \"%s\": %s", fn, 
                strerror(errno));
        return NULL;
    }

    for(i=0; i < CACHESIZE; i++) {
        if(cache[i].filename)
        {
#ifdef _WIN32
            if(!stricmp(fn, cache[i].filename))
#else
            if(!strcmp(fn, cache[i].filename))
#endif
            {
                if(file.st_mtime > cache[i].last_modified)
                {
                    xsltFreeStylesheet(cache[i].stylesheet);

                    cache[i].last_modified = file.st_mtime;
                    cache[i].stylesheet = xsltParseStylesheetFile (XMLSTR(fn));
                    cache[i].cache_age = time(NULL);
                }
                ICECAST_LOG_DEBUG("Using cached sheet %i", i);
                return cache[i].stylesheet;
            }
        }
        else
            empty = i;
    }

    if(empty>=0)
        i = empty;
    else
        i = evict_cache_entry();

    cache[i].last_modified = file.st_mtime;
    cache[i].filename = strdup(fn);
    cache[i].stylesheet = xsltParseStylesheetFile (XMLSTR(fn));
    cache[i].cache_age = time(NULL);
    return cache[i].stylesheet;
}

void xslt_transform(xmlDocPtr doc, const char *xslfilename, client_t *client)
{
    xmlDocPtr    res;
    xsltStylesheetPtr cur;
    xmlChar *string;
    int len, problem = 0;
    const char *mediatype = NULL;
    const char *charset = NULL;

    xmlSetGenericErrorFunc ("", log_parse_failure);
    xsltSetGenericErrorFunc ("", log_parse_failure);

    thread_mutex_lock(&xsltlock);
    cur = xslt_get_stylesheet(xslfilename);

    if (cur == NULL)
    {
        thread_mutex_unlock(&xsltlock);
        ICECAST_LOG_ERROR("problem reading stylesheet \"%s\"", xslfilename);
        client_send_404 (client, "Could not parse XSLT file");
        return;
    }

    res = xsltApplyStylesheet(cur, doc, NULL);

    if (xsltSaveResultToString (&string, &len, res, cur) < 0)
        problem = 1;

    /* lets find out the content type and character encoding to use */
    if (cur->encoding)
       charset = (char *)cur->encoding;

    if (cur->mediaType)
        mediatype = (char *)cur->mediaType;
    else
    {
        /* check method for the default, a missing method assumes xml */
        if (cur->method && xmlStrcmp (cur->method, XMLSTR("html")) == 0)
            mediatype = "text/html";
        else
            if (cur->method && xmlStrcmp (cur->method, XMLSTR("text")) == 0)
                mediatype = "text/plain";
            else
                mediatype = "text/xml";
    }
    if (problem == 0)
    {
        ssize_t ret;
        int failed = 0;
        refbuf_t *refbuf;
        size_t full_len = strlen (mediatype) + len + 1024;
        if (full_len < 4096)
            full_len = 4096;
        refbuf = refbuf_new (full_len);

        if (string == NULL)
            string = xmlCharStrdup ("");
        ret = util_http_build_header(refbuf->data, full_len, 0, 0, 200, NULL, mediatype, charset, NULL, NULL);
        if (ret == -1) {
            ICECAST_LOG_ERROR("Dropping client as we can not build response headers.");
            client_send_500(client, "Header generation failed.");
        } else {
            if ( full_len < (ret + len + 64) ) {
                void *new_data;
                full_len = ret + len + 64;
                new_data = realloc(refbuf->data, full_len);
                if (new_data) {
                    ICECAST_LOG_DEBUG("Client buffer reallocation succeeded.");
                    refbuf->data = new_data;
                    refbuf->len = full_len;
                    ret = util_http_build_header(refbuf->data, full_len, 0, 0, 200, NULL, mediatype, charset, NULL, NULL);
                    if (ret == -1) {
                        ICECAST_LOG_ERROR("Dropping client as we can not build response headers.");
                        client_send_500(client, "Header generation failed.");
                        failed = 1;
                    }
                } else {
                    ICECAST_LOG_ERROR("Client buffer reallocation failed. Dropping client.");
                    client_send_500(client, "Buffer reallocation failed.");
                    failed = 1;
                }
            }

            if (!failed) {
                  snprintf(refbuf->data + ret, full_len - ret, "Content-Length: %d\r\n\r\n%s", len, string);

                client->respcode = 200;
                client_set_queue (client, NULL);
                client->refbuf = refbuf;
                refbuf->len = strlen (refbuf->data);
                fserve_add_client (client, NULL);
            }
        }
        xmlFree (string);
    }
    else
    {
        ICECAST_LOG_WARN("problem applying stylesheet \"%s\"", xslfilename);
        client_send_404 (client, "XSLT problem");
    }
    thread_mutex_unlock (&xsltlock);
    xmlFreeDoc(res);
}

