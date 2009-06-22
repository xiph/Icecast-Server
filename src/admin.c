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

#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
#include <libxml/xmlmemory.h>
#include <libxml/parser.h>
#include <libxml/tree.h>

#include "cfgfile.h"
#include "connection.h"
#include "refbuf.h"
#include "client.h"
#include "source.h"
#include "global.h"
#include "event.h"
#include "stats.h"
#include "compat.h"
#include "xslt.h"
#include "fserve.h"
#include "admin.h"
#include "slave.h"

#include "format.h"

#include "logging.h"
#include "auth.h"

#define CATMODULE "admin"


static void command_fallback(client_t *client, source_t *source, int response);
static void command_metadata(client_t *client, source_t *source, int response);
static void command_shoutcast_metadata(client_t *client, source_t *source);
static void command_show_listeners(client_t *client, source_t *source,
        int response);
static void command_move_clients(client_t *client, source_t *source,
        int response);
static void command_stats(client_t *client, const char *filename);
static void command_stats_mount (client_t *client, source_t *source, int response);
static void command_kill_client(client_t *client, source_t *source,
        int response);
static void command_reset_stats (client_t *client, source_t *source, int response);
static void command_manageauth(client_t *client, source_t *source,
        int response);
static void command_buildm3u(client_t *client, const char *mount);
static void command_show_image (client_t *client, const char* mount);
static void command_kill_source(client_t *client, source_t *source,
        int response);
static void command_updatemetadata(client_t *client, source_t *source,
        int response);
static void command_admin_function (client_t *client, int response);
static void command_list_log (client_t *client, int response);
static void command_manage_relay (client_t *client, int response);

static void admin_handle_general_request(client_t *client, const char *command);


struct admin_command
{
    const char *request;
    admin_response_type response;
    union {
        void *x; /* not used but helps on initialisations */
        void (*source)(client_t *client, source_t *source, int response);
        void (*general)(client_t *client, int response);
    } handle;
};


static time_t now;

static struct admin_command admin_general[] =
{
    { "managerelays",       RAW,    { command_manage_relay } },
    { "manageauth",         RAW,    { command_manageauth } },
    { "listmounts",         RAW,    { command_list_mounts } },
    { "function",           RAW,    { command_admin_function } },
    { "streamlist.txt",     TEXT,   { command_list_mounts } },
    { "showlog.txt",        TEXT,   { command_list_log } },
    { "showlog.xsl",        XSLT,   { command_list_log } },
    { "managerelays.xsl",   XSLT,   { command_manage_relay } },
    { "manageauth.xsl",     XSLT,   { command_manageauth } },
    { "listmounts.xsl",     XSLT,   { command_list_mounts } },
    { "moveclients.xsl",    XSLT,   { command_list_mounts } },
    { "function.xsl",       XSLT,   { command_admin_function } },
    { "response.xsl",       XSLT },
    { NULL }
};



static struct admin_command admin_mount[] =
{
    { "fallbacks",          RAW,    { command_fallback } },
    { "metadata",           RAW,    { command_metadata } },
    { "listclients",        RAW,    { command_show_listeners } },
    { "updatemetadata",     RAW,    { command_updatemetadata } },
    { "killclient",         RAW,    { command_kill_client } },
    { "moveclients",        RAW,    { command_move_clients } },
    { "killsource",         RAW,    { command_kill_source } },
    { "stats",              RAW,    { command_stats_mount } },
    { "manageauth",         RAW,    { command_manageauth } },
    { "admin.cgi",          RAW,    { command_shoutcast_metadata } },
    { "resetstats",         XSLT,   { command_reset_stats } },
    { "metadata.xsl",       XSLT,   { command_metadata } },
    { "listclients.xsl",    XSLT,   { command_show_listeners } },
    { "updatemetadata.xsl", XSLT,   { command_updatemetadata } },
    { "killclient.xsl",     XSLT,   { command_kill_client } },
    { "moveclients.xsl",    XSLT,   { command_move_clients } },
    { "killsource.xsl",     XSLT,   { command_kill_source } },
    { "manageauth.xsl",     XSLT,   { command_manageauth } },
    { NULL }
};

/* build an XML doc containing information about currently running sources.
 * If a mountpoint is passed then that source will not be added to the XML
 * doc even if the source is running */
