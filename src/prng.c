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

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>

#ifdef HAVE_UNAME
#include <sys/utsname.h>
#endif

#ifdef HAVE_OPENSSL
#include <openssl/rand.h>
#include <openssl/err.h>
#endif

#include "common/thread/thread.h"
#include "common/timing/timing.h"

#include "prng.h"
#include "digest.h"
#include "cfgfile.h"

#include "logging.h"
#define CATMODULE "prng"

#define BLOCK_LENGTH                (512/8)

#define SEEDING_FACTOR              128
#define SEEDING_MAX_BEFORE_RESEED   32768

static int initialized = 0;
static mutex_t digest_a_lock;
static mutex_t digest_b_lock;
static digest_t * digest_a; // protected by digest_a_lock
static digest_t * digest_b; // protected by digest_h_lock
static size_t before_reseed; // protected by digest_a_lock

static void prng_initial_seed(void)
{
    struct {
        int debian;
        uint64_t t;
#ifdef HAVE_UNAME
        struct utsname utsname;
#endif
#ifdef HAVE_SETUID
        uid_t uid;
        pid_t pid;
        pid_t ppid;
#endif
    } seed;

    memset(&seed, 0, sizeof(seed));

    seed.debian = 4;
    seed.t = timing_get_time();
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

static void prng_cross_seed(void)
{
    char buffer[1024];
    ssize_t len;

#ifdef HAVE_OPENSSL
    if (RAND_bytes((unsigned char*)buffer, sizeof(buffer)) == 1) {
        prng_write(buffer, sizeof(buffer));
    } else {
        ERR_get_error(); // clear error
    }
    len = prng_read(buffer, sizeof(buffer));
    if (len > 0)
        RAND_add(buffer, len, len/10.);
#endif
}

static void prng_read_seeds(prng_seed_config_t *seed, int configure_time)
{
    while (seed) {
        switch (seed->type) {
            case PRNG_SEED_TYPE_READ_ONCE:
            case PRNG_SEED_TYPE_READ_WRITE:
                if (configure_time)
                    prng_read_file(seed->filename, seed->size);
                break;
            case PRNG_SEED_TYPE_DEVICE:
                prng_read_file(seed->filename, seed->size);
                break;
            case PRNG_SEED_TYPE_STATIC:
                prng_write(seed->filename, strlen(seed->filename));
                break;
            case PRNG_SEED_TYPE_PROFILE:
                if (strcmp(seed->filename, "linux") == 0) {
                    if (configure_time) {
                        prng_read_file("/proc/sys/kernel/random/boot_id", -1);
                        prng_read_file("/etc/machine-id", -1);
                    }
                    prng_read_file("/proc/sys/kernel/random/uuid", -1);
                }
                if (strcmp(seed->filename, "linux") == 0 || strcmp(seed->filename, "bsd") == 0) {
                    prng_read_file("/dev/urandom", 64);
                }
                break;
        }
        seed = seed->next;
    }
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
    prng_cross_seed();
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

    prng_read_seeds(config->prng_seed, 1);
    prng_cross_seed();
}

void prng_deconfigure(void)
{
    ice_config_t *config;
    prng_seed_config_t *seed;

    if (!initialized)
        return;

    config = config_get_config();
    seed = config->prng_seed;
    while (seed) {
        if (seed->type == PRNG_SEED_TYPE_READ_WRITE) {
            prng_write_file(seed->filename, seed->size);
        }
        seed = seed->next;
    }
    config_release_config();
}

void prng_auto_reseed(void)
{
    int need_seeding;
    ice_config_t *config;

    thread_mutex_lock(&digest_a_lock);
    need_seeding = before_reseed == 0;
    thread_mutex_unlock(&digest_a_lock);

    if (!need_seeding)
        return;

    config = config_get_config();
    prng_read_seeds(config->prng_seed, 0);
    config_release_config();
    prng_cross_seed();
}

void prng_write(const void *buffer, size_t len)
{
    if (!initialized)
        return;

    thread_mutex_lock(&digest_a_lock);
    digest_write(digest_a, buffer, len);
    digest_write(digest_a, &len, sizeof(len));
    before_reseed += len * SEEDING_FACTOR;
    if (before_reseed > SEEDING_MAX_BEFORE_RESEED)
        before_reseed = SEEDING_MAX_BEFORE_RESEED;
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
    if (before_reseed > len) {
        before_reseed -= len;
    } else {
        before_reseed = 0;
    }
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
            thread_mutex_unlock(&digest_b_lock);
            return -1;
        }
        ret += res;
    }
    thread_mutex_unlock(&digest_b_lock);

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
    int fh;

    if (len < 0 || len > 1048576)
        len = 1048576;

#ifdef O_CLOEXEC
    fh = open(filename, O_RDONLY|O_CLOEXEC, 0);
#else
    fh = open(filename, O_RDONLY, 0);
#endif
    if (fh < 0)
        return -1;

    while (done < (size_t)len) {
        size_t todo = (size_t)len < sizeof(buffer) ? (size_t)len : sizeof(buffer);
        size_t res = read(fh, buffer, todo);

        if (res < 1) {
            close(fh);
            return 0;
        }

        prng_write(buffer, res);

        done += res;
    }

    close(fh);
    return 0;
}
