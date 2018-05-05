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
#include <libxml/uri.h>
#include <libxslt/xslt.h>
#include <libxslt/xsltInternals.h>
#include <libxslt/transform.h>
#include <libxslt/xsltutils.h>
#include <libxslt/documents.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>

#ifdef  HAVE_SYS_TIME_H
#include <sys/time.h>
#endif

#ifdef WIN32
#define snprintf _snprintf
#endif

#include "common/thread/thread.h"
#include "common/avl/avl.h"
#include "common/httpp/httpp.h"
#include "common/net/sock.h"

#include "connection.h"

#include "global.h"
#include "refbuf.h"
#include "client.h"
#include "config.h"
#include "stats.h"
#include "fserve.h"
#include "util.h"

#define CATMODULE "xslt"

#include "logging.h"

typedef struct {
    char *filename;
    time_t last_modified;
    time_t cache_age;
    xsltStylesheetPtr stylesheet;
} stylesheet_cache_t;

#ifndef HAVE_XSLTSAVERESULTTOSTRING
int xsltSaveResultToString(xmlChar **doc_txt_ptr, int * doc_txt_len, xmlDocPtr result, xsltStylesheetPtr style) {
    xmlOutputBufferPtr buf;

    *doc_txt_ptr = NULL;
    *doc_txt_len = 0;
    if (result->children == NULL)
        return (0);

    buf = xmlAllocOutputBuffer(NULL);

    if (buf == NULL)
        return (-1);
    xsltSaveResultTo(buf, result, style);
    if (buf->conv != NULL) {
        *doc_txt_len = buf->conv->use;
        *doc_txt_ptr = xmlStrndup(buf->conv->content, *doc_txt_len);
    } else {
        *doc_txt_len = buf->buffer->use;
        *doc_txt_ptr = xmlStrndup(buf->buffer->content, *doc_txt_len);
    }
    (void) xmlOutputBufferClose(buf);
    return 0;
}
#endif

/* Keep it small... */
#define CACHESIZE 3

static stylesheet_cache_t cache[CACHESIZE];
static mutex_t xsltlock;

/* Reference to the original xslt loader func */
static xsltDocLoaderFunc xslt_loader;
/* Admin path cache */
static xmlChar *admin_path = NULL;

void xslt_initialize(void)
{
    memset(cache, 0, sizeof(stylesheet_cache_t) * CACHESIZE);
    thread_mutex_create(&xsltlock);
    xmlInitParser();
    LIBXML_TEST_VERSION
    xmlSubstituteEntitiesDefault(1);
    xmlLoadExtDtdDefaultValue = 1;
    xslt_loader = xsltDocDefaultLoader;
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
    if (admin_path)
        xmlFree(admin_path);
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
                    cache[i].stylesheet = xsltParseStylesheetFile(XMLSTR(fn));
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
    cache[i].stylesheet = xsltParseStylesheetFile(XMLSTR(fn));
    cache[i].cache_age = time(NULL);
    return cache[i].stylesheet;
}