xmlDocPtr admin_build_sourcelist (const char *mount)
{
    avl_node *node;
    source_t *source;
    xmlNodePtr xmlnode, srcnode;
    xmlDocPtr doc;
    char buf[22];
    time_t now = time(NULL);

    doc = xmlNewDoc(XMLSTR("1.0"));
    xmlnode = xmlNewDocNode(doc, NULL, XMLSTR("icestats"), NULL);
    xmlDocSetRootElement(doc, xmlnode);

    if (mount) {
        xmlNewChild(xmlnode, NULL, XMLSTR("current_source"), XMLSTR(mount));
    }

    node = avl_get_first(global.source_tree);
    while(node) {
        source = (source_t *)node->key;
        if (mount && strcmp (mount, source->mount) == 0)
        {
            node = avl_get_next (node);
            continue;
        }

        thread_mutex_lock (&source->lock);
        if (source_available (source))
        {
            ice_config_t *config;
            mount_proxy *mountinfo;

            srcnode = xmlNewChild (xmlnode, NULL, XMLSTR("source"), NULL);
            xmlSetProp (srcnode, XMLSTR("mount"), XMLSTR(source->mount));

            snprintf (buf, sizeof(buf), "%lu", source->listeners);
            xmlNewChild (srcnode, NULL, XMLSTR("listeners"), XMLSTR(buf));

            config = config_get_config();
            mountinfo = config_find_mount (config, source->mount);
            if (mountinfo)
            {
                if (mountinfo->auth)
                {
                    xmlNewChild (srcnode, NULL, XMLSTR("authenticator"), 
                            XMLSTR(mountinfo->auth->type));
                }
                if (mountinfo->fallback_mount)
                    xmlNewChild (srcnode, NULL, XMLSTR("fallback"), 
                            XMLSTR(mountinfo->fallback_mount));
            }
            config_release_config();

            if (source->running)
            {
                if (source->client)
                {
                    snprintf (buf, sizeof(buf), "%lu",
                            (unsigned long)(now - source->client->con->con_time));
                    xmlNewChild (srcnode, NULL, XMLSTR("Connected"), XMLSTR(buf));
                }
                xmlNewChild (srcnode, NULL, XMLSTR("content-type"), 
                        XMLSTR(source->format->contenttype));
            }
        }
        thread_mutex_unlock (&source->lock);
        node = avl_get_next(node);
    }
    return(doc);
}

void admin_send_response(xmlDocPtr doc, client_t *client, 
        admin_response_type response, const char *xslt_template)
{
    if (response == RAW)
    {
        xmlChar *buff = NULL;
        int len = 0;
        unsigned int buf_len;
        const char *http = "HTTP/1.0 200 OK\r\n"
               "Content-Type: text/xml\r\n"
               "Content-Length: ";
        xmlDocDumpFormatMemoryEnc (doc, &buff, &len, NULL, 1);
        buf_len = strlen (http) + len + 20;
        client_set_queue (client, NULL);
        client->refbuf = refbuf_new (buf_len);
        len = snprintf (client->refbuf->data, buf_len, "%s%d\r\n\r\n%s", http, len, buff);
        client->refbuf->len = len;
        xmlFree(buff);
        client->respcode = 200;
        fserve_add_client (client, NULL);
    }
    if (response == XSLT)
    {
        char *fullpath_xslt_template;
        int fullpath_xslt_template_len;
        ice_config_t *config = config_get_config();

        fullpath_xslt_template_len = strlen (config->adminroot_dir) + 
            strlen(xslt_template) + 2;
        fullpath_xslt_template = malloc(fullpath_xslt_template_len);
        snprintf(fullpath_xslt_template, fullpath_xslt_template_len, "%s%s%s",
            config->adminroot_dir, PATH_SEPARATOR, xslt_template);
        config_release_config();

        DEBUG1("Sending XSLT (%s)", fullpath_xslt_template);
        xslt_transform(doc, fullpath_xslt_template, client);
        free(fullpath_xslt_template);
    }
}


static struct admin_command *find_admin_command (struct admin_command *list, const char *uri)
{
    for (; list->request; list++)
    {
        if (strcmp (list->request, uri) == 0)
            break;
    }
    if (list->request == NULL)
    {
        list = NULL;
        if (strcmp (uri, "stats.xml") != 0)
            DEBUG1("request (%s) not a builtin", uri);
    }
    return list;
}

void admin_mount_request (client_t *client, const char *uri)
{
    source_t *source;
    const char *mount = httpp_get_query_param (client->parser, "mount");

    struct admin_command *cmd = find_admin_command (admin_mount, uri);

    if (cmd == NULL)
    {
        command_stats (client, uri);
        return;
    }

    if (cmd == NULL || cmd->handle.source == NULL)
    {
        INFO0("mount request not recognised");
        client_send_400 (client, "unknown request");
        return;
    }

    avl_tree_rlock(global.source_tree);
    source = source_find_mount_raw(mount);

    if (source == NULL)
    {
        WARN1("Admin command on non-existent source %s", mount);
        avl_tree_unlock(global.source_tree);
        client_send_400 (client, "Source does not exist");
    }
    else
    {
        if (source->running == 0 && source->on_demand == 0)
        {
            avl_tree_unlock (global.source_tree);
            INFO1("Received admin command on unavailable mount \"%s\"", mount);
            client_send_400 (client, "Source is not available");
            return;
        }
        cmd->handle.source (client, source, cmd->response);
        avl_tree_unlock(global.source_tree);
    }
}


