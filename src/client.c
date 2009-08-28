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

/* client.c
**
** client interface implementation
**
*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <string.h>

#include "thread/thread.h"
#include "avl/avl.h"
#include "httpp/httpp.h"
#include "timing/timing.h"

#include "cfgfile.h"
#include "connection.h"
#include "refbuf.h"
#include "format.h"
#include "stats.h"
#include "fserve.h"

#include "client.h"
#include "logging.h"
#include "slave.h"
#include "global.h"

#undef CATMODULE
#define CATMODULE "client"

int worker_count;

/* Return client_t ready for use. The provided socket can be SOCK_ERROR to
 * allocate a dummy client_t.  Must be called with global lock held.
 */
client_t *client_create (sock_t sock)
{
    client_t *client = calloc (1, sizeof (client_t));

    if (sock != SOCK_ERROR)
    {
        refbuf_t *r;
        if (connection_init (&client->connection, sock) < 0)
        {
            free (client);
            return NULL;
        }
        r = refbuf_new (PER_CLIENT_REFBUF_SIZE);
        r->len = 0;
        client->shared_data = r;
        client->flags |= CLIENT_ACTIVE;
    }
    global.clients++;
    stats_event_args (NULL, "clients", "%d", global.clients);
    return client;
}


void client_destroy(client_t *client)
{
    if (client == NULL)
        return;

    if (client->worker)
    {
        WARN0 ("client still on worker thread");
        return;
    }
    /* release the buffer now, as the buffer could be on the source queue
     * and may of disappeared after auth completes */
    if (client->refbuf)
    {
        refbuf_release (client->refbuf);
        client->refbuf = NULL;
    }

    if (client->flags & CLIENT_AUTHENTICATED)
        DEBUG1 ("client still in auth \"%s\"", httpp_getvar (client->parser, HTTPP_VAR_URI));

    /* write log entry if ip is set (some things don't set it, like outgoing 
     * slave requests
     */
    if (client->respcode && client->parser)
        logging_access(client);

    connection_close (&client->connection);
    if (client->parser)
        httpp_destroy (client->parser);

    global_lock ();
    global.clients--;
    stats_event_args (NULL, "clients", "%d", global.clients);
    config_clear_listener (client->server_conn);
    global_unlock ();

    /* we need to free client specific format data (if any) */
    if (client->free_client_data)
        client->free_client_data (client);

    free(client->username);
    free(client->password);

    free(client);
}


/* helper function for reading data from a client */
int client_read_bytes (client_t *client, void *buf, unsigned len)
{
    int (*con_read)(struct connection_tag *handle, void *buf, size_t len) = connection_read;
    int bytes;

    if (client->refbuf && client->pos < client->refbuf->len)
    {
        unsigned remaining = client->refbuf->len - client->pos;
        if (remaining > len)
            remaining = len;
        memcpy (buf, client->refbuf->data + client->pos, remaining);
        client->pos += remaining;
        return remaining;
    }
#ifdef HAVE_OPENSSL
    if (client->connection.ssl)
        con_read = connection_read_ssl;
#endif
    bytes = con_read (&client->connection, buf, len);

    if (bytes == -1 && client->connection.error)
        DEBUG0 ("reading from connection has failed");

    return bytes;
}


void client_send_302(client_t *client, const char *location)
{
    client_set_queue (client, NULL);
    client->refbuf = refbuf_new (PER_CLIENT_REFBUF_SIZE);
    snprintf (client->refbuf->data, PER_CLIENT_REFBUF_SIZE,
            "HTTP/1.0 302 Temporarily Moved\r\n"
            "Content-Type: text/html\r\n"
            "Location: %s\r\n\r\n"
            "Moved <a href=\"%s\">here</a>\r\n", location, location);
    client->respcode = 302;
    client->refbuf->len = strlen (client->refbuf->data);
    fserve_setup_client (client, NULL);
}


void client_send_400(client_t *client, char *message) {
    client_set_queue (client, NULL);
    client->refbuf = refbuf_new (PER_CLIENT_REFBUF_SIZE);
    snprintf (client->refbuf->data, PER_CLIENT_REFBUF_SIZE,
            "HTTP/1.0 400 Bad Request\r\n"
            "Content-Type: text/html\r\n\r\n"
            "<b>%s</b>\r\n", message);
    client->respcode = 400;
    client->refbuf->len = strlen (client->refbuf->data);
    fserve_setup_client (client, NULL);
}


void client_send_401 (client_t *client, const char *realm)
{
    ice_config_t *config = config_get_config ();

    if (realm == NULL)
        realm = config->server_id;

    client_set_queue (client, NULL);
    client->refbuf = refbuf_new (500);
    snprintf (client->refbuf->data, 500,
            "HTTP/1.0 401 Authentication Required\r\n"
            "WWW-Authenticate: Basic realm=\"%s\"\r\n"
            "\r\n"
            "You need to authenticate\r\n", realm);
    config_release_config();
    client->respcode = 401;
    client->refbuf->len = strlen (client->refbuf->data);
    fserve_setup_client (client, NULL);
}


