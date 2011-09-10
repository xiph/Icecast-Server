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

#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
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

struct bufs
{
    refbuf_t *head, **tail;
    int len;
};


static int xslt_write_callback (void *ctxt, const char *data, int len)
{
    struct bufs *x = ctxt;
    refbuf_t *r;
    int loop = 10;

    if (len == 0)
        return 0;
    if (len < 0 || len > 2000000)
    {
        ERROR1 ("%d length requested", len);
        return -1;
    }
    while (loop)
    {
        int size = len > 4096 ? len : 4096;
        if (*x->tail == NULL)
        {
            *x->tail = refbuf_new (size);
            (*x->tail)->len = 0;
        }
        r = *x->tail;
        if (r->len + len > size)
        {
            x->tail = &r->next;
            loop--;
            continue;
        }
        memcpy (r->data + r->len, data, len);
        r->len += len;
        x->len += len;
        break;
    }
    return len;
}


int xslt_SaveResultToBuf (refbuf_t **bptr, int *len, xmlDocPtr result, xsltStylesheetPtr style)
{
    xmlOutputBufferPtr buf;
    struct bufs x;

    if (result->children == NULL)
    {
        *bptr = NULL;
        *len = 0;
        return 0;
    }

    memset (&x, 0, sizeof (x));
    x.tail = &x.head;
    buf = xmlOutputBufferCreateIO (xslt_write_callback, NULL, &x, NULL);

    if (buf == NULL)
		return  -1;
    xsltSaveResultTo (buf, result, style);
    *bptr = x.head;
    *len = x.len;
    xmlOutputBufferClose(buf);
    return 0;
}

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

    if (cache[oldest].stylesheet)
        xsltFreeStylesheet(cache[oldest].stylesheet);
    free(cache[oldest].filename);

    return oldest;
}

static xsltStylesheetPtr xslt_get_stylesheet(const char *fn) {
    int i;
    int empty = -1;
    struct stat file;

    if(stat(fn, &file)) {
        WARN2("Error checking for stylesheet file \"%s\": %s", fn, 
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
                DEBUG1("Using cached sheet %i", i);
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


int xslt_transform (xmlDocPtr doc, const char *xslfilename, client_t *client)
{
    xmlDocPtr    res;
    xsltStylesheetPtr cur;
    int len;
    refbuf_t *content = NULL;
    char **params = NULL;

    xmlSetGenericErrorFunc ("", log_parse_failure);
    xsltSetGenericErrorFunc ("", log_parse_failure);

    thread_mutex_lock(&xsltlock);
    cur = xslt_get_stylesheet(xslfilename);

    if (cur == NULL)
    {
        thread_mutex_unlock(&xsltlock);
        ERROR1 ("problem reading stylesheet \"%s\"", xslfilename);
        return client_send_404 (client, "Could not parse XSLT file");
    }
    if (client->parser->queryvars)
    {
        // annoying but we need to surround the args with ' when passing them in
        int i, arg_count = client->parser->queryvars->length * 2;
        avl_node *node = avl_get_first (client->parser->queryvars);

        params = calloc (arg_count+1, sizeof (char *));
        for (i = 0; node && i < arg_count; node = avl_get_next (node))
        {
            http_var_t *param = (http_var_t *)node->key;
            params[i++] = param->name;
            params[i] = (char*)alloca (strlen (param->value) +3);
            sprintf (params[i++], "\'%s\'", param->value);
        }
        params[i] = NULL;
    }

    res = xsltApplyStylesheet (cur, doc, (const char **)params);
    free (params);

    if (res == NULL || xslt_SaveResultToBuf (&content, &len, res, cur) < 0)
    {
        thread_mutex_unlock (&xsltlock);
        xmlFreeDoc (res);
        WARN1 ("problem applying stylesheet \"%s\"", xslfilename);
        return client_send_404 (client, "XSLT problem");
    }
    else
    {
        /* the 100 is to allow for the hardcoded headers */
        refbuf_t *refbuf = refbuf_new (100);
        const char *mediatype = NULL;

        /* lets find out the content type to use */
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
        snprintf (refbuf->data, 100,
                "HTTP/1.0 200 OK\r\nContent-Type: %s\r\nContent-Length: %d\r\n\r\n",
                mediatype, len);

        thread_mutex_unlock (&xsltlock);
        client->respcode = 200;
        client_set_queue (client, NULL);
        client->refbuf = refbuf;
        refbuf->len = strlen (refbuf->data);
        refbuf->next = content;
    }
    xmlFreeDoc(res);
    return fserve_setup_client (client);
}