int admin_handle_request (client_t *client, const char *uri)
{
    const char *mount;

    if (strcmp (uri, "/admin.cgi") != 0 && strncmp("/admin/", uri, 7) != 0)
        return -1;

    mount = httpp_get_query_param(client->parser, "mount");

    if (strcmp (uri, "/admin.cgi") == 0)
    {
        const char *pass = httpp_get_query_param (client->parser, "pass");
        if (pass == NULL)
        {
            client_send_400 (client, "missing pass parameter");
            return 0;
        }
        uri++;
        if (mount == NULL)
        {
            if (client->server_conn && client->server_conn->shoutcast_mount)
                httpp_set_query_param (client->parser, "mount",
                        client->server_conn->shoutcast_mount);
            mount = httpp_get_query_param (client->parser, "mount");
        }
        httpp_setvar (client->parser, HTTPP_VAR_PROTOCOL, "ICY");
        httpp_setvar (client->parser, HTTPP_VAR_ICYPASSWORD, pass);
    }
    else
        uri += 7;

    if (connection_check_admin_pass (client->parser))
        client->authenticated = 1;

    /* special case for slaves requesting a streamlist for authenticated relaying */
    if (strcmp (uri, "streams") == 0)
    {
        client->is_slave = 1;
        auth_add_listener ("/admin/streams", client);
        return 0;
    }
    if (strcmp (uri, "streamlist.txt") == 0)
    {
        if (connection_check_relay_pass (client->parser))
            client->authenticated = 1;
    }

    if (mount)
    {
        /* no auth/stream required for this */
        if (strcmp (uri, "buildm3u") == 0)
        {
            command_buildm3u (client, mount);
            return 0;
        }
        if (strcmp (uri, "showimage") == 0)
        {
            command_show_image (client, mount);
            return 0;
        }

        /* This is a mount request, but admin user is allowed */
        if (client->authenticated == 0)
        {
            switch (auth_check_source (client, mount))
            {
                case 0:
                    break;
                default:
                    INFO1("Bad or missing password on mount modification "
                            "admin request (%s)", uri);
                    client_send_401 (client, NULL);
                    /* fall through */
                case 1:
                    return 0;
            }
        }
        admin_mount_request (client, uri);
        return 0;
    }

    admin_handle_general_request (client, uri);
    return 0;
}



static void admin_handle_general_request(client_t *client, const char *uri)
{
    struct admin_command *cmd;

    if (client->authenticated == 0)
    {
        INFO1("Bad or missing password on admin command request (%s)", uri);
        client_send_401 (client, NULL);
        return;
    }

    cmd = find_admin_command (admin_general, uri);
    if (cmd == NULL)
    {
        INFO1 ("processing file %s", uri);
        command_stats (client, uri);
        return;
    }
    if (cmd->handle.general == NULL)
    {
        client_send_400 (client, "unknown request");
        return;
    }
    cmd->handle.general (client, cmd->response);
}


#define COMMAND_REQUIRE(client,name,var) \
    do { \
        (var) = httpp_get_query_param((client)->parser, (name)); \
        if((var) == NULL) { \
            client_send_400((client), "Missing parameter"); \
            return; \
        } \
    } while(0);
#define COMMAND_OPTIONAL(client,name,var) \
    (var) = httpp_get_query_param((client)->parser, (name))

static void html_success(client_t *client, char *message)
{
    client->respcode = 200;
    snprintf (client->refbuf->data, PER_CLIENT_REFBUF_SIZE,
            "HTTP/1.0 200 OK\r\nContent-Type: text/html\r\n\r\n" 
            "<html><head><title>Admin request successful</title></head>"
            "<body><p>%s</p></body></html>", message);
    client->refbuf->len = strlen (client->refbuf->data);
    fserve_add_client (client, NULL);
}


static void command_move_clients(client_t *client, source_t *source,
    int response)
{
    const char *dest_source;
    source_t *dest;
    xmlDocPtr doc;
    xmlNodePtr node;
    int parameters_passed = 0;
    char buf[255];

    if((COMMAND_OPTIONAL(client, "destination", dest_source))) {
        parameters_passed = 1;
    }
    if (!parameters_passed) {
        doc = admin_build_sourcelist(source->mount);
        admin_send_response(doc, client, response, "moveclients.xsl");
        xmlFreeDoc(doc);
        return;
    }

    dest = source_find_mount (dest_source);

    if (dest == NULL)
    {
        client_send_400 (client, "No such destination");
        return;
    }

    if (strcmp (dest->mount, source->mount) == 0)
    {
        client_send_400 (client, "supplied mountpoints are identical");
        return;
    }

    if (dest->running == 0 && dest->on_demand == 0)
    {
        client_send_400 (client, "Destination not running");
        return;
    }

    INFO2 ("source is \"%s\", destination is \"%s\"", source->mount, dest->mount);

    doc = xmlNewDoc(XMLSTR("1.0"));
    node = xmlNewDocNode(doc, NULL, XMLSTR("iceresponse"), NULL);
    xmlDocSetRootElement(doc, node);

    source_move_clients (source, dest);

    snprintf (buf, sizeof(buf), "Clients moved from %s to %s",
            source->mount, dest_source);
    xmlNewChild(node, NULL, XMLSTR("message"), XMLSTR(buf));
    xmlNewChild(node, NULL, XMLSTR("return"), XMLSTR("1"));

    admin_send_response(doc, client, response, "response.xsl");
    xmlFreeDoc(doc);
}

