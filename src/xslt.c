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

#ifndef _WIN32
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

#define CATMODULE "xslt"

#include "logging.h"

typedef struct {
    char              *filename;
    time_t             last_modified;
    time_t             cache_age;
    xsltStylesheetPtr  stylesheet;
} stylesheet_cache_t;

/* Keep it small... */
#define CACHESIZE 3

stylesheet_cache_t cache[CACHESIZE];
mutex_t xsltlock;

void xslt_initialize()
{
    memset(cache, 0, sizeof(stylesheet_cache_t)*CACHESIZE);
    thread_mutex_create(&xsltlock);
}

void xslt_shutdown() {
    int i;

    for(i=0; i < CACHESIZE; i++) {
        if(cache[i].filename)
            free(cache[i].filename);
        if(cache[i].stylesheet)
            xsltFreeStylesheet(cache[i].stylesheet);
    }

    xsltCleanupGlobals();
}

static int evict_cache_entry() {
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
        DEBUG1("Error checking for stylesheet file: %s", strerror(errno));
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
                    cache[i].stylesheet = xsltParseStylesheetFile(fn);
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
    cache[i].stylesheet = xsltParseStylesheetFile(fn);
    cache[i].cache_age = time(NULL);
    return cache[i].stylesheet;
}

void xslt_transform(xmlDocPtr doc, const char *xslfilename, client_t *client)
{
    xmlOutputBufferPtr outputBuffer;
    xmlDocPtr    res;
    xsltStylesheetPtr cur;
    const char *params[16 + 1];
    size_t count,bytes;

    params[0] = NULL;

    xmlSubstituteEntitiesDefault(1);
    xmlLoadExtDtdDefaultValue = 1;

    thread_mutex_lock(&xsltlock);
    cur = xslt_get_stylesheet(xslfilename);
    thread_mutex_unlock(&xsltlock);

    if (cur == NULL) {
        bytes = sock_write_string(client->con->sock, 
                (char *)"Could not parse XSLT file");
        if(bytes > 0) client->con->sent_bytes += bytes;
        
        return;
    }

    res = xsltApplyStylesheet(cur, doc, params);

    outputBuffer = xmlAllocOutputBuffer(NULL);

    count = xsltSaveResultTo(outputBuffer, res, cur);

    /*  Add null byte to end. */
    bytes = xmlOutputBufferWrite(outputBuffer, 1, "");

    if(sock_write_string(client->con->sock, 
                (char *)outputBuffer->buffer->content))
        client->con->sent_bytes += bytes;
    
    xmlOutputBufferClose(outputBuffer);
    xmlFreeDoc(res);
}

