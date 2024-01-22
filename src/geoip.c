/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2023     , Philipp Schafft <lion@lion.leolix.org>,
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "icecasttypes.h"

#ifdef HAVE_MAXMINDDB
#include <maxminddb.h>
#include <string.h>
#include <errno.h>
#endif

#include <igloo/error.h>
#include <igloo/ro.h>

#include "geoip.h"
#include "global.h"
#include "client.h"
#include "connection.h"
#include "util_string.h"
#include "logging.h"
#define CATMODULE "geoip"

struct geoip_db_tag {
    igloo_ro_full_t __parent;

#ifdef HAVE_MAXMINDDB
    MMDB_s mmdb;
#endif
};

static void geoip_db_free(igloo_ro_t self)
{
#ifdef HAVE_MAXMINDDB
    geoip_db_t *db = igloo_ro_to_type(self, geoip_db_t);

    MMDB_close(&(db->mmdb));
#endif
}

igloo_RO_PUBLIC_TYPE(geoip_db_t, igloo_ro_full_t,
        igloo_RO_TYPEDECL_FREE(geoip_db_free)
        );

#ifdef HAVE_MAXMINDDB
geoip_db_t * geoip_db_new(const char *filename)
{
    geoip_db_t *ret;
    MMDB_s mmdb;
    int status;

    if (!filename) {
        ICECAST_LOG_INFO("No geoip database given");
        return NULL;
    }

    status = MMDB_open(filename, MMDB_MODE_MMAP, &mmdb);
    if (status != MMDB_SUCCESS) {
        if (status == MMDB_IO_ERROR) {
            ICECAST_LOG_ERROR("Cannot open geoip database: %s: %s", MMDB_strerror(status), strerror(errno));
        } else {
            ICECAST_LOG_ERROR("Cannot open geoip database: %s", MMDB_strerror(status));
        }
        return NULL;
    }

    if (igloo_ro_new_raw(&ret, geoip_db_t, igloo_instance) != igloo_ERROR_NONE)
        return NULL;

    ret->mmdb = mmdb;

    ICECAST_LOG_INFO("Loaded geoip database: %s", filename);

    return ret;
}

void geoip_lookup_client(geoip_db_t *self, client_t * client)
{
    int gai_error, mmdb_error;
    MMDB_lookup_result_s result;
    connection_t *con;

    if (!self || !client)
        return;

    if (!client->con && !client->con->ip)
        return;

    con = client->con;

    result = MMDB_lookup_string(&(self->mmdb), client->con->ip, &gai_error, &mmdb_error);

    if (gai_error || mmdb_error != MMDB_SUCCESS)
        return;

    if (result.found_entry) {
        MMDB_entry_data_s entry_data;
        int status;

        status = MMDB_get_value(&result.entry, &entry_data, "country", "iso_code", (const char*)NULL);
        if (status == MMDB_SUCCESS && entry_data.has_data) {
            if (entry_data.type == MMDB_DATA_TYPE_UTF8_STRING) {
                if (entry_data.data_size < sizeof(con->geoip.iso_3166_1_alpha_2)) {
                    memcpy(con->geoip.iso_3166_1_alpha_2, entry_data.utf8_string, entry_data.data_size);
                    con->geoip.iso_3166_1_alpha_2[entry_data.data_size] = 0;
                    util_strtolower(con->geoip.iso_3166_1_alpha_2);
                    ICECAST_LOG_DINFO("FOUND: <%zu> <%H>", (size_t)entry_data.data_size, con->geoip.iso_3166_1_alpha_2);
                }
            }
        }

        status = MMDB_get_value(&result.entry, &entry_data, "location", "latitude", (const char*)NULL);
        if (status == MMDB_SUCCESS && entry_data.has_data && entry_data.type == MMDB_DATA_TYPE_DOUBLE) {
            con->geoip.latitude = entry_data.double_value;
            con->geoip.have_latitude = true;
        }

        status = MMDB_get_value(&result.entry, &entry_data, "location", "longitude", (const char*)NULL);
        if (status == MMDB_SUCCESS && entry_data.has_data && entry_data.type == MMDB_DATA_TYPE_DOUBLE) {
            con->geoip.longitude = entry_data.double_value;
            con->geoip.have_longitude = true;
        }
    }
}
#endif