static int admin_function (const char *function, char *buf, unsigned int len)
{
    if (strcmp (function, "reopenlog") == 0)
    {
        ice_config_t *config = config_grab_config();

        restart_logging (config);
        config_release_config();
        snprintf (buf, len, "Re-opening log files");
        return 0;
    }
    if (strcmp (function, "updatecfg") == 0)
    {
        global . schedule_config_reread = 1;
        snprintf (buf, len, "Requesting reread of configuration file");
        return 0;
    }
    return -1;
}


static void command_admin_function (client_t *client, int response)
{
    xmlDocPtr doc;
    xmlNodePtr node;
    const char *perform;
    char buf[256];

    COMMAND_REQUIRE (client, "perform", perform);
    if (admin_function (perform, buf, sizeof buf) < 0)
    {
        client_send_400 (client, "No such handler");
        return;
    }
    doc = xmlNewDoc(XMLSTR("1.0"));
    node = xmlNewDocNode(doc, NULL, XMLSTR("iceresponse"), NULL);
    xmlDocSetRootElement(doc, node);

    xmlNewChild(node, NULL, XMLSTR("message"), XMLSTR(buf));
    xmlNewChild(node, NULL, XMLSTR("return"), XMLSTR("1"));

    admin_send_response(doc, client, response, "response.xsl");
    xmlFreeDoc(doc);
}


static void add_relay_xmlnode (xmlNodePtr node, relay_server *relay, int from_master)
{
    xmlNodePtr relaynode = xmlNewChild (node, NULL, XMLSTR("relay"), NULL);
    relay_server_master *master = relay->masters;
    char str [50];

    xmlNewChild (relaynode, NULL, XMLSTR("localmount"), XMLSTR(relay->localmount));
    snprintf (str, sizeof (str), "%d", relay->enable);
    xmlNewChild (relaynode, NULL, XMLSTR("enable"), XMLSTR(str));
    snprintf (str, sizeof (str), "%d", relay->on_demand);
    xmlNewChild (relaynode, NULL, XMLSTR("on_demand"), XMLSTR(str));
    snprintf (str, sizeof (str), "%d", from_master);
    xmlNewChild (relaynode, NULL, XMLSTR("from_master"), XMLSTR(str));
    while (master)
    {
        xmlNodePtr masternode = xmlNewChild (relaynode, NULL, XMLSTR("master"), NULL);
        xmlNewChild (masternode, NULL, XMLSTR("server"), XMLSTR(master->ip));
        xmlNewChild (masternode, NULL, XMLSTR("mount"), XMLSTR(master->mount));
        snprintf (str, sizeof (str), "%d", master->port);
        xmlNewChild (masternode, NULL, XMLSTR("port"), XMLSTR(str));
        master = master->next;
    }
}


static void command_manage_relay (client_t *client, int response)
{
    const char *relay_mount, *enable;
    const char *msg;
    relay_server *relay;
    xmlDocPtr doc;
    xmlNodePtr node;

    COMMAND_OPTIONAL (client, "relay", relay_mount);
    COMMAND_OPTIONAL (client, "enable", enable);

    if (relay_mount == NULL || enable == NULL)
    {
        doc = xmlNewDoc (XMLSTR("1.0"));
        node = xmlNewDocNode (doc, NULL, XMLSTR("icerelaystats"), NULL);
        xmlDocSetRootElement(doc, node);
        thread_mutex_lock (&(config_locks()->relay_lock));

        for (relay = global.relays; relay; relay=relay->next)
            add_relay_xmlnode (node, relay, 0);
        for (relay = global.master_relays; relay; relay=relay->next)
            add_relay_xmlnode (node, relay, 1);

        thread_mutex_unlock (&(config_locks()->relay_lock));
        admin_send_response (doc, client, response, "managerelays.xsl");
        xmlFreeDoc (doc);
        return;
    }

    thread_mutex_lock (&(config_locks()->relay_lock));

    relay = slave_find_relay (global.relays, relay_mount);
    if (relay == NULL)
        relay = slave_find_relay (global.master_relays, relay_mount);

    msg = "no such relay";
    if (relay)
    {
        relay->enable = atoi (enable);
        msg = "relay has been changed";
        if (relay->enable == 0)
        {
            if (relay->source && relay->source->running == 0)
                relay->source->on_demand = 0;
        }
        slave_update_all_mounts();
    }
    thread_mutex_unlock (&(config_locks()->relay_lock));

    doc = xmlNewDoc(XMLSTR("1.0"));
    node = xmlNewDocNode(doc, NULL, XMLSTR("iceresponse"), NULL);
    xmlDocSetRootElement(doc, node);
    xmlNewChild(node, NULL, XMLSTR("message"), XMLSTR(msg));
    xmlNewChild(node, NULL, XMLSTR("return"), XMLSTR("1"));
    admin_send_response(doc, client, response, "response.xsl");
    xmlFreeDoc(doc);
}


