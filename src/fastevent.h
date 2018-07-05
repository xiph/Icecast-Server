/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2018,      Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

#ifndef __FASTEVENT_H__
#define __FASTEVENT_H__

/* Add all conditions when to enable fast events here. */
#if 1
#define FASTEVENT_ENABLED
#endif

#include <stdarg.h>
#include <refobject.h>

typedef enum {
    FASTEVENT_TYPE_SLOWEVENT = 0,
    FASTEVENT_TYPE_CONNECTION_CREATE,
    FASTEVENT_TYPE_CONNECTION_DESTROY,
    FASTEVENT_TYPE_CONNECTION_READ,
    FASTEVENT_TYPE_CONNECTION_PUTBACK,
    FASTEVENT_TYPE_CONNECTION_WRITE,
    FASTEVENT_TYPE_CLIENT_CREATE,
    FASTEVENT_TYPE_CLIENT_DESTROY,
    FASTEVENT_TYPE_CLIENT_READ,
    FASTEVENT_TYPE_CLIENT_WRITE,
    FASTEVENT_TYPE_CLIENT_READ_BODY,
    FASTEVENT_TYPE_CLIENT_READY_FOR_AUTH,
    FASTEVENT_TYPE_CLIENT_AUTHED,
    FASTEVENT_TYPE_CLIENT_SEND_RESPONSE,
    FASTEVENT_TYPE__END /* must be last element */
} fastevent_type_t;

typedef enum {
    FASTEVENT_DATATYPE_NONE = 0,
    FASTEVENT_DATATYPE_EVENT,
    FASTEVENT_DATATYPE_CLIENT,
    FASTEVENT_DATATYPE_CONNECTION,
    FASTEVENT_DATATYPE_OBR,             /* Object, const void *Buffer, size_t Request_length */
    FASTEVENT_DATATYPE_OBRD             /* Object, const void *Buffer, size_t Request_length, ssize_t Done_length */
} fastevent_datatype_t;

typedef int fastevent_flag_t;
#define FASTEVENT_FLAG_NONE                     ((fastevent_flag_t)0x0000)
#define FASTEVENT_FLAG_MODIFICATION_ALLOWED     ((fastevent_flag_t)0x0001)

typedef void (*fastevent_cb_t)(const void *userdata, fastevent_type_t type, fastevent_flag_t flags, fastevent_datatype_t datatype, va_list ap);
typedef void (*fastevent_freecb_t)(void **userdata);

#ifdef FASTEVENT_ENABLED
int fastevent_initialize(void);
int fastevent_shutdown(void);
refobject_t fastevent_register(fastevent_type_t type, fastevent_cb_t cb, fastevent_freecb_t freecb, void *userdata);
void fastevent_emit(fastevent_type_t type, fastevent_flag_t flags, fastevent_datatype_t datatype, ...);
#else
#define fastevent_initialize() 0
#define fastevent_shutdown() 0
#define fastevent_register(type,cb,freecb,userdata) REFOBJECT_NULL
#define fastevent_emit(type,flags,datatype,...)
#endif

#endif
