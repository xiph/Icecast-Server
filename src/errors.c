/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2018,      Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "errors.h"
#include "logging.h"
#define CATMODULE "errors"

// cut -d' ' -f2 x | while read x; do printf "    {.id = %-60s .http_status = xxx,\n     .message = \"\"},\n" "$x",; done
const icecast_error_t __errors[] = {
    {.id = ICECAST_ERROR_ADMIN_DEST_NOT_RUNNING,                        .http_status = 400,
     .message = "Destination not running"},
    {.id = ICECAST_ERROR_ADMIN_METADAT_BADCALL,                         .http_status = 400,
     .message = "illegal metadata call"},
    {.id = ICECAST_ERROR_ADMIN_METADAT_NO_SUCH_ACTION,                  .http_status = 501,
     .message = "No such action"},
    {.id = ICECAST_ERROR_ADMIN_MISSING_PARAMETER,                       .http_status = 400,
     .message = "Missing parameter"},
    {.id = ICECAST_ERROR_ADMIN_missing_parameter,                       .http_status = 400,
     .message = "missing parameter"},
    {.id = ICECAST_ERROR_ADMIN_MOUNT_NOT_ACCEPT_URL_UPDATES,            .http_status = 501,
     .message = "mountpoint will not accept URL updates"},
    {.id = ICECAST_ERROR_ADMIN_NO_SUCH_DESTINATION,                     .http_status = 404,
     .message = "No such destination"},
    {.id = ICECAST_ERROR_ADMIN_ROLEMGN_ADD_NOSYS,                       .http_status = 501,
     .message = "Adding users to role not supported by role"},
    {.id = ICECAST_ERROR_ADMIN_ROLEMGN_DELETE_NOSYS,                    .http_status = 501,
     .message = "Deleting users from role not supported by role"},
    {.id = ICECAST_ERROR_ADMIN_ROLEMGN_ROLE_NOT_FOUND,                  .http_status = 404,
     .message = "Role not found"},
    {.id = ICECAST_ERROR_ADMIN_SOURCE_DOES_NOT_EXIST,                   .http_status = 404,
     .message = "Source does not exist"},
    {.id = ICECAST_ERROR_ADMIN_SOURCE_IS_NOT_AVAILABLE,                 .http_status = 400,
     .message = "Source is not available"},
    {.id = ICECAST_ERROR_ADMIN_SUPPLIED_MOUNTPOINTS_ARE_IDENTICAL,      .http_status = 400,
     .message = "supplied mountpoints are identical"},
    {.id = ICECAST_ERROR_ADMIN_UNRECOGNISED_COMMAND,                    .http_status = 400,
     .message = "unrecognised command"},
    {.id = ICECAST_ERROR_AUTH_BUSY,                                     .http_status = 503,
     .message = "busy, please try again later"},
    {.id = ICECAST_ERROR_CON_CONTENT_TYPE_NOSYS,                        .http_status = 415,
     .message = "Content-type not supported"},
    {.id = ICECAST_ERROR_CON_INTERNAL_FORMAT_ALLOC_ERROR,               .http_status = 500,
     .message = "internal format allocation problem"},
    {.id = ICECAST_ERROR_CON_MISSING_PASS_PARAMETER,                    .http_status = 400 /* XXX */,
     .message = "missing pass parameter"},
    {.id = ICECAST_ERROR_CON_MOUNT_IN_USE,                              .http_status = 409,
     .message = "Mountpoint in use"},
    {.id = ICECAST_ERROR_CON_MOUNTPOINT_NOT_STARTING_WITH_SLASH,        .http_status = 400,
     .message = "source mountpoint not starting with /"},
    {.id = ICECAST_ERROR_CON_NO_CONTENT_TYPE_GIVEN,                     .http_status = 400,
     .message = "No Content-type given"},
    {.id = ICECAST_ERROR_CON_PER_CRED_CLIENT_LIMIT,                     .http_status = 429,
     .message = "Reached limit of concurrent connections on those credentials"},
    {.id = ICECAST_ERROR_CON_rejecting_client_for_whatever_reason,      .http_status = 403 /* XXX */,
     .message = "Rejecting client for whatever reason"},
    {.id = ICECAST_ERROR_CON_SOURCE_CLIENT_LIMIT,                       .http_status = 503,
     .message = "too many sources connected"},
    {.id = ICECAST_ERROR_CON_UNIMPLEMENTED,                             .http_status = 501,
     .message = "Unimplemented"},
    {.id = ICECAST_ERROR_CON_UNKNOWN_REQUEST,                           .http_status = 405,
     .message = "unknown request"},
    {.id = ICECAST_ERROR_CON_UPGRADE_ERROR,                             .http_status = 400 /* XXX */,
     .message = "Can not upgrade protocol"},
    {.id = ICECAST_ERROR_FSERV_FILE_NOT_FOUND,                          .http_status = 404,
     .message = "The file you requested could not be found"},
    {.id = ICECAST_ERROR_FSERV_FILE_NOT_READABLE,                       .http_status = 404 /* XXX */,
     .message = "File not readable"},
    {.id = ICECAST_ERROR_FSERV_REQUEST_RANGE_NOT_SATISFIABLE,           .http_status = 416,
     .message = "Request Range Not Satisfiable"},
    {.id = ICECAST_ERROR_GEN_BUFFER_REALLOC,                            .http_status = 500,
     .message = "Buffer reallocation failed."},
    {.id = ICECAST_ERROR_GEN_CLIENT_LIMIT,                              .http_status = 503,
     .message = "Icecast connection limit reached"},
    {.id = ICECAST_ERROR_GEN_CLIENT_NEEDS_TO_AUTHENTICATE,              .http_status = 401,
     .message = "You need to authenticate"},
    {.id = ICECAST_ERROR_GEN_HEADER_GEN_FAILED,                         .http_status = 500,
     .message = "Header generation failed."},
    {.id = ICECAST_ERROR_GEN_MEMORY_EXHAUSTED,                          .http_status = 503,
     .message = "memory exhausted"},
    {.id = ICECAST_ERROR_SOURCE_MOUNT_UNAVAILABLE,                      .http_status = 404 /* XXX */,
     .message = "Mount unavailable"},
    {.id = ICECAST_ERROR_SOURCE_STREAM_PREPARATION_ERROR,               .http_status = 500 /* XXX */,
     .message = "Stream preparation error"},
    {.id = ICECAST_ERROR_XSLT_PARSE,                                    .http_status = 404 /* XXX */,
     .message = "Could not parse XSLT file"},
    {.id = ICECAST_ERROR_XSLT_problem,                                  .http_status = 500,
     .message = "XSLT problem"}
};

const icecast_error_t * error_get_by_id(int id) {
    size_t i;

    for (i = 0; i < (sizeof(__errors)/sizeof(*__errors)); i++) {
        if (__errors[i].id == id) {
            return &(__errors[i]);
        }
    }

    return NULL;
}

