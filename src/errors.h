/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2018,      Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>
 */

#ifndef __ERRORS_H__
#define __ERRORS_H__

#define ICECAST_ERROR_ADMIN_DEST_NOT_RUNNING                   1
#define ICECAST_ERROR_ADMIN_METADAT_BADCALL                    2
#define ICECAST_ERROR_ADMIN_METADAT_NO_SUCH_ACTION             3
#define ICECAST_ERROR_ADMIN_MISSING_PARAMETER                  4
#define ICECAST_ERROR_ADMIN_missing_parameter                  5 /* what is this? */
#define ICECAST_ERROR_ADMIN_MOUNT_NOT_ACCEPT_URL_UPDATES       6
#define ICECAST_ERROR_ADMIN_NO_SUCH_DESTINATION                7
#define ICECAST_ERROR_ADMIN_ROLEMGN_ADD_NOSYS                  8
#define ICECAST_ERROR_ADMIN_ROLEMGN_DELETE_NOSYS               9
#define ICECAST_ERROR_ADMIN_ROLEMGN_ROLE_NOT_FOUND             10
#define ICECAST_ERROR_ADMIN_SOURCE_DOES_NOT_EXIST              11
#define ICECAST_ERROR_ADMIN_SOURCE_IS_NOT_AVAILABLE            12
#define ICECAST_ERROR_ADMIN_SUPPLIED_MOUNTPOINTS_ARE_IDENTICAL 13
#define ICECAST_ERROR_ADMIN_UNRECOGNISED_COMMAND               14
#define ICECAST_ERROR_AUTH_BUSY                                15
#define ICECAST_ERROR_CON_CONTENT_TYPE_NOSYS                   16
#define ICECAST_ERROR_CON_INTERNAL_FORMAT_ALLOC_ERROR          17
#define ICECAST_ERROR_CON_MISSING_PASS_PARAMETER               18
#define ICECAST_ERROR_CON_MOUNT_IN_USE                         19
#define ICECAST_ERROR_CON_MOUNTPOINT_NOT_STARTING_WITH_SLASH   20
#define ICECAST_ERROR_CON_NO_CONTENT_TYPE_GIVEN                21
#define ICECAST_ERROR_CON_PER_CRED_CLIENT_LIMIT                22
#define ICECAST_ERROR_CON_rejecting_client_for_whatever_reason 23 /* ??? */
#define ICECAST_ERROR_CON_SOURCE_CLIENT_LIMIT                  24
#define ICECAST_ERROR_CON_UNIMPLEMENTED                        25
#define ICECAST_ERROR_CON_UNKNOWN_REQUEST                      26
#define ICECAST_ERROR_CON_UPGRADE_ERROR                        27
#define ICECAST_ERROR_FSERV_FILE_NOT_FOUND                     28
#define ICECAST_ERROR_FSERV_FILE_NOT_READABLE                  29
#define ICECAST_ERROR_FSERV_REQUEST_RANGE_NOT_SATISFIABLE      30
#define ICECAST_ERROR_GEN_BUFFER_REALLOC                       31
#define ICECAST_ERROR_GEN_CLIENT_LIMIT                         32
#define ICECAST_ERROR_GEN_CLIENT_NEEDS_TO_AUTHENTICATE         33
#define ICECAST_ERROR_GEN_HEADER_GEN_FAILED                    34
#define ICECAST_ERROR_GEN_MEMORY_EXHAUSTED                     35
#define ICECAST_ERROR_SOURCE_MOUNT_UNAVAILABLE                 36
#define ICECAST_ERROR_SOURCE_STREAM_PREPARATION_ERROR          37
#define ICECAST_ERROR_XSLT_PARSE                               38
#define ICECAST_ERROR_XSLT_problem                             39

struct icecast_error_tag {
    const int id;
    const int http_status;
    const char *uuid;
    const char *message;
};

typedef struct icecast_error_tag icecast_error_t;

const icecast_error_t * error_get_by_id(int id);

#endif  /* __ERRORS_H__ */
