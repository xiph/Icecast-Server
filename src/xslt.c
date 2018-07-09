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
 * Copyright 2012-2018, Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
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

#include "xslt.h"
#include "refbuf.h"
#include "client.h"
#include "errors.h"
#include "stats.h"
#include "fserve.h"
#include "util.h"
#include "cfgfile.h"

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
/* Admin URI cache */
static xmlChar *admin_URI = NULL;

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

    xslt_clear_cache();

    thread_mutex_destroy (&xsltlock);
    xmlCleanupParser();
    xsltCleanupGlobals();
    if (admin_URI)
        xmlFree(admin_URI);
}

static void clear_cache_entry(size_t idx) {
    free(cache[idx].filename);
    if (cache[idx].stylesheet)
        xsltFreeStylesheet(cache[idx].stylesheet);
}

void xslt_clear_cache(void)
{
    size_t i;

    ICECAST_LOG_DEBUG("Clearing stylesheet cache.");

    thread_mutex_lock(&xsltlock);

    for (i = 0; i < CACHESIZE; i++)
        clear_cache_entry(i);

    thread_mutex_unlock(&xsltlock);
}

static int evict_cache_entry(void) {
    int i, age=0, oldest=0;

    for(i=0; i < CACHESIZE; i++) {
        if(cache[i].cache_age > age) {
            age = cache[i].cache_age;
            oldest = i;
        }
    }

    clear_cache_entry(oldest);

    return oldest;
}

static xsltStylesheetPtr xslt_get_stylesheet(const char *fn) {
    int i;
    int empty = -1;
    struct stat file;

    ICECAST_LOG_DEBUG("Looking up stylesheet file \"%s\".", fn);

    if (stat(fn, &file) != 0) {
        ICECAST_LOG_WARN("Error checking for stylesheet file \"%s\": %s", fn,
                strerror(errno));
        return NULL;
    }

    for (i = 0; i < CACHESIZE; i++) {
        if(cache[i].filename) {
#ifdef _WIN32
            if(!stricmp(fn, cache[i].filename)) {
#else
            if(!strcmp(fn, cache[i].filename)) {
#endif
                if(file.st_mtime > cache[i].last_modified) {
                    ICECAST_LOG_DEBUG("Source file newer than cached copy. Reloading slot %i", i);
                    xsltFreeStylesheet(cache[i].stylesheet);

                    cache[i].last_modified = file.st_mtime;
                    cache[i].stylesheet = xsltParseStylesheetFile(XMLSTR(fn));
                    cache[i].cache_age = time(NULL);
                }
                ICECAST_LOG_DEBUG("Using cached sheet %i", i);
                return cache[i].stylesheet;
            }
        } else {
            empty = i;
        }
    }

    if (empty >= 0) {
        i = empty;
        ICECAST_LOG_DEBUG("Using empty slot %i", i);
    } else {
        i = evict_cache_entry();
        ICECAST_LOG_DEBUG("Using evicted slot %i", i);
    }

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
    xmlChar *rel_URI, *fn, *final_URI = NULL;
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
            rel_URI = xmlBuildRelativeURI(URI, c->doc->URL);
            if (rel_URI != NULL && admin_URI != NULL) {
                fn = xmlBuildURI(rel_URI, admin_URI);
                final_URI = fn;
                xmlFree(rel_URI);
            }

            /* Fail if there was an error constructing the path */
            if (final_URI == NULL) {
                if (rel_URI)
                    xmlFree(rel_URI);
                return NULL;
            }
        break;

        /* In case a top stylesheet is loaded */
        case XSLT_LOAD_START:
            config = config_get_config();

            /* Check if we need to load the admin path */
            if (!admin_URI) {
                /* Append path separator to path */
                size_t len = strlen(config->adminroot_dir);
                xmlChar* admin_path = xmlMalloc(len+2);
                xmlStrPrintf(admin_path, len+2, "%s/", XMLSTR(config->adminroot_dir));

                /* Convert admin path to URI */
                admin_URI = xmlPathToURI(admin_path);
                xmlFree(admin_path);

                if (!admin_URI) {
                    return NULL;
                } else {
                    ICECAST_LOG_DEBUG("Loaded and cached admin_URI \"%s\"", admin_URI);
                }
            }
            config_release_config();
        break;

        /* Avoid warnings about other events we don't care for */
        default:
        break;
    }

    /* Get the actual xmlDoc */
    if (final_URI) {
        ICECAST_LOG_DEBUG("Calling xslt_loader() for \"%s\" (was: \"%s\").", final_URI, URI);
        ret = xslt_loader(final_URI, dict, options, ctxt, type);
        xmlFree(final_URI);
    } else {
        ICECAST_LOG_DEBUG("Calling xslt_loader() for \"%s\".", URI);
        ret = xslt_loader(URI, dict, options, ctxt, type);
    }
    return ret;
}

static inline void _send_error(client_t *client, icecast_error_id_t id, int old_status) {
    if (old_status >= 400) {
        client_send_error_by_id(client, ICECAST_ERROR_RECURSIVE_ERROR);
        return;
    }

    client_send_error_by_id(client, id);
}

void xslt_transform(xmlDocPtr doc, const char *xslfilename, client_t *client, int status)
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
        _send_error(client, ICECAST_ERROR_XSLT_PARSE, status);
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
        ret = util_http_build_header(refbuf->data, full_len, 0, 0, status, NULL, mediatype, charset, NULL, NULL, client);
        if (ret == -1) {
            ICECAST_LOG_ERROR("Dropping client as we can not build response headers.");
            _send_error(client, ICECAST_ERROR_GEN_HEADER_GEN_FAILED, status);
        } else {
            if ( full_len < (ret + (ssize_t)len + (ssize_t)64) ) {
                void *new_data;
                full_len = ret + (ssize_t)len + (ssize_t)64;
                new_data = realloc(refbuf->data, full_len);
                if (new_data) {
                    ICECAST_LOG_DEBUG("Client buffer reallocation succeeded.");
                    refbuf->data = new_data;
                    refbuf->len = full_len;
                    ret = util_http_build_header(refbuf->data, full_len, 0, 0, status, NULL, mediatype, charset, NULL, NULL, client);
                    if (ret == -1) {
                        ICECAST_LOG_ERROR("Dropping client as we can not build response headers.");
                        _send_error(client, ICECAST_ERROR_GEN_HEADER_GEN_FAILED, status);
                        failed = 1;
                    }
                } else {
                    ICECAST_LOG_ERROR("Client buffer reallocation failed. Dropping client.");
                    _send_error(client, ICECAST_ERROR_GEN_BUFFER_REALLOC, status);
                    failed = 1;
                }
            }

            if (!failed) {
                  snprintf(refbuf->data + ret, full_len - ret, "Content-Length: %d\r\n\r\n%s", len, string);

                client->respcode = status;
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
        _send_error(client, ICECAST_ERROR_XSLT_problem, status);
    }
    thread_mutex_unlock (&xsltlock);
    xmlFreeDoc(res);
}

