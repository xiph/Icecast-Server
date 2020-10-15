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

#include <strings.h>

#include "errors.h"
#include "logging.h"
#define CATMODULE "errors"

// cut -d' ' -f2 x | while read x; do printf "    {.id = %-60s .http_status = xxx,\n     .message = \"\"},\n" "$x",; done
static const icecast_error_t __errors[] = {
    {.id = ICECAST_ERROR_ADMIN_DEST_NOT_RUNNING,                        .http_status = 400,
     .uuid = "52735a81-16fe-4d7e-9984-5aed8a941055",
     .message = "Destination not running"},
    {.id = ICECAST_ERROR_ADMIN_METADAT_BADCALL,                         .http_status = 400,
     .uuid = "85d33e67-5c4e-4511-b4fa-3ca69ccd03de",
     .message = "illegal metadata call"},
    {.id = ICECAST_ERROR_ADMIN_METADAT_NO_SUCH_ACTION,                  .http_status = 501,
     .uuid = "14f4d814-98d9-468c-8a0b-ba5e74c9d771",
     .message = "No such action"},
    {.id = ICECAST_ERROR_ADMIN_MISSING_PARAMETER,                       .http_status = 400,
     .uuid = "cb11dc71-6149-454c-8d4e-47a3af26b03a",
     .message = "Missing parameter"},
    {.id = ICECAST_ERROR_ADMIN_missing_parameter,                       .http_status = 400,
     .uuid = "8be9ef0a-2b32-450c-aec9-a414ca0c074c",
     .message = "missing parameter"},
    {.id = ICECAST_ERROR_ADMIN_MOUNT_NOT_ACCEPT_URL_UPDATES,            .http_status = 501,
     .uuid = "3bed51bb-a10f-4af3-9965-4e67181de7d6",
     .message = "mountpoint will not accept URL updates"},
    {.id = ICECAST_ERROR_ADMIN_NO_SUCH_DESTINATION,                     .http_status = 404,
     .uuid = "c5f1ee06-46a0-4697-9f01-6e9fc333d555",
     .message = "No such destination"},
    {.id = ICECAST_ERROR_ADMIN_ROLEMGN_ADD_NOSYS,                       .http_status = 501,
     .uuid = "7e1a8426-2ae1-4a6b-bfd9-59d8f8153021",
     .message = "Adding users to role not supported by role"},
    {.id = ICECAST_ERROR_ADMIN_ROLEMGN_DELETE_NOSYS,                    .http_status = 501,
     .uuid = "367fbad1-389e-4292-bba8-c97984e616cc",
     .message = "Deleting users from role not supported by role"},
    {.id = ICECAST_ERROR_ADMIN_ROLEMGN_ROLE_NOT_FOUND,                  .http_status = 404,
     .uuid = "59fe9c81-8c34-49ff-800f-7ec42ea498be",
     .message = "Role not found"},
    {.id = ICECAST_ERROR_ADMIN_SOURCE_DOES_NOT_EXIST,                   .http_status = 404,
     .uuid = "2f51a026-02e4-4fe4-bf9d-cc16557b3b65",
     .message = "Source does not exist"},
    {.id = ICECAST_ERROR_ADMIN_SOURCE_IS_NOT_AVAILABLE,                 .http_status = 400,
     .uuid = "00b9d977-f41d-455f-820f-6d457dffb246",
     .message = "Source is not available"},
    {.id = ICECAST_ERROR_ADMIN_SUPPLIED_MOUNTPOINTS_ARE_IDENTICAL,      .http_status = 400,
     .uuid = "4be9a010-7a3f-44e4-b74d-3c6d9c4f7236",
     .message = "supplied mountpoints are identical"},
    {.id = ICECAST_ERROR_ADMIN_UNRECOGNISED_COMMAND,                    .http_status = 400,
     .uuid = "811bddac-5be5-4580-9cde-7b849e66dfe5",
     .message = "unrecognised command"},
    {.id = ICECAST_ERROR_AUTH_BUSY,                                     .http_status = 503,
     .uuid = "26708754-8f98-4191-81d1-7fb7246200d6",
     .message = "busy, please try again later"},
    {.id = ICECAST_ERROR_CON_CONTENT_TYPE_NOSYS,                        .http_status = 415,
     .uuid = "f684ad3c-513b-4d87-9a66-424788bc6adb",
     .message = "Content-type not supported"},
    {.id = ICECAST_ERROR_CON_INTERNAL_FORMAT_ALLOC_ERROR,               .http_status = 500,
     .uuid = "47a4b11b-5d2a-46e2-8948-942e7b0af3e6",
     .message = "internal format allocation problem"},
    {.id = ICECAST_ERROR_CON_MISSING_PASS_PARAMETER,                    .http_status = 400 /* XXX */,
     .uuid = "b59c3a05-e2b1-4a14-8798-bbe1ae46603b",
     .message = "missing pass parameter"},
    {.id = ICECAST_ERROR_CON_MOUNT_IN_USE,                              .http_status = 409,
     .uuid = "c5724467-5f85-48c7-b45a-915c3150c292",
     .message = "Mountpoint in use"},
    {.id = ICECAST_ERROR_CON_MOUNTPOINT_NOT_STARTING_WITH_SLASH,        .http_status = 400,
     .uuid = "1ae45ead-40fc-4de2-b56f-e54d3247f2ee",
     .message = "source mountpoint not starting with /"},
    {.id = ICECAST_ERROR_CON_NO_CONTENT_TYPE_GIVEN,                     .http_status = 400,
     .uuid = "2cd86778-ac30-49e7-a108-26d627a7923b",
     .message = "No Content-type given"},
    {.id = ICECAST_ERROR_CON_PER_CRED_CLIENT_LIMIT,                     .http_status = 429,
     .uuid = "9c72c1ec-f638-4d33-a077-6acbbff25317",
     .message = "Reached limit of concurrent connections on those credentials"},
    {.id = ICECAST_ERROR_CON_SOURCE_CLIENT_LIMIT,                       .http_status = 503,
     .uuid = "c770182d-c854-422a-a8e5-7142689234a3",
     .message = "too many sources connected"},
    {.id = ICECAST_ERROR_CON_UNIMPLEMENTED,                             .http_status = 501,
     .uuid = "58ce6cb4-72b4-49da-8ad2-feaf775bc61e",
     .message = "Unimplemented"},
    {.id = ICECAST_ERROR_CON_UNKNOWN_REQUEST,                           .http_status = 405,
     .uuid = "78f590cc-8812-40d5-a4ef-17344ab75b35",
     .message = "unknown request"},
    {.id = ICECAST_ERROR_CON_UPGRADE_ERROR,                             .http_status = 400 /* XXX */,
     .uuid = "ec16f654-f262-415f-ab91-95703ae33704",
     .message = "Can not upgrade protocol"},
    {.id = ICECAST_ERROR_CON_MOUNT_NO_FOR_DIRECT_ACCESS,                .http_status = 400 /* XXX */,
     .uuid = "652548c6-2a7d-4c73-a1c5-e53759032bd1",
     .message = "Mountpoint is not available for direct access"},
    {.id = ICECAST_ERROR_FSERV_FILE_NOT_FOUND,                          .http_status = 404,
     .uuid = "18c32b43-0d8e-469d-b434-10133cdd06ad",
     .message = "The file you requested could not be found"},
    {.id = ICECAST_ERROR_FSERV_FILE_NOT_READABLE,                       .http_status = 404 /* XXX */,
     .uuid = "c883d55d-fb41-4f4c-8800-563f5542f51d",
     .message = "File not readable"},
    {.id = ICECAST_ERROR_FSERV_REQUEST_RANGE_NOT_SATISFIABLE,           .http_status = 416,
     .uuid = "5874cc51-770b-42b5-82d2-737b2b406b30",
     .message = "Request Range Not Satisfiable"},
    {.id = ICECAST_ERROR_GEN_BUFFER_REALLOC,                            .http_status = 500,
     .uuid = "cda8203e-f237-4090-8d43-544efdd6295c",
     .message = "Buffer reallocation failed."},
    {.id = ICECAST_ERROR_GEN_CLIENT_LIMIT,                              .http_status = 503,
     .uuid = "87fd3e61-6702-4473-b506-f616d27a142f",
     .message = "Icecast connection limit reached"},
    {.id = ICECAST_ERROR_GEN_CLIENT_NEEDS_TO_AUTHENTICATE,              .http_status = 401,
     .uuid = "25387198-0643-4577-9139-7c4f24f59d4a",
     .message = "You need to authenticate"},
    {.id = ICECAST_ERROR_GEN_HEADER_GEN_FAILED,                         .http_status = 500,
     .uuid = "a8b3c3fe-cb87-45fe-9a9d-ee4c2075d43a",
     .message = "Header generation failed."},
    {.id = ICECAST_ERROR_GEN_MEMORY_EXHAUSTED,                          .http_status = 503,
     .uuid = "18411e73-713e-4910-b7e4-52a2e324b4e0",
     .message = "memory exhausted"},
    {.id = ICECAST_ERROR_SOURCE_MOUNT_UNAVAILABLE,                      .http_status = 404 /* XXX */,
     .uuid = "88d06875-fcf2-4417-84af-05866c97745c",
     .message = "Mount unavailable"},
    {.id = ICECAST_ERROR_SOURCE_STREAM_PREPARATION_ERROR,               .http_status = 500 /* XXX */,
     .uuid = "9e50d94d-f03d-4515-8216-577bf8e9f70d",
     .message = "Stream preparation error"},
    {.id = ICECAST_ERROR_SOURCE_MAX_LISTENERS,                          .http_status = 503,
     .uuid = "df147168-baaa-4959-82a4-746a1232927d",
     .message = "Maximum listeners reached for this source"},
    {.id = ICECAST_ERROR_XSLT_PARSE,                                    .http_status = 404 /* XXX */,
     .uuid = "f86b5b28-c1f8-49f6-a4cd-a18e2a6a44fd",
     .message = "Could not parse XSLT file"},
    {.id = ICECAST_ERROR_XSLT_problem,                                  .http_status = 500,
     .uuid = "d3c6e4b3-7d6e-4191-a81b-970273067ae3",
     .message = "XSLT problem"},
    {.id = ICECAST_ERROR_RECURSIVE_ERROR,                               .http_status = 500,
     .uuid = "13489d5c-eae6-4bf3-889e-ec1fa9a9b9ac",
     .message = "Recursive error"}
};

const icecast_error_t * error_get_by_id(icecast_error_id_t id) {
    size_t i;

    for (i = 0; i < (sizeof(__errors)/sizeof(*__errors)); i++) {
        if (__errors[i].id == id) {
            return &(__errors[i]);
        }
    }

    return NULL;
}

const icecast_error_t * error_get_by_uuid(const char *uuid)
{
    size_t i;

    for (i = 0; i < (sizeof(__errors)/sizeof(*__errors)); i++) {
        if (strcasecmp(__errors[i].uuid, uuid) == 0) {
            return &(__errors[i]);
        }
    }

    return NULL;
}
