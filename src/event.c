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

#include "event.h"
#include "cfgfile.h"
#include "yp.h"
#include "source.h"

#include "refbuf.h"
#include "client.h"
#include "logging.h"
#include "slave.h"
#include "fserve.h"
#include "stats.h"

#define CATMODULE "event"

void event_config_read(void *arg)
{
    int ret;
    ice_config_t *config;
    ice_config_t new_config;
    /* reread config file */

    config = config_grab_config(); /* Both to get the lock, and to be able
                                     to find out the config filename */
    xmlSetGenericErrorFunc ("config", log_parse_failure);
    ret = config_parse_file(config->config_filename, &new_config);
    if(ret < 0) {
        ICECAST_LOG_ERROR("Error parsing config, not replacing existing config");
        switch(ret) {
            case CONFIG_EINSANE:
                ICECAST_LOG_ERROR("Config filename null or blank");
                break;
            case CONFIG_ENOROOT:
                ICECAST_LOG_ERROR("Root element not found in %s", config->config_filename);
                break;
            case CONFIG_EBADROOT:
                ICECAST_LOG_ERROR("Not an icecast2 config file: %s",
                        config->config_filename);
                break;
            default:
                ICECAST_LOG_ERROR("Parse error in reading %s", config->config_filename);
                break;
        }
        config_release_config();
    }
    else {
        config_clear(config);
        config_set_config(&new_config);
        config = config_get_config_unlocked();
        restart_logging (config);
        yp_recheck_config (config);
        fserve_recheck_mime_types (config);
        stats_global (config);
        config_release_config();
        slave_update_all_mounts();
    }
}

