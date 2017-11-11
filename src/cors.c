/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2017, Julien CROUZET <contact@juliencrouzet.fr>
 */

/**
 * Cors handling functions
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <ctype.h>

#include "cfgfile.h"
#include "client.h"
#include "logging.h"

#define CATMODULE "CORS"

static const char* cors_header_names[7] = {
    "access-control-allow-origin",
    "access-control-expose-headers",
    "access-control-max-age",
    "access-control-allow-credentials",
    "access-control-allow-methods",
    "access-control-allow-headers",
    NULL
};

static const char *icy_headers = "icy-br, icy-caps, icy-description, icy-genre, icy-metaint, icy-metadata-interval, icy-name, icy-pub, icy-public, icy-url";

static ice_config_cors_path_t* _find_matching_path(ice_config_cors_path_t  *paths, char *path)
{
    ice_config_cors_path_t *matching_path = paths;

    while(matching_path) {
        if (strncmp(matching_path->base, path, strlen(matching_path->base)) == 0) {
            return matching_path;
        }
        matching_path = matching_path->next;
    }
    return NULL;
}

static int _cors_valid_origin(ice_config_cors_path_t  *path, const char *origin) {
    if (path->forbidden) {
        for (int i = 0; path->forbidden[i]; i++) {
            if (strstr(origin, path->forbidden[i]) == origin) {
                ICECAST_LOG_DEBUG(
                    "Declared origin \"%s\" matches forbidden origin \"%s\", not sending CORS",
                    origin,
                    path->forbidden[i]
                );
                return 0;
            }
        }
    }
    if (path->allowed) {
        for (int i = 0; path->allowed[i]; i++) {
            if ((strlen(path->allowed[i]) == 1) && path->allowed[i][0] == '*') {
                ICECAST_LOG_DEBUG(
                    "All (\"*\") allowed origin for \"%s\", sending CORS",
                    origin
                );
                return 1;
            }
            if (strstr(origin, path->allowed[i]) == origin) {
                ICECAST_LOG_DEBUG(
                    "Declared origin \"%s\" matches allowed origin \"%s\", sending CORS",
                    origin,
                    path->allowed[i]
                );
                return 1;
            }
        }
    }
    ICECAST_LOG_DEBUG(
        "Declared origin \"%s\" does not matches any declared origin, not sending CORS",
        origin
    );
    return 0;
}

static void _add_header(char       **out,
                        size_t      *len,
                        const char  *header_name,
                        const char  *header_value)
{
    int   new_length;
    char *new_out;

    if (!header_name || !header_value || !strlen(header_name)) {
        return;
    }
    new_length = strlen(header_name) + strlen(header_value) + 4;
    new_out = calloc(*len + new_length, sizeof(char));
    if (!new_out) {
        ICECAST_LOG_ERROR("Out of memory while setting CORS header.");
        return;
    }
    snprintf(new_out,
             *len + new_length, 
             "%s%s: %s\r\n",
             *out,
             header_name,
             header_value);
    free(*out);
    *len += new_length;
    *out = new_out;
}


/**
 * Removes an header by its name in current headers list.
 * Header removal is needed to remove any manually added headers
 * added while a forbidden rule is active.
 */
static void _remove_header(char                   **out,
                           size_t                  *len,
                           const char              *header_name)
{
    int header_start[100];
    int header_end[100];
    int current_position = 0;
    int found_count = 0;
    char *new_out;

    if (!*len)
        return;
    while((current_position < (*len -1)) && found_count < 100) {
        char *substr = strcasestr((*out + current_position), header_name);
        char *substr_end;
        if (!substr) {
            break;
        }
        substr_end = strstr(substr, "\r\n");
        if (!substr_end) {
            return;
        }
        header_start[found_count] = substr - *out;
        header_end[found_count] = substr_end - *out + 2;
        current_position = header_end[found_count];
        found_count++;
    }
    if (!found_count) {
        return;
    }
    current_position = 0;
    new_out = calloc(*len + 1, sizeof(char));
    if (!new_out) {
        return;
    }
    free(*out);
    for (int i = 0; i < found_count; i++) {
        while (current_position < header_start[i]) {
            new_out[current_position] = *out[current_position];
        }
        current_position = header_end[i];
    }
    while (current_position < *len) {
        new_out[current_position] = 0;
        current_position++;
    }
    *out = new_out;
    for (int i = 0; i < found_count; i++) {
        *len -= header_end[i] - header_start[i];
    }
    return;
}


static void _add_cors(char                   **out,
                      size_t                  *len,
                      ice_config_cors_path_t  *path,
                      char                    *origin)
{
    _add_header(out, len, "Access-Control-Allow-Origin", origin);
    if (path->exposed_headers) {
        _add_header(out, len, "Access-Control-Expose-Headers", path->exposed_headers);
    } else {
        _add_header(out, len, "Access-Control-Expose-Headers", icy_headers);
    }
    _add_header(out, len, "Access-Control-Max-Age", "3600");
    _add_header(out, len, "Access-Control-Allow-Credentials", "true");
    _add_header(out, len, "Access-Control-Allow-Methods", "GET");
    _add_header(out, len, "Access-Control-Allow-Headers", "icy-metadata");
    return;
}

static void _remove_cors(char **out, size_t *len) {
    for(int i = 0; cors_header_names[i]; i++) {
        _remove_header(out, len, cors_header_names[i]);
    }
    return;
}

void cors_set_headers(char                   **out,
                      size_t                  *len,
                      ice_config_cors_path_t  *paths,
                      struct _client_tag      *client)
{
    char *origin = NULL;
    char *path = (char *)client->parser->uri;
    ice_config_cors_path_t *matching_path;

    if (!paths)
        return;
    if (!(origin = (char *)httpp_getvar(client->parser, "origin")))
        return;
    if (!path)
        return;

    matching_path = _find_matching_path(paths, path);
    if (!matching_path) {
        ICECAST_LOG_DEBUG(
            "Requested path \"%s\" does not matches any declared CORS configured path",
            path
        );
        return;
    }

    ICECAST_LOG_DEBUG(
        "Requested path \"%s\" matches the \"%s\" declared CORS path",
        path,
        matching_path->base
    );

    _remove_cors(out, len);

    if (
        !matching_path->no_cors &&
        _cors_valid_origin(matching_path, origin)
    ) {
        _add_cors(out, len, matching_path, origin);
    }
    return;
}