void client_send_403(client_t *client, const char *reason)
{
    if (reason == NULL)
        reason = "Forbidden";
    client_set_queue (client, NULL);
    client->refbuf = refbuf_new (PER_CLIENT_REFBUF_SIZE);
    snprintf (client->refbuf->data, PER_CLIENT_REFBUF_SIZE,
            "HTTP/1.0 403 %s\r\n"
            "Content-Type: text/html\r\n\r\n", reason);
    client->respcode = 403;
    client->refbuf->len = strlen (client->refbuf->data);
    fserve_setup_client (client, NULL);
}

void client_send_403redirect (client_t *client, const char *mount, const char *reason)
{
    if (redirect_client (mount, client))
        return;
    client_send_403 (client, reason);
}


void client_send_404(client_t *client, const char *message)
{
    if (client->worker == NULL)   /* client is not on any worker now */
    {
        client_destroy (client);
        return;
    }
    client_set_queue (client, NULL);
    if (client->respcode == 0)
    {
        if (message == NULL)
            message = "Not Available";
        client->refbuf = refbuf_new (PER_CLIENT_REFBUF_SIZE);
        snprintf (client->refbuf->data, PER_CLIENT_REFBUF_SIZE,
                "HTTP/1.0 404 Not Available\r\n"
                "Content-Type: text/html\r\n\r\n"
                "<b>%s</b>\r\n", message);
        client->respcode = 404;
        client->refbuf->len = strlen (client->refbuf->data);
        fserve_setup_client (client, NULL);
    }
}


void client_send_416(client_t *client)
{
    client_set_queue (client, NULL);
    client->refbuf = refbuf_new (PER_CLIENT_REFBUF_SIZE);
    snprintf (client->refbuf->data, PER_CLIENT_REFBUF_SIZE,
            "HTTP/1.0 416 Request Range Not Satisfiable\r\n\r\n");
    client->respcode = 416;
    client->refbuf->len = strlen (client->refbuf->data);
    fserve_setup_client (client, NULL);
}


/* helper function for sending the data to a client */
int client_send_bytes (client_t *client, const void *buf, unsigned len)
{
    int (*con_send)(struct connection_tag *handle, const void *buf, size_t len) = connection_send;
    int ret;
#ifdef HAVE_OPENSSL
    if (client->connection.ssl)
        con_send = connection_send_ssl;
#endif
    ret = con_send (&client->connection, buf, len);

    if (client->connection.error)
        DEBUG0 ("Client connection died");

    return ret;
}

void client_set_queue (client_t *client, refbuf_t *refbuf)
{
    refbuf_t *to_release = client->refbuf;

    if (to_release && client->flags & CLIENT_HAS_INTRO_CONTENT)
    {
        refbuf_t *intro = to_release->next;
        while (intro)
        {
            refbuf_t *r = intro->next;
            intro->next = NULL;
            refbuf_release (intro);
            intro = r;
        }
        to_release->next = NULL;
        client->flags &= ~CLIENT_HAS_INTRO_CONTENT;
    }
    client->refbuf = refbuf;
    if (refbuf)
        refbuf_addref (client->refbuf);
    client->pos = 0;
    if (to_release)
        refbuf_release (to_release);
}


worker_t *find_least_busy_handler (void)
{
    worker_t *min = workers;

    if (workers && workers->next)
    {
        worker_t *handler = workers->next;
        DEBUG2 ("handler %p has %d clients", min, min->count);
        while (handler)
        {
            DEBUG2 ("handler %p has %d clients", handler, handler->count);
            if (handler->count < min->count)
                min = handler;
            handler = handler->next;
        }
    }
    return min;
}


void client_change_worker (client_t *client, worker_t *dest_worker)
{
    worker_t *this_worker = client->worker;

    // make sure this client list is ok
    *this_worker->current_p = client->next_on_worker;
    this_worker->count--;
    thread_mutex_unlock (&this_worker->lock);
    client->next_on_worker = NULL;

    thread_mutex_lock (&dest_worker->lock);
    if (dest_worker->running)
    {
        client->worker = dest_worker;
        *dest_worker->last_p = client;
        dest_worker->last_p = &client->next_on_worker;
        dest_worker->count++;
        client->flags |= CLIENT_HAS_CHANGED_THREAD;
        // make client inactive so that the destination thread does not run it straight away
        client->flags &= ~CLIENT_ACTIVE;
    }
    thread_mutex_unlock (&dest_worker->lock);
    thread_mutex_lock (&this_worker->lock);
}


