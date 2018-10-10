/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2018,      Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

/**
 * Special fast event functions
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <string.h>

#include "common/thread/thread.h"

#include "fastevent.h"

#include "logging.h"
#define CATMODULE "fastevent"

#ifdef FASTEVENT_ENABLED

typedef struct {
    refobject_base_t __base;

    fastevent_type_t type;
    fastevent_cb_t cb;
    fastevent_freecb_t freecb;
    void *userdata;
} fastevent_registration_t;

struct eventrow {
    size_t length;
    size_t used;
    fastevent_registration_t **registrations;
};

static struct eventrow fastevent_registrations[FASTEVENT_TYPE__END];
static rwlock_t fastevent_lock;

static inline struct eventrow * __get_row(fastevent_type_t type)
{
    size_t idx = type;

    if (idx >= FASTEVENT_TYPE__END)
        return NULL;

    return &(fastevent_registrations[idx]);
}

static int __add_to_row(struct eventrow * row, fastevent_registration_t *registration)
{
    fastevent_registration_t **n;

    if (row == NULL)
        return -1;

    /* Check if we need to reallocate row space */
    if (row->length == row->used) {
        n = realloc(row->registrations, sizeof(*n)*(row->length + 4));
        if (n == NULL) {
            ICECAST_LOG_ERROR("Can not allocate row space.");
            return -1;
        }

        row->registrations = n;
        row->length += 4;
    }

    row->registrations[row->used++] = registration;
    return 0;
}

static int __remove_from_row(struct eventrow * row, fastevent_registration_t *registration)
{
    size_t i;

    if (row == NULL)
        return -1;

    for (i = 0; i < row->used; i++) {
        if (row->registrations[i] == registration) {
            memmove(&(row->registrations[i]), &(row->registrations[i+1]), sizeof(*(row->registrations))*(row->used - i - 1));
            row->used--;
            return 0;
        }
    }

    return -1;
}


static void __unregister(refobject_t self, void **userdata)
{
    fastevent_registration_t *registration = REFOBJECT_TO_TYPE(self, fastevent_registration_t *);
    struct eventrow * row;

    (void)userdata;

    thread_rwlock_wlock(&fastevent_lock);
    row = __get_row(registration->type);
    if (__remove_from_row(row, registration) != 0) {
        ICECAST_LOG_ERROR("Can not remove fast event from row. BUG.");
    }
    thread_rwlock_unlock(&fastevent_lock);

    if (registration->freecb)
        registration->freecb(&(registration->userdata));

    if (registration->userdata != NULL)
        free(registration->userdata);
}

int fastevent_initialize(void)
{
    thread_rwlock_create(&fastevent_lock);
    return 0;
}

int fastevent_shutdown(void)
{
    size_t i;

    thread_rwlock_wlock(&fastevent_lock);
    for (i = 0; i < FASTEVENT_TYPE__END; i++) {
        if (fastevent_registrations[i].used) {
            ICECAST_LOG_ERROR("Subsystem shutdown but elements still in use. BUG.");
            continue;
        }

        free(fastevent_registrations[i].registrations);
        fastevent_registrations[i].registrations = NULL;
    }
    thread_rwlock_unlock(&fastevent_lock);
    thread_rwlock_destroy(&fastevent_lock);

    return 0;
}

REFOBJECT_DEFINE_PRIVATE_TYPE(fastevent_registration_t,
        REFOBJECT_DEFINE_TYPE_FREE(__unregister)
        );

refobject_t fastevent_register(fastevent_type_t type, fastevent_cb_t cb, fastevent_freecb_t freecb, void *userdata)
{
    struct eventrow * row;
    fastevent_registration_t *registration;

    if (cb == NULL)
        return REFOBJECT_NULL;

    thread_rwlock_wlock(&fastevent_lock);
    row = __get_row(type);

    if (row == NULL) {
        thread_rwlock_unlock(&fastevent_lock);
        return REFOBJECT_NULL;
    }

    registration = refobject_new__new(fastevent_registration_t, NULL, NULL, NULL);

    if (!registration) {
        thread_rwlock_unlock(&fastevent_lock);
        return REFOBJECT_NULL;
    }

    registration->type = type;
    registration->cb = cb;
    registration->freecb = freecb;
    registration->userdata = userdata;

    if (__add_to_row(row, registration) != 0) {
        thread_rwlock_unlock(&fastevent_lock);
        refobject_unref(REFOBJECT_FROM_TYPE(registration));
        return REFOBJECT_NULL;
    }

    thread_rwlock_unlock(&fastevent_lock);
    return REFOBJECT_FROM_TYPE(registration);
}

void fastevent_emit(fastevent_type_t type, fastevent_flag_t flags, fastevent_datatype_t datatype, ...)
{
    struct eventrow * row;
    va_list ap, apx;
    size_t i;

    ICECAST_LOG_DEBUG("event: type=%i, flags=%i, datatype=%i, ...", (int)type, (int)flags, (int)datatype);

    thread_rwlock_rlock(&fastevent_lock);
    row = __get_row(type);
    if (row == NULL || row->used == 0) {
        thread_rwlock_unlock(&fastevent_lock);
        return;
    }

    va_start(ap, datatype);

    for (i = 0; i < row->used; i++) {
        va_copy(apx, ap);
        row->registrations[i]->cb(row->registrations[i]->userdata, type, flags, datatype, apx);
        va_end(apx);
    }

    thread_rwlock_unlock(&fastevent_lock);

    va_end(ap);
}
#endif