/* Custom xslt loader */
static xmlDocPtr custom_loader(const        xmlChar *URI,
                               xmlDictPtr   dict,
                               int          options,
                               void        *ctxt,
                               xsltLoadType type)
{
    xmlDocPtr ret;
    xmlChar *rel_path, *fn, *final_URI = NULL;
    char *path_URI = NULL;
    xsltStylesheet *c;
    ice_config_t *config;

    switch (type) {
        /* In case an include is loaded */
        case XSLT_LOAD_STYLESHEET:
            /* URI is an escaped URI, make an unescaped version */
            path_URI = util_url_unescape((const char*)URI);
            /* Error if we can't unescape */
            if (path_URI == NULL)
                return NULL;

            /* Not look in admindir if the include file exists */
            if (access(path_URI, F_OK) == 0) {
                free(path_URI);
                break;
            }
            free(path_URI);

            c = (xsltStylesheet *) ctxt;
            /* Check if we actually have context/path */
            if (ctxt == NULL || c->doc->URL == NULL)
                break;

            /* Construct the right path */
            rel_path = xmlBuildRelativeURI(URI, c->doc->URL);
            if (rel_path != NULL && admin_path != NULL) {
                fn = xmlBuildURI(rel_path, admin_path);
                final_URI = fn;
                xmlFree(rel_path);
            }

            /* Fail if there was an error constructing the path */
            if (final_URI == NULL) {
                if (rel_path)
                    xmlFree(rel_path);
                return NULL;
            }
        break;
        /* In case a top stylesheet is loaded */
        case XSLT_LOAD_START:
            config = config_get_config();
            /* Admin path is cached, so that we don't need to get it from
             * the config every time we load a xsl include.
             * Whenever a new top stylesheet is loaded, we check here
             * if the path in the config has changed and adjust it, if needed.
             */
            if (admin_path != NULL &&
                strcmp(config->adminroot_dir, (char *)admin_path) != 0) {
                xmlFree(admin_path);
                admin_path = NULL;
            }
            /* Do we need to load the admin path? */
            if (!admin_path) {
                size_t len = strlen(config->adminroot_dir);

                admin_path = xmlMalloc(len+2);
                if (!admin_path)
                    return NULL;

                /* Copy over admin path and add a tailing slash. */
                xmlStrPrintf(admin_path, len+2, XMLSTR("%s/"), XMLSTR(config->adminroot_dir));
            }
            config_release_config();
        break;

        /* Avoid warnings about other events we don't care for */
        default:
        break;
    }

    /* Get the actual xmlDoc */
    if (final_URI) {
        ret = xslt_loader(final_URI, dict, options, ctxt, type);
        xmlFree(final_URI);
    } else {
        ret = xslt_loader(URI, dict, options, ctxt, type);
    }
    return ret;
}

void xslt_transform(xmlDocPtr doc, const char *xslfilename, client_t *client)
{
    xmlDocPtr res;
    xsltStylesheetPtr cur;
    xmlChar *string;
    int len, problem = 0;
    const char *mediatype = NULL;
    const char *charset = NULL;

    xmlSetGenericErrorFunc("", log_parse_failure);
    xsltSetGenericErrorFunc("", log_parse_failure);
    xsltSetLoaderFunc(custom_loader);

    thread_mutex_lock(&xsltlock);
    cur = xslt_get_stylesheet(xslfilename);

    if (cur == NULL)
    {
        thread_mutex_unlock(&xsltlock);
        ICECAST_LOG_ERROR("problem reading stylesheet \"%s\"", xslfilename);
        client_send_error(client, 404, 0, "Could not parse XSLT file");
        return;
    }

    res = xsltApplyStylesheet(cur, doc, NULL);
    if (res != NULL) {
        if (xsltSaveResultToString(&string, &len, res, cur) < 0)
            problem = 1;
    } else {
        problem = 1;
    }

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
        ssize_t full_len = strlen(mediatype) + (ssize_t)len + (ssize_t)1024;
        if (full_len < 4096)
            full_len = 4096;
        refbuf = refbuf_new (full_len);

        if (string == NULL)
            string = xmlCharStrdup ("");
        ret = util_http_build_header(refbuf->data, full_len, 0, 0, 200, NULL, mediatype, charset, NULL, NULL, client);
        if (ret == -1) {
            ICECAST_LOG_ERROR("Dropping client as we can not build response headers.");
            client_send_error(client, 500, 0, "Header generation failed.");
        } else {
            if ( full_len < (ret + (ssize_t)len + (ssize_t)64) ) {
                void *new_data;
                full_len = ret + (ssize_t)len + (ssize_t)64;
                new_data = realloc(refbuf->data, full_len);
                if (new_data) {
                    ICECAST_LOG_DEBUG("Client buffer reallocation succeeded.");
                    refbuf->data = new_data;
                    refbuf->len = full_len;
                    ret = util_http_build_header(refbuf->data, full_len, 0, 0, 200, NULL, mediatype, charset, NULL, NULL, client);
                    if (ret == -1) {
                        ICECAST_LOG_ERROR("Dropping client as we can not build response headers.");
                        client_send_error(client, 500, 0, "Header generation failed.");
                        failed = 1;
                    }
                } else {
                    ICECAST_LOG_ERROR("Client buffer reallocation failed. Dropping client.");
                    client_send_error(client, 500, 0, "Buffer reallocation failed.");
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
        client_send_error(client, 404, 0, "XSLT problem");
    }
    thread_mutex_unlock (&xsltlock);
    xmlFreeDoc(res);
}

