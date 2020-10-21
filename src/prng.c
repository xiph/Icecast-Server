/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2020,      Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>
 *
 * The SHA3 implementation is based on rhash:
 * 2013 by Aleksey Kravchenko <rhash.admin@gmail.com>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <stdio.h>

#ifdef HAVE_UNAME
#include <sys/utsname.h>
#endif

#include "common/thread/thread.h"

#include "prng.h"
#include "digest.h"

#include "logging.h"
#define CATMODULE "prng"

#define BLOCK_LENGTH (512/8)

static int initialized = 0;
static mutex_t digest_a_lock;
static mutex_t digest_b_lock;
static digest_t * digest_a;
static digest_t * digest_b;

static void prng_initial_seed(void)
{
    struct {
        int debian;
        time_t t;
#ifdef HAVE_UNAME
        struct utsname utsname;
#endif
#ifdef HAVE_SETUID
        uid_t uid;
        pid_t pid;
        pid_t ppid;
#endif
    } seed;

    seed.debian = 4;
    seed.t = time(NULL);
#ifdef HAVE_UNAME
    uname(&seed.utsname);
#endif
#ifdef HAVE_SETUID
    seed.uid = getuid();
    seed.pid = getpid();
    seed.ppid = getppid();
#endif

    prng_write(&seed, sizeof(seed));
}

void prng_initialize(void)
{
    if (initialized)
        return;

    thread_mutex_create(&digest_a_lock);
    thread_mutex_create(&digest_b_lock);
    digest_a = digest_new(DIGEST_ALGO_SHA3_512);
    digest_b = digest_new(DIGEST_ALGO_SHA3_512);
    initialized = 1;
    prng_initial_seed();
}

void prng_shutdown(void)
{
    if (!initialized)
        return;

    refobject_unref(digest_b);
    refobject_unref(digest_a);
    thread_mutex_destroy(&digest_b_lock);
    thread_mutex_destroy(&digest_a_lock);

    initialized = 0;
}

void prng_configure(ice_config_t *config)
{
    if (!initialized)
        return;

    // no-op at the moment.
}

void prng_write(const void *buffer, size_t len)
{
    if (!initialized)
        return;

    thread_mutex_lock(&digest_a_lock);
    digest_write(digest_a, buffer, len);
    thread_mutex_unlock(&digest_a_lock);
}

static ssize_t prng_read_block(void *froma, void *buffer, size_t len)
{
    char fromb[BLOCK_LENGTH];

    digest_write(digest_b, froma, BLOCK_LENGTH);

    if (digest_read(digest_b, fromb, sizeof(fromb)) != sizeof(fromb))
        return -1;

    refobject_unref(digest_b);
    digest_b = digest_new(DIGEST_ALGO_SHA3_512);
    digest_write(digest_b, fromb, sizeof(fromb));
    digest_write(digest_b, &len, sizeof(len));

    if (len > sizeof(fromb))
        len = sizeof(fromb);

    memcpy(buffer, fromb, len);

    return len;
}

ssize_t prng_read(void *buffer, size_t len)
{
    digest_t *copy;
    char froma[BLOCK_LENGTH];
    size_t ret = 0;
    ssize_t res;

    if (!initialized)
        return -1;

    thread_mutex_lock(&digest_a_lock);
    digest_write(digest_a, &len, sizeof(len));
    copy = digest_copy(digest_a);
    thread_mutex_unlock(&digest_a_lock);

    if (!copy)
        return -1;

    if (digest_read(copy, froma, sizeof(froma)) != sizeof(froma))
        return -1;

    refobject_unref(copy);

    thread_mutex_lock(&digest_b_lock);
    while (ret < len) {
        res = prng_read_block(froma, buffer + ret, len - ret);
        if (res < 0) {
            thread_mutex_unlock(&digest_a_lock);
            return -1;
        }
        ret += res;
    }
    thread_mutex_unlock(&digest_a_lock);

    return ret;
}

int prng_write_file(const char *filename, ssize_t len)
{
    char buffer[BLOCK_LENGTH*16];
    size_t done = 0;
    FILE *file;

    if (len < 0)
        len = 1024;

    file = fopen(filename, "wb");
    if (!file)
        return -1;

    while (done < (size_t)len) {
        size_t todo = (size_t)len < sizeof(buffer) ? (size_t)len : sizeof(buffer);
        ssize_t res = prng_read(buffer, todo);
        if (res < 1) {
            fclose(file);
            return -1;
        }

        if (fwrite(buffer, 1, res, file) != (size_t)res) {
            fclose(file);
            return -1;
        }

        done += res;
    }

    fclose(file);
    return 0;
}

int prng_read_file(const char *filename, ssize_t len)
{
    char buffer[BLOCK_LENGTH*16];
    size_t done = 0;
    FILE *file;

    if (len < 0 || len > 1048576)
        len = 1048576;

    file = fopen(filename, "rb");
    if (!file)
        return -1;

    while (done < (size_t)len) {
        size_t todo = (size_t)len < sizeof(buffer) ? (size_t)len : sizeof(buffer);
        size_t res = fread(buffer, 1, todo, file);

        if (res < 1) {
            fclose(file);
            return 0;
        }

        prng_write(buffer, res);

        done += res;
    }

    fclose(file);
    return 0;
}
