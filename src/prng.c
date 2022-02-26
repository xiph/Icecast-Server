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

#include <igloo/prng.h>

#include "common/thread/thread.h"
#include "common/timing/timing.h"

#include "prng.h"
#include "global.h"
#include "digest.h"
#include "cfgfile.h"

#include "logging.h"
#define CATMODULE "prng"

#define BLOCK_LENGTH                (512/8)

#define SEEDING_FACTOR              128
#define SEEDING_MAX_BEFORE_RESEED   32768

static int initialized = 0;

static void prng_initial_seed(void)
{
#ifdef HAVE_OPENSSL
    char buffer[1024];
    const char *filename;
#endif

#ifdef HAVE_OPENSSL
    filename = RAND_file_name(buffer, sizeof(buffer));
    if (filename)
        RAND_load_file(filename, -1);
    ERR_get_error(); // clear error if any
#endif
}

static void prng_cross_seed(void)
{
    char buffer[1024];
    ssize_t len;

#ifdef HAVE_OPENSSL
    if (RAND_bytes((unsigned char*)buffer, sizeof(buffer)) == 1) {
        igloo_prng_write(igloo_instance, buffer, sizeof(buffer), -1, igloo_PRNG_FLAG_NONE);
    } else {
        ERR_get_error(); // clear error
    }
    len = igloo_prng_read(igloo_instance, buffer, sizeof(buffer), igloo_PRNG_FLAG_NONE);
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
                    igloo_prng_read_file(igloo_instance, seed->filename, seed->size, -1, igloo_PRNG_FLAG_NONE);
                break;
            case PRNG_SEED_TYPE_DEVICE:
                igloo_prng_read_file(igloo_instance, seed->filename, seed->size, -1, igloo_PRNG_FLAG_NONE);
                break;
            case PRNG_SEED_TYPE_STATIC:
                igloo_prng_write(igloo_instance, seed->filename, strlen(seed->filename), -1, igloo_PRNG_FLAG_NONE);
                break;
            case PRNG_SEED_TYPE_PROFILE:
                if (strcmp(seed->filename, "linux") == 0) {
                    igloo_prng_read_file(igloo_instance, "/proc/sys/kernel/random/uuid", -1, -1, igloo_PRNG_FLAG_NONE);
                }
                if (strcmp(seed->filename, "linux") == 0 || strcmp(seed->filename, "bsd") == 0) {
                    igloo_prng_read_file(igloo_instance, "/dev/urandom", 64, -1, igloo_PRNG_FLAG_NONE);
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

    initialized = 1;
    prng_initial_seed();
    prng_cross_seed();
}

void prng_shutdown(void)
{
    if (!initialized)
        return;

    initialized = 0;
}

void prng_configure(ice_config_t *config)
{
    if (!initialized)
        return;

    igloo_prng_write(igloo_instance, config->location,  strlen(config->location), -1, igloo_PRNG_FLAG_NONE);
    igloo_prng_write(igloo_instance, config->admin,     strlen(config->admin),    -1, igloo_PRNG_FLAG_NONE);
    igloo_prng_write(igloo_instance, config->hostname,  strlen(config->hostname), -1, igloo_PRNG_FLAG_NONE);
    igloo_prng_write(igloo_instance, config, sizeof(*config), -1, igloo_PRNG_FLAG_NONE);
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
            igloo_prng_write_file(igloo_instance, seed->filename, seed->size, igloo_PRNG_FLAG_NONE);
        }
        seed = seed->next;
    }
    config_release_config();
}