static void add_listener_node (xmlNodePtr srcnode, client_t *listener)
{
    const char *useragent;
    char buf[30];

    xmlNodePtr node = xmlNewChild (srcnode, NULL, XMLSTR("listener"), NULL);

    snprintf (buf, sizeof (buf), "%lu", listener->con->id);
    xmlSetProp (node, XMLSTR("id"), XMLSTR(buf));

    xmlNewChild (node, NULL, XMLSTR("ip"), XMLSTR(listener->con->ip));

    useragent = httpp_getvar (listener->parser, "user-agent");
    if (useragent)
    {
        xmlChar *str = xmlEncodeEntitiesReentrant (srcnode->doc, XMLSTR(useragent));
        xmlNewChild (node, NULL, XMLSTR("useragent"), str); 
        xmlFree (str);
    }

    snprintf (buf, sizeof (buf), "%u", listener->lag);
    xmlNewChild (node, NULL, XMLSTR("lag"), XMLSTR(buf));

    snprintf (buf, sizeof (buf), "%lu",
            (unsigned long)(now - listener->con->con_time));
    xmlNewChild (node, NULL, XMLSTR("connected"), XMLSTR(buf));
    if (listener->username)
    {
        xmlChar *str = xmlEncodeEntitiesReentrant (srcnode->doc, XMLSTR(listener->username));
        xmlNewChild (node, NULL, XMLSTR("username"), str);
        xmlFree (str);
    }
}


/* populate within srcnode, groups of 0 or more listener tags detailing
 * information about each listener connected on the provide source.
 */
void admin_source_listeners (source_t *source, xmlNodePtr srcnode)
{
    client_t *listener;

    if (source == NULL)
        return;

    thread_mutex_lock (&source->lock);

    now = time(NULL);
    listener = source->active_clients;
    while (listener)
    {
        add_listener_node (srcnode, listener);
        listener = listener->next;
    }
    thread_mutex_unlock (&source->lock);
}


static void command_reset_stats (client_t *client, source_t *source, int response)
{
    const char *msg = "Failed to reset values";
    const char *name = httpp_get_query_param (client->parser, "setting");
    int all = 0, ok = 0;
    xmlDocPtr doc;
    xmlNodePtr node;

    if (name == NULL)
        all = 1;
    if (all || strstr (name, "peak"))
    {
        source->peak_listeners = source->listeners;
        source->prev_listeners = source->peak_listeners+1;
        ok = 1;
    }
    if (all || strstr (name, "read"))
        if (source->format)
        {
            source->format->read_bytes = 0;
            ok = 1;
        }
    if (all || strstr (name, "sent"))
        if (source->format)
        {
            source->format->sent_bytes = 0;
            ok = 1;
        }

    if (ok)
        msg = "have reset settings";
    doc = xmlNewDoc(XMLSTR("1.0"));
    node = xmlNewDocNode(doc, NULL, XMLSTR("iceresponse"), NULL);
    xmlDocSetRootElement(doc, node);
    xmlNewChild(node, NULL, XMLSTR("message"), XMLSTR(msg));
    xmlNewChild(node, NULL, XMLSTR("return"), XMLSTR("1"));
    admin_send_response(doc, client, response, "response.xsl");
    xmlFreeDoc(doc);
}

static void command_show_listeners(client_t *client, source_t *source,
    int response)
{
    xmlDocPtr doc;
    xmlNodePtr node, srcnode;
    unsigned long id = -1;
    const char *ID_str = NULL;
    char buf[22];

    doc = xmlNewDoc(XMLSTR("1.0"));
    node = xmlNewDocNode(doc, NULL, XMLSTR("icestats"), NULL);
    srcnode = xmlNewChild(node, NULL, XMLSTR("source"), NULL);

    xmlSetProp(srcnode, XMLSTR("mount"), XMLSTR(source->mount));
    xmlDocSetRootElement(doc, node);

    snprintf(buf, sizeof(buf), "%lu", source->listeners);
    xmlNewChild(srcnode, NULL, XMLSTR("listeners"), XMLSTR(buf));

    COMMAND_OPTIONAL(client, "id", ID_str);
    if (ID_str)
        id = atoi (ID_str);

    if (id == -1)
        admin_source_listeners (source, srcnode);
    else
    {
        client_t *listener;
        thread_mutex_lock (&source->lock);

        now = time(NULL);
        listener = source->active_clients;
        while (listener)
        {
            if (listener->con->id == id)
            {
                add_listener_node (srcnode, listener);
                break;
            }
            listener = listener->next;
        }
        thread_mutex_unlock (&source->lock);
    }

    admin_send_response(doc, client, response, "listclients.xsl");
    xmlFreeDoc(doc);
}

