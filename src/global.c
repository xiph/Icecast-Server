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

/* -*- c-basic-offset: 4; indent-tabs-mode: nil; -*- */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include "thread/thread.h"
#include "avl/avl.h"
#include "timing/timing.h"

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
    global.server_sockets = 0;
    global.relays = NULL;
    global.master_relays = NULL;
    global.running = 0;
    global.clients = 0;
    global.sources = 0;
    global.source_tree = avl_tree_new(source_compare_sources, NULL);
#ifdef MY_ALLOC
    global.alloc_tree = avl_tree_new(compare_allocs, NULL);
#endif
    thread_mutex_create(&_global_mutex);
    thread_spin_create (&global.spinlock);
    thread_rwlock_create (&global.shutdown_lock);
    global.out_bitrate = rate_setup (10000, 1000);
}

void global_shutdown(void)
{
    thread_mutex_destroy(&_global_mutex);
    thread_spin_destroy (&global.spinlock);
    thread_rwlock_destroy (&global.shutdown_lock);
    avl_tree_free(global.source_tree, NULL);
#ifdef MY_ALLOC
    avl_tree_free(global.alloc_tree, free_alloc_node);
#endif
    rate_free (global.out_bitrate);
    global.out_bitrate = NULL;
}

void global_lock(void)
{
    thread_mutex_lock(&_global_mutex);
}

void global_unlock(void)
{
    thread_mutex_unlock(&_global_mutex);
}

void global_add_bitrates (struct rate_calc *rate, unsigned long value, uint64_t milli)
{
    thread_spin_lock (&global.spinlock);
    rate_add (rate, value, milli);
    thread_spin_unlock (&global.spinlock);
}

void global_reduce_bitrate_sampling (struct rate_calc *rate)
{
    thread_spin_lock (&global.spinlock);
    rate_reduce (rate, 500);
    thread_spin_unlock (&global.spinlock);
}

unsigned long global_getrate_avg (struct rate_calc *rate)
{
    unsigned long v;
    thread_spin_lock (&global.spinlock);
    v = rate_avg (rate);
    thread_spin_unlock (&global.spinlock);
    return v;
}

#ifdef MY_ALLOC

#undef malloc
#undef calloc
#undef realloc
#undef free

int compare_allocs(void *arg, void *a, void *b)
{
    alloc_node  *nodea = (alloc_node  *)a;
    alloc_node  *nodeb = (alloc_node  *)b;

    return strcmp(nodea->name, nodeb->name);
}

typedef struct 
{
    alloc_node *info;
    size_t len;
} allocheader;

void *my_calloc (const char *file, int line, size_t num, size_t size)
{
    alloc_node match, *result;
    snprintf (match.name, sizeof (match.name), "%s:%d", file, line);

    avl_tree_wlock (global.alloc_tree);
    if (avl_get_by_key (global.alloc_tree, &match, (void**)&result) == 0)
    {
        allocheader *block = calloc (1, (num*size)+sizeof(allocheader));
        result->count++;
        result->allocated += (num*size);
        block->info = result;
        block->len = num*size;
        avl_tree_unlock (global.alloc_tree);
        return block+1;
    }
    result = calloc (1, sizeof(alloc_node));
    if (result)
    {
        allocheader *block = calloc (1, (num*size)+sizeof(allocheader));
        snprintf (result->name, sizeof (result->name), "%s:%d", file, line);
        result->count = 1;
        result->allocated = (num * size);
        avl_insert (global.alloc_tree, result);
        block->info = result;
        block->len = num*size;
        avl_tree_unlock (global.alloc_tree);
        return block+1;
    }
    avl_tree_unlock (global.alloc_tree);
    return NULL;
}
int free_alloc_node(void *key)
{
    alloc_node *node = (alloc_node *)key;
    memset (node, 255, sizeof(*node));
    free(node);
    return 1;
}

void my_free (void *freeblock)
{
    allocheader *block;
    alloc_node *info;
    if (freeblock == NULL)
        return;
    block = (allocheader*)freeblock -1;
    info = block->info;
    avl_tree_wlock (global.alloc_tree);
    info->count--;
    info->allocated -= block->len;
    avl_tree_unlock (global.alloc_tree);
    free (block);
}
void *my_realloc (const char *file, int line, void *ptr, size_t size)
{
    allocheader *block, *newblock;
    alloc_node *info;

    if (ptr == NULL)
        return my_calloc (file, line, 1, size);
    if (size == 0)
    {
        my_free (ptr);
        return NULL;
    }
    block = (allocheader*)ptr -1;
    avl_tree_wlock (global.alloc_tree);
    newblock = realloc (block, sizeof (allocheader)+size);
    info = newblock->info;
    info->allocated -= newblock->len;
    info->allocated += size;
    newblock->len = size;
    avl_tree_unlock (global.alloc_tree);
    return newblock+1;
}
char *my_strdup(const char *file, int line, const char *s)
{
    int len = strlen (s) +1;
    char *str = my_calloc (file, line, 1, len);
    strcpy (str, s);
    return str;
}
#endif
