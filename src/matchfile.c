/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2015,      Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "matchfile.h"
#include "logging.h"
#include "util.h" /* for MAX_LINE_LEN and get_line() */
#include "common/avl/avl.h"
#define CATMODULE "matchfile"

struct matchfile_tag {
    /* reference counter */
    size_t refcount;

    /* filename of input file */
    char *filename;

    time_t file_recheck;
    time_t file_mtime;
    avl_tree *contents;
};

static int __func_free(void *x) {
    free(x);
    return 1;
}

static int __func_compare (void *arg, void *a, void *b) {
    return strcmp(b, a);
}

static void __func_recheck(matchfile_t *file) {
    time_t now = time(NULL);
    struct stat file_stat;
    FILE *input = NULL;
    avl_tree *new_contents;
    char line[MAX_LINE_LEN];

    if (now < file->file_recheck)
        return;

    file->file_recheck = now + 10;

    if (stat(file->filename, &file_stat) < 0) {
        ICECAST_LOG_WARN("failed to check status of \"%s\": %s", file->filename, strerror(errno));
        return;
    }

    if (file_stat.st_mtime == file->file_mtime)
        return; /* common case, no update to file */

    file->file_mtime = file_stat.st_mtime;

    input = fopen(file->filename, "r");
    if (!input) {
        ICECAST_LOG_WARN("Failed to open file \"%s\": %s", file->filename, strerror(errno));
        return;
    }

    new_contents = avl_tree_new(__func_compare, NULL);

    while (get_line(input, line, MAX_LINE_LEN)) {
        char *str;

        if(!line[0] || line[0] == '#')
            continue;
        str = strdup(line);
        if (str)
            avl_insert(new_contents, str);
    }

    fclose(input);

    if (file->contents) avl_tree_free(file->contents, __func_free);
    file->contents = new_contents;
}

matchfile_t *matchfile_new(const char *filename) {
    matchfile_t *ret;

    if (!filename)
        return NULL;

    ret = calloc(1, sizeof(matchfile_t));
    if (!ret)
        return NULL;

    ret->refcount     = 1;
    ret->filename     = strdup(filename);
    ret->file_mtime   = 0;
    ret->file_recheck = 0;

    if (!ret->filename) {
        matchfile_release(ret);
        return NULL;
    }

    /* load initial database */
    __func_recheck(ret);

    return ret;
}

int          matchfile_addref(matchfile_t *file) {
    if (!file)
        return -1;

    file->refcount++;

    return 0;
}

int          matchfile_release(matchfile_t *file) {
    if (!file)
        return -1;

    file->refcount--;

    if (file->refcount)
        return 0;

    if (file->contents)
        avl_tree_free(file->contents, __func_free);
    free(file->filename);
    free(file);

    return 0;
}

/* we are not const char *key because of avl_get_by_key()... */
int          matchfile_match(matchfile_t *file, const char *key) {
    void *result;

    if (!file)
        return -1;

    /* reload database if needed */
    __func_recheck(file);

    if (!file->contents)
        return 0;

    return avl_get_by_key(file->contents, (void*)key, &result) == 0 ? 1 : 0;
}

int          matchfile_match_allow_deny(matchfile_t *allow, matchfile_t *deny, const char *key) {
    if (!allow && !deny)
        return 1;

    if (!key)
        return 0;

    if (matchfile_match(deny, key) > 0) {
        ICECAST_LOG_DEBUG("%s is banned", key);
        return 0;
    }

    if (matchfile_match(allow, key) > 0) {
        ICECAST_LOG_DEBUG("%s is allowed", key);
        return 1;
    } else if (allow) {
        /* we are not on allow list but there is one, so reject */
        ICECAST_LOG_DEBUG("%s is not allowed", key);
        return 0;
    }

    /* default: allow */
    return 1;
}