static void command_show_image (client_t *client, const char *mount)
{
    source_t *source;

    avl_tree_rlock (global.source_tree);
    source = source_find_mount_raw (mount);
    if (source && source->format && source->format->get_image)
    {
        thread_mutex_lock (&source->lock);
        avl_tree_unlock (global.source_tree);
        if (source->format->get_image (client, source->format) == 0)
        {
            thread_mutex_unlock (&source->lock);
            fserve_add_client (client, NULL);
            return;
        }
        thread_mutex_unlock (&source->lock);
    }
    else
        avl_tree_unlock (global.source_tree);
    client_send_404 (client, "No image available");
}

static void command_buildm3u (client_t *client, const char *mount)
{
    const char *username = NULL;
    const char *password = NULL;
    ice_config_t *config;

    COMMAND_REQUIRE(client, "username", username);
    COMMAND_REQUIRE(client, "password", password);

    client->respcode = 200;
    config = config_get_config();
    snprintf (client->refbuf->data, PER_CLIENT_REFBUF_SIZE,
        "HTTP/1.0 200 OK\r\n"
        "Content-Type: audio/x-mpegurl\r\n"
        "Content-Disposition = attachment; filename=listen.m3u\r\n\r\n" 
        "http://%s:%s@%s:%d%s\r\n",
        username,
        password,
        config->hostname,
        config->port,
        mount
    );
    config_release_config();

    client->refbuf->len = strlen (client->refbuf->data);
    fserve_add_client (client, NULL);
}


static void command_manageauth(client_t *client, source_t *source,
        int response)
{
    xmlDocPtr doc;
    xmlNodePtr node, srcnode, msgnode;
    const char *action = NULL;
    const char *username = NULL;
    char *message = NULL;
    int ret = AUTH_OK;
    ice_config_t *config = config_get_config ();
    mount_proxy *mountinfo = config_find_mount (config, source->mount);

    do
    { 
        if (mountinfo == NULL || mountinfo->auth == NULL)
        {
            WARN1 ("manage auth request for %s but no facility available", source->mount);
            break;
        }
        COMMAND_OPTIONAL (client, "action", action);
        COMMAND_OPTIONAL (client, "username", username);

        if (action == NULL)
            action = "list";

        if (!strcmp(action, "add"))
        {
            const char *password = NULL;
            COMMAND_OPTIONAL (client, "password", password);

            if (username == NULL || password == NULL)
            {
                WARN1 ("manage auth request add for %s but no user/pass", source->mount);
                break;
            }
            ret = mountinfo->auth->adduser(mountinfo->auth, username, password);
            if (ret == AUTH_FAILED) {
                message = strdup("User add failed - check the icecast error log");
            }
            if (ret == AUTH_USERADDED) {
                message = strdup("User added");
            }
            if (ret == AUTH_USEREXISTS) {
                message = strdup("User already exists - not added");
            }
        }
        if (!strcmp(action, "delete"))
        {
            if (username == NULL)
            {
                WARN1 ("manage auth request delete for %s but no username", source->mount);
                break;
            }
            ret = mountinfo->auth->deleteuser(mountinfo->auth, username);
            if (ret == AUTH_FAILED) {
                message = strdup("User delete failed - check the icecast error log");
            }
            if (ret == AUTH_USERDELETED) {
                message = strdup("User deleted");
            }
        }

        doc = xmlNewDoc(XMLSTR "1.0");
        node = xmlNewDocNode(doc, NULL, XMLSTR("icestats"), NULL);
        srcnode = xmlNewChild(node, NULL, XMLSTR("source"), NULL);
        xmlSetProp(srcnode, XMLSTR "mount", XMLSTR(source->mount));

        if (message) {
            msgnode = xmlNewChild(node, NULL, XMLSTR("iceresponse"), NULL);
            xmlNewChild(msgnode, NULL, XMLSTR "message", XMLSTR(message));
        }

        xmlDocSetRootElement(doc, node);

        if (mountinfo && mountinfo->auth && mountinfo->auth->listuser)
            mountinfo->auth->listuser (mountinfo->auth, srcnode);

        config_release_config ();

        admin_send_response(doc, client, response, "manageauth.xsl");
        free (message);
        xmlFreeDoc(doc);
        return;
    } while (0);

    config_release_config ();
    client_send_400 (client, "missing parameter");
}

static void command_kill_source(client_t *client, source_t *source,
        int response)
{
    xmlDocPtr doc;
    xmlNodePtr node;

    doc = xmlNewDoc(XMLSTR("1.0"));
    node = xmlNewDocNode(doc, NULL, XMLSTR("iceresponse"), NULL);
    xmlNewChild(node, NULL, XMLSTR("message"), XMLSTR("Source Removed"));
    xmlNewChild(node, NULL, XMLSTR("return"), XMLSTR("1"));
    xmlDocSetRootElement(doc, node);

    source->running = 0;

    admin_send_response(doc, client, response, "response.xsl");
    xmlFreeDoc(doc);
}

