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
 * Copyright 2015,      Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include "cfgfile.h"

void _dump_config(ice_config_t *config);

int main(void)
{
    ice_config_t *config;

    config_initialize();

    config_parse_file("icecast.xml");

    config = config_get_config_unlocked();

    _dump_config(config);

    config_shutdown();

    return 0;
}

void _dump_config(ice_config_t *config)
{
    printf("-----\n");
    printf("location = %s\n", config->location);
    printf("admin = %s\n", config->admin);
    printf("client_limit = %d\n", config->client_limit);
    printf("source_limit = %d\n", config->source_limit);
    printf("threadpool_size = %d\n", config->threadpool_size);
    printf("client_timeout = %d\n", config->client_timeout);
    printf("source_password = %s\n", config->source_password);
    printf("hostname = %s\n", config->hostname);
    printf("port = %d\n", config->port);
    printf("bind_address = %s\n", config->bind_address);
    printf("base_dir = %s\n", config->base_dir);
    printf("log_dir = %s\n", config->log_dir);
    printf("access_log = %s\n", config->access_log);
    printf("error_log = %s\n", config->error_log);
    printf("loglevel = %d\n", config->loglevel);
    printf("-----\n");
}





