/* -*- c-basic-offset: 4; indent-tabs-mode: nil; -*- */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include "thread/thread.h"
#include "avl/avl.h"
#include "httpp/httpp.h"

#include "connection.h"
#include "refbuf.h"
#include "client.h"
#include "source.h"
#include "format.h"

#include "global.h"

ice_global_t global;

static mutex_t _global_mutex;

void global_initialize(void)
{
    memset(global.serversock, 0, sizeof(int)*MAX_LISTEN_SOCKETS);
    global.server_sockets = 0;
    global.running = 0;
    global.clients = 0;
    global.sources = 0;
    global.source_tree = avl_tree_new(source_compare_sources, NULL);
    thread_mutex_create(&_global_mutex);
}

void global_shutdown(void)
{
    thread_mutex_destroy(&_global_mutex);
    avl_tree_free(global.source_tree, source_free_source);
}

void global_lock(void)
{
    thread_mutex_lock(&_global_mutex);
}

void global_unlock(void)
{
    thread_mutex_unlock(&_global_mutex);
}