static void command_kill_client(client_t *client, source_t *source,
    int response)
{
    const char *idtext;
    int id;
    client_t *listener;
    xmlDocPtr doc;
    xmlNodePtr node;
    char buf[50] = "";

    COMMAND_REQUIRE(client, "id", idtext);

    id = atoi(idtext);

    thread_mutex_lock (&source->lock);
    listener = source_find_client(source, id);

    doc = xmlNewDoc(XMLSTR("1.0"));
    node = xmlNewDocNode(doc, NULL, XMLSTR("iceresponse"), NULL);
    xmlDocSetRootElement(doc, node);

    if(listener != NULL) {
        INFO1("Admin request: client %d removed", id);

        /* This tags it for removal on the next iteration of the main source
         * loop
         */
        listener->con->error = 1;
        snprintf(buf, sizeof(buf), "Client %d removed", id);
        xmlNewChild(node, NULL, XMLSTR("message"), XMLSTR(buf));
        xmlNewChild(node, NULL, XMLSTR("return"), XMLSTR("1"));
    }
    else {
        memset(buf, '\000', sizeof(buf));
        snprintf(buf, sizeof(buf)-1, "Client %d not found", id);
        xmlNewChild(node, NULL, XMLSTR("message"), XMLSTR(buf));
        xmlNewChild(node, NULL, XMLSTR("return"), XMLSTR("0"));
    }
    thread_mutex_unlock (&source->lock);
    admin_send_response(doc, client, response, "response.xsl");
    xmlFreeDoc(doc);
}

static void command_fallback(client_t *client, source_t *source,
    int response)
{
    const char *fallback;

    DEBUG0("Got fallback request");

    COMMAND_REQUIRE(client, "fallback", fallback);

    client_send_400 (client, "not implemented");
}

static void command_metadata(client_t *client, source_t *source,
    int response)
{
    const char *song, *title, *artist, *artwork, *charset, *url;
    format_plugin_t *plugin;
    xmlDocPtr doc;
    xmlNodePtr node;
    int same_ip = 1;

    doc = xmlNewDoc(XMLSTR("1.0"));
    node = xmlNewDocNode(doc, NULL, XMLSTR("iceresponse"), NULL);
    xmlDocSetRootElement(doc, node);

    DEBUG0("Got metadata update request");

    COMMAND_OPTIONAL(client, "song", song);
    COMMAND_OPTIONAL(client, "title", title);
    COMMAND_OPTIONAL(client, "artist", artist);
    COMMAND_OPTIONAL(client, "url", url);
    COMMAND_OPTIONAL(client, "artwork", artwork);
    COMMAND_OPTIONAL(client, "charset", charset);

    thread_mutex_lock (&source->lock);

    plugin = source->format;
    if (source->client && strcmp (client->con->ip, source->client->con->ip) != 0)
        if (response == RAW && connection_check_admin_pass (client->parser) == 0)
            same_ip = 0;

    do
    {
        if (same_ip == 0 || plugin == NULL)
            break;
        if (artwork)
            stats_event (source->mount, "artwork", artwork);
        if (plugin->set_tag)
        {
            if (url)
            {
                plugin->set_tag (plugin, "url", url, charset);
                INFO2 ("Metadata url on %s set to \"%s\"", source->mount, url);
            }
            if (song)
            {
                plugin->set_tag (plugin, "song", song, charset);
                INFO2("Metadata song on mountpoint %s changed to \"%s\"", source->mount, song);
            }
            if (artist)
            {
                plugin->set_tag (plugin, "artist", artist, charset);
                INFO2 ("Metadata artist on mountpoint %s changed to \"%s\"", source->mount, artist);
            }
            if (title)
            {
                plugin->set_tag (plugin, "title", title, charset);
                INFO2 ("Metadata title on mountpoint %s changed to \"%s\"", source->mount, title);
            }
            /* updates are now done, let them be pushed into the stream */
            plugin->set_tag (plugin, NULL, NULL, NULL);
        }
        else
        {
            break;
        }
        thread_mutex_unlock (&source->lock);
        xmlNewChild(node, NULL, XMLSTR("message"), XMLSTR("Metadata update successful"));
        xmlNewChild(node, NULL, XMLSTR("return"), XMLSTR("1"));
        admin_send_response(doc, client, response, "response.xsl");
        xmlFreeDoc(doc);
        return;

    } while (0);
    thread_mutex_unlock (&source->lock);
    xmlNewChild(node, NULL, XMLSTR("message"), 
            XMLSTR("Mountpoint will not accept this URL update"));
    xmlNewChild(node, NULL, XMLSTR("return"), XMLSTR("1"));
    admin_send_response(doc, client, response, "response.xsl");
    xmlFreeDoc(doc);
}