void client_add_worker (client_t *client)
{
    worker_t *handler;

    thread_rwlock_rlock (&workers_lock);
    /* add client to the handler with the least number of clients */
    handler = find_least_busy_handler();
    thread_mutex_lock (&handler->lock);
    thread_rwlock_unlock (&workers_lock);

    client->schedule_ms = handler->time_ms;
    *handler->last_p = client;
    handler->last_p = &client->next_on_worker;
    client->worker = handler;
    ++handler->count;
    if (handler->wakeup_ms - handler->time_ms > 15)
        thread_cond_signal (&handler->cond); /* wake thread if required */
    thread_mutex_unlock (&handler->lock);
}


void *worker (void *arg)
{
    worker_t *handler = arg;
    client_t *client, **prevp;
    long prev_count = -1;
    struct timespec wakeup_time;

    handler->running = 1;
    thread_mutex_lock (&handler->lock);
    thread_get_timespec (&handler->current_time);
    handler->time_ms = THREAD_TIME_MS (&handler->current_time);
    wakeup_time = handler->current_time;

    prevp = &handler->clients;
    while (1)
    {
        if (handler->running == 0 && handler->count == 0)
            break;
        if (prev_count != handler->count)
        {
            DEBUG2 ("%p now has %d clients", handler, handler->count);
            prev_count = handler->count;
        }
        thread_cond_timedwait (&handler->cond, &handler->lock, &wakeup_time);
        thread_get_timespec (&handler->current_time);
        handler->time_ms = THREAD_TIME_MS (&handler->current_time);
        handler->wakeup_ms = handler->time_ms + 60000;
        client = handler->clients;
        prevp = &handler->clients;
        while (client)
        {
            /* process client details but skip those that are not ready yet */
            if (client->flags & CLIENT_ACTIVE)
            {
                if (client->schedule_ms <= handler->time_ms+15)
                {
                    int ret;

                    handler->current_p = prevp;
                    ret = client->ops->process (client);

                    /* special handler, client has moved away to another worker */
                    if (client->flags & CLIENT_HAS_CHANGED_THREAD)
                    {
                        client->flags &= ~CLIENT_HAS_CHANGED_THREAD;
                        client->flags |= CLIENT_ACTIVE;
                        client = *prevp;
                        if (client == NULL)
                            handler->last_p = prevp;
                        continue;
                    }
                    if (ret < 0)
                    {
                        client_t *to_go = client;
                        *prevp = to_go->next_on_worker;
                        client->next_on_worker = NULL;
                        client->worker = NULL;
                        handler->count--;
                        if (client->ops->release)
                            client->ops->release (client);
                        client = *prevp;
                        if (client == NULL)
                            handler->last_p = prevp;
                        continue;
                    }
                }
                if (client->schedule_ms < handler->wakeup_ms)
                    handler->wakeup_ms = client->schedule_ms;
            }
            prevp = &client->next_on_worker;
            client = *prevp;
        }
        handler->wakeup_ms += 10;  /* allow a small sleep */

        wakeup_time.tv_sec =  (long)(handler->wakeup_ms/1000);
        wakeup_time.tv_nsec = (long)((handler->wakeup_ms - (wakeup_time.tv_sec*1000))*1000000);
    }
    thread_mutex_unlock (&handler->lock);
    INFO0 ("shutting down");
    return NULL;
}


static void worker_start (void)
{
    worker_t *handler = calloc (1, sizeof(worker_t));

    thread_mutex_create (&handler->lock);
    thread_cond_create (&handler->cond);
    thread_rwlock_wlock (&workers_lock);
    handler->last_p = &handler->clients;
    handler->next = workers;
    workers = handler;
    worker_count++;
    handler->thread = thread_create ("worker", worker, handler, THREAD_ATTACHED);
    thread_rwlock_unlock (&workers_lock);
}

static void worker_stop (void)
{
    worker_t *handler = workers;
    client_t *clients = NULL, **last;
    int count;

    if (handler == NULL)
        return;
    thread_rwlock_wlock (&workers_lock);
    workers = handler->next;
    worker_count--;
    thread_rwlock_unlock (&workers_lock);

    thread_mutex_lock (&handler->lock);
    handler->running = 0;
    thread_cond_signal (&handler->cond);
    thread_mutex_unlock (&handler->lock);

    thread_sleep (10000);
    thread_mutex_lock (&handler->lock);
    clients = handler->clients;
    last = handler->last_p;
    count = handler->count;
    thread_cond_signal (&handler->cond);
    thread_mutex_unlock (&handler->lock);
    if (clients)
    {
        if (worker_count == 0)
            WARN0 ("clients left unprocessed");
        else
        {
            thread_mutex_lock (&workers->lock);
            *workers->last_p = clients;
            workers->last_p = last;
            workers->count += count;
            thread_mutex_unlock (&workers->lock);
        }
    }
    thread_join (handler->thread);
    thread_mutex_destroy (&handler->lock);
    thread_cond_destroy (&handler->cond);
    free (handler);
}

void workers_adjust (int new_count)
{
    while (worker_count != new_count)
    {
        if (worker_count < new_count)
            worker_start ();
        else
            worker_stop ();
    }
}