static void command_shoutcast_metadata(client_t *client, source_t *source)
{
    const char *action;
    const char *value;
    int same_ip = 1;

    DEBUG0("Got shoutcast metadata update request");

    COMMAND_REQUIRE(client, "mode", action);
    COMMAND_REQUIRE(client, "song", value);

    if (strcmp (action, "updinfo") != 0)
    {
        thread_mutex_unlock (&source->lock);
        client_send_400 (client, "No such action");
        return;
    }

    if (source->shoutcast_compat == 0)
    {
        ERROR0 ("illegal change of metadata on non-shoutcast compatible stream");
        client_send_400 (client, "illegal metadata call");
        return;
    }

    if (source->client && strcmp (client->con->ip, source->client->con->ip) != 0)
        if (connection_check_admin_pass (client->parser) == 0)
            same_ip = 0;

    if (same_ip && source->format && source->format->set_tag)
    {
        httpp_set_query_param (client->parser, "mount", client->server_conn->shoutcast_mount);
        source->format->set_tag (source->format, "title", value, NULL);
        source->format->set_tag (source->format, NULL, NULL, NULL);

        DEBUG2("Metadata on mountpoint %s changed to \"%s\"", 
                source->mount, value);
        thread_mutex_unlock (&source->lock);
        html_success(client, "Metadata update successful");
    }
    else
    {
        thread_mutex_unlock (&source->lock);
        client_send_400 (client, "mountpoint will not accept URL updates");
    }
}


static void command_stats_mount (client_t *client, source_t *source, int response)
{
    command_stats (client, NULL);
}
/* catch all function for admin requests.  If file has xsl extension then
 * transform it using the available stats, else send the XML tree of the
 * stats
 */
static void command_stats (client_t *client, const char *filename)
{
    admin_response_type response = RAW;
    const char *show_mount = NULL;
    xmlDocPtr doc;

    if (filename)
        if (util_check_valid_extension (filename) == XSLT_CONTENT)
            response = XSLT;

    show_mount = httpp_get_query_param (client->parser, "mount");

    doc = stats_get_xml (STATS_ALL, show_mount);
    admin_send_response (doc, client, response, filename);
    xmlFreeDoc(doc);
}


static void command_list_log (client_t *client, int response)
{
    refbuf_t *content;
    const char *logname = httpp_get_query_param (client->parser, "log");
    int log = -1;

    if (logname == NULL)
    {
        client_send_400 (client, "No log specified");
        return;
    }

    if (strcmp (logname, "errorlog") == 0)
        log = errorlog;
    else if (strcmp (logname, "accesslog") == 0)
        log = accesslog;
    else if (strcmp (logname, "playlistlog") == 0)
        log = playlistlog;

    if (log < 0)
    {
        WARN1 ("request to show unknown log \"%s\"", logname);
        client_send_400 (client, "");
        return;
    }
    content = refbuf_new (0);
    log_contents (log, &content->data, &content->len);

    if (response == XSLT)
    {
        xmlNodePtr xmlnode, lognode;
        xmlDocPtr doc;

        doc = xmlNewDoc(XMLSTR("1.0"));
        xmlnode = xmlNewDocNode(doc, NULL, XMLSTR("icestats"), NULL);
        xmlDocSetRootElement(doc, xmlnode);
        lognode = xmlNewTextChild (xmlnode, NULL, XMLSTR("log"), XMLSTR(content->data));
        refbuf_release (content);

        admin_send_response (doc, client, XSLT, "showlog.xsl");
        xmlFreeDoc(doc);
    }
    else
    {
        refbuf_t *http = refbuf_new (100);
        int len = snprintf (http->data, 100, "%s",
                "HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\n\r\n");
        http->len = len;
        http->next = content; 
        client->respcode = 200;
        client_set_queue (client, http);
        fserve_add_client (client, NULL);
    }
}


void command_list_mounts(client_t *client, int response)
{
    DEBUG0("List mounts request");

    if (response == TEXT)
    {
        redirector_update (client);

        snprintf (client->refbuf->data, PER_CLIENT_REFBUF_SIZE,
                "HTTP/1.0 200 OK\r\nContent-Type: text/html\r\n\r\n");
        client->refbuf->len = strlen (client->refbuf->data);
        client->respcode = 200;

        if (strcmp (httpp_getvar (client->parser, HTTPP_VAR_URI), "/admin/streams") == 0)
            client->refbuf->next = stats_get_streams (1);
        else
            client->refbuf->next = stats_get_streams (0);
        fserve_add_client (client, NULL);
    }
    else
    {
        xmlDocPtr doc;
        avl_tree_rlock (global.source_tree);
        doc = admin_build_sourcelist(NULL);
        avl_tree_unlock (global.source_tree);

        admin_send_response(doc, client, response, "listmounts.xsl");
        xmlFreeDoc(doc);
    }
}

static void command_updatemetadata(client_t *client, source_t *source,
    int response)
{
    xmlDocPtr doc;
    xmlNodePtr node, srcnode;

    doc = xmlNewDoc(XMLSTR("1.0"));
    node = xmlNewDocNode(doc, NULL, XMLSTR("icestats"), NULL);
    srcnode = xmlNewChild(node, NULL, XMLSTR("source"), NULL);
    xmlSetProp(srcnode, XMLSTR("mount"), XMLSTR(source->mount));
    xmlDocSetRootElement(doc, node);

    admin_send_response(doc, client, response, 
        "updatemetadata.xsl");
    xmlFreeDoc(doc);
}

