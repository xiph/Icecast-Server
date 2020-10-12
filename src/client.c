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
 * Copyright 2011-2020, Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
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

#include <libxml/tree.h>

#include "common/thread/thread.h"
#include "common/avl/avl.h"
#include "common/httpp/httpp.h"

#include "global.h"
#include "refobject.h"
#include "cfgfile.h"
#include "connection.h"
#include "tls.h"
#include "refbuf.h"
#include "format.h"
#include "stats.h"
#include "fserve.h"
#include "errors.h"
#include "reportxml.h"
#include "refobject.h"
#include "xslt.h"
#include "xml2json.h"
#include "source.h"

#include "client.h"
#include "auth.h"
#include "logging.h"

#include "util.h"
#include "acl.h"
#include "listensocket.h"
#include "fastevent.h"

/* for ADMIN_COMMAND_ERROR, and ADMIN_ICESTATS_LEGACY_EXTENSION_APPLICATION */
#include "admin.h"

#ifdef _WIN32
#define snprintf _snprintf
#endif

#undef CATMODULE
#define CATMODULE "client"

static inline void client_send_500(client_t *client, const char *message);

/* This returns the protocol ID based on the string.
 * If the string is invalid for any reason we return ICECAST_PROTOCOL_HTTP.
 */
protocol_t client_protocol_from_string(const char *str)
{
    if (!str) {
        ICECAST_LOG_ERROR("No protocol string given. Returning ICECAST_PROTOCOL_HTTP.");
        return ICECAST_PROTOCOL_HTTP;
    }

    if (strcasecmp(str, "http") == 0) {
        return ICECAST_PROTOCOL_HTTP;
    } else if (strcasecmp(str, "icy") == 0 || strcasecmp(str, "shoutcast") == 0) {
        return ICECAST_PROTOCOL_SHOUTCAST;
    } else {
        ICECAST_LOG_ERROR("Unknown protocol \"%H\" string given. Returning ICECAST_PROTOCOL_HTTP.", str);
        return ICECAST_PROTOCOL_HTTP;
    }
}

const char * client_protocol_to_string(protocol_t protocol)
{
    switch (protocol) {
        case ICECAST_PROTOCOL_HTTP:
            return "http";
        break;
        case ICECAST_PROTOCOL_SHOUTCAST:
            return "icy";
        break;
    }

    return NULL;
}

/* create a client_t with the provided connection and parser details. Return
 * 0 on success, -1 if server limit has been reached.  In either case a
 * client_t is returned just in case a message needs to be returned. Should
 * be called with global lock held.
 */
int client_create(client_t **c_ptr, connection_t *con, http_parser_t *parser)
{
    ice_config_t    *config;
    client_t        *client = (client_t *) calloc(1, sizeof(client_t));
    const listener_t *listener_real, *listener_effective;
    int              ret    = -1;

    if (client == NULL)
        abort();

    config = config_get_config();

    global.clients++;
    if (config->client_limit < global.clients) {
        ICECAST_LOG_WARN("server client limit reached (%d/%d)", config->client_limit, global.clients);
    } else {
        ret = 0;
    }

    config_release_config ();

    stats_event_args (NULL, "clients", "%d", global.clients);
    client->con = con;
    client->parser = parser;
    client->protocol = ICECAST_PROTOCOL_HTTP;
    client->request_body_length = 0;
    client->request_body_read = 0;
    client->admin_command = ADMIN_COMMAND_ERROR;
    client->refbuf = refbuf_new (PER_CLIENT_REFBUF_SIZE);
    client->refbuf->len = 0; /* force reader code to ignore buffer contents */
    client->pos = 0;
    client->write_to_client = format_generic_write_to_client;
    *c_ptr = client;

    listener_real = listensocket_get_listener(con->listensocket_real);
    listener_effective = listensocket_get_listener(con->listensocket_effective);
    ICECAST_LOG_DEBUG("Client %p created on connection %p (connection ID: %llu, socket real: %p \"%H\", socket effective: %p \"%H\")",
            client, con, (long long unsigned int)con->id,
            con->listensocket_real, con->listensocket_real ? listener_real->id : NULL,
            con->listensocket_effective, con->listensocket_effective ? listener_effective->id : NULL
            );
    listensocket_release_listener(con->listensocket_effective);
    listensocket_release_listener(con->listensocket_real);

    fastevent_emit(FASTEVENT_TYPE_CLIENT_CREATE, FASTEVENT_FLAG_MODIFICATION_ALLOWED, FASTEVENT_DATATYPE_CLIENT, client);

    return ret;
}

void client_complete(client_t *client)
{
    const char *header;
    long long unsigned int scannumber;

    do {
        header = httpp_getvar(client->parser, "content-length");
        if (header) {
            if (sscanf(header, "%llu", &scannumber) == 1) {
                client->request_body_length = scannumber;
                break;
            }
        }

        if (client->parser->req_type == httpp_req_source) {
            client->request_body_length = -1; /* streaming */
            break;
        }

        header = httpp_getvar(client->parser, "transfer-encoding");
        if (header) {
            if (strcasecmp(header, "identity") != 0) {
                client->request_body_length = -1; /* streaming */
                break;
            }
        }

        if (client->parser->req_type == httpp_req_put) {
            /* As we don't know yet, we asume this PUT is in streaming mode */
            client->request_body_length = -1; /* streaming */
            break;
        }

        if (client->parser->req_type == httpp_req_none) {
            /* We are a client. If the server did not tell us, we asume streaming. */
            client->request_body_length = -1; /* streaming */
            break;
        }
    } while (0);

    ICECAST_LOG_DEBUG("Client %p has request_body_length=%zi", client, client->request_body_length);
}

static inline void client_reuseconnection(client_t *client) {
    connection_t *con;
    reuse_t reuse;

    if (!client)
        return;

    con = client->con;
    reuse = client->reuse;

    if (reuse == ICECAST_REUSE_UPGRADETLS) {
        http_parser_t *parser = client->parser;

        httpp_deletevar(parser, "upgrade");
        client->reuse = ICECAST_REUSE_CLOSE;

        /* release the buffer now, as the buffer could be on the source queue
         * and may of disappeared after auth completes */
        client_set_queue(client, NULL);
        client->refbuf = refbuf_new (PER_CLIENT_REFBUF_SIZE);
        client->refbuf->len = 0; /* force reader code to ignore buffer contents */
        client->pos = 0;

        connection_uses_tls(con);
        connection_queue_client(client);

        return;
    }

    con = connection_create(con->sock, con->listensocket_real, con->listensocket_effective, strdup(con->ip));
    client->con->sock = -1; /* TODO: do not use magic */

    /* handle to keep the TLS connection */
    if (client->con->tls) {
        /* AHhhggrr.. That pain....
         * stealing TLS state...
         */
        con->tls  = client->con->tls;
        con->read = client->con->read;
        con->send = client->con->send;
        client->con->tls  = NULL;
        client->con->read = NULL;
        client->con->send = NULL;
    }

    if (client->con->readbufferlen) {
        /* Aend... moorre paaiin.
         * stealing putback buffer.
         */
        con->readbuffer = client->con->readbuffer;
        con->readbufferlen = client->con->readbufferlen;
        client->con->readbuffer = NULL;
        client->con->readbufferlen = 0;
    }

    client->reuse = ICECAST_REUSE_CLOSE;

    client_destroy(client);
    connection_queue(con);
}

void client_destroy(client_t *client)
{
    ICECAST_LOG_DEBUG("Called to destory client %p", client);
    if (client == NULL)
        return;

    fastevent_emit(FASTEVENT_TYPE_CLIENT_DESTROY, FASTEVENT_FLAG_MODIFICATION_ALLOWED, FASTEVENT_DATATYPE_CLIENT, client);

    if (client->reuse != ICECAST_REUSE_CLOSE) {
        /* only reuse the client if we reached the body's EOF. */
        if (client_body_eof(client) == 1) {
            client_reuseconnection(client);
            return;
        }
    }

    /* release the buffer now, as the buffer could be on the source queue
     * and may of disappeared after auth completes */
    client_set_queue(client, NULL);

    if (auth_release_client(client))
        return;

    /* write log entry if ip is set (some things don't set it, like outgoing
     * slave requests
     */
    if (client->respcode && client->parser)
        logging_access(client);
    if (client->con)
        connection_close(client->con);
    if (client->parser)
        httpp_destroy(client->parser);
    if (client->encoding)
        httpp_encoding_release(client->encoding);

    global_lock();
    global.clients--;
    stats_event_args(NULL, "clients", "%d", global.clients);
    global_unlock();

    /* we need to free client specific format data (if any) */
    if (client->free_client_data)
        client->free_client_data(client);

    refobject_unref(client->handler_module);
    free(client->handler_function);
    free(client->uri);
    free(client->username);
    free(client->password);
    free(client->role);
    acl_release(client->acl);

    free(client);
}

/* helper function for reading data from a client */
static ssize_t __client_read_bytes_real(client_t *client, void *buf, size_t len)
{
    /* we have data to read from a refbuf first */
    if (client->refbuf->len < len)
        len = client->refbuf->len;
    memcpy (buf, client->refbuf->data, len);
    if (len < client->refbuf->len) {
        char *ptr = client->refbuf->data;
        memmove (ptr, ptr+len, client->refbuf->len - len);
    }
    client->refbuf->len -= len;
    return len;
}

int client_read_bytes(client_t *client, void *buf, unsigned len)
{
    ssize_t (*reader)(void*, void*, size_t) = (ssize_t(*)(void*,void*,size_t))__client_read_bytes_real;
    void *userdata = client;
    int bytes;

    if (!(client->refbuf && client->refbuf->len)) {
        reader = (ssize_t(*)(void*,void*,size_t))connection_read_bytes;
        userdata = client->con;
    }

    if (client->encoding) {
        bytes = httpp_encoding_read(client->encoding, buf, len, reader, userdata);
    } else {
        bytes = reader(userdata, buf, len);
    }

    if (bytes == -1 && client->con->error)
        ICECAST_LOG_DEBUG("reading from connection has failed");

    fastevent_emit(FASTEVENT_TYPE_CLIENT_READ, FASTEVENT_FLAG_MODIFICATION_ALLOWED, FASTEVENT_DATATYPE_OBRD, client, buf, (size_t)len, (ssize_t)bytes);

    return bytes;
}

static inline void _client_send_report(client_t *client, const char *uuid, const char *message, int http_status, const char *location)
{
    reportxml_t *report;
    admin_format_t admin_format;
    const char *xslt = NULL;

    admin_format = client_get_admin_format_by_content_negotiation(client);

    switch (admin_format) {
        case ADMIN_FORMAT_RAW:
        case ADMIN_FORMAT_JSON:
            xslt = NULL;
        break;
        case ADMIN_FORMAT_HTML:
            xslt = CLIENT_DEFAULT_ERROR_XSL_HTML;
        break;
        case ADMIN_FORMAT_PLAINTEXT:
            xslt = CLIENT_DEFAULT_ERROR_XSL_PLAINTEXT;
        break;
        default:
            client_send_500(client, "Invalid Admin Type");
        break;
    }

    report = client_get_reportxml(uuid, NULL, message);

    client_send_reportxml(client, report, DOCUMENT_DOMAIN_ADMIN, xslt, admin_format, http_status, location);

    refobject_unref(report);
}

void client_send_error_by_error(client_t *client, const icecast_error_t *error)
{

    if (!error) {
         client_send_500(client, "Unknown error ID");
         return;
    }

    if (error->http_status == 500) {
         client_send_500(client, error->message);
        return;
    }

    _client_send_report(client, error->uuid, error->message, error->http_status, NULL);
}
void client_send_error_by_uuid(client_t *client, const char *uuid)
{
    client_send_error_by_error(client, error_get_by_uuid(uuid));
}
void client_send_error_by_id(client_t *client, icecast_error_id_t id)
{
    client_send_error_by_error(client, error_get_by_id(id));
}

void client_send_101(client_t *client, reuse_t reuse)
{
    ssize_t ret;

    if (!client)
        return;

    if (reuse != ICECAST_REUSE_UPGRADETLS) {
        client_send_500(client, "Bad reuse parameter");
        return;
    }

    client->reuse = reuse;

    ret = util_http_build_header(client->refbuf->data, PER_CLIENT_REFBUF_SIZE, 0,
                                 0, 101, NULL,
                                 "text/plain", "utf-8",
                                 NULL, NULL, client);

    snprintf(client->refbuf->data + ret, PER_CLIENT_REFBUF_SIZE - ret,
             "Content-Length: 0\r\nUpgrade: TLS/1.0, HTTP/1.1\r\n\r\n");

    client->respcode = 101;
    client->refbuf->len = strlen(client->refbuf->data);

    fastevent_emit(FASTEVENT_TYPE_CLIENT_SEND_RESPONSE, FASTEVENT_FLAG_MODIFICATION_ALLOWED, FASTEVENT_DATATYPE_CLIENT, client);

    fserve_add_client(client, NULL);
}

void client_send_204(client_t *client)
{
    client_send_buffer(client, 204, NULL, NULL, NULL, 0, NULL);
}

void client_send_426(client_t *client, reuse_t reuse)
{
    ssize_t ret;

    if (!client)
        return;

    if (reuse != ICECAST_REUSE_UPGRADETLS) {
        client_send_500(client, "Bad reuse parameter");
        return;
    }

    client->reuse = reuse;
    ret = util_http_build_header(client->refbuf->data, PER_CLIENT_REFBUF_SIZE, 0,
                                 0, 426, NULL,
                                 "text/plain", "utf-8",
                                 NULL, NULL, client);

    snprintf(client->refbuf->data + ret, PER_CLIENT_REFBUF_SIZE - ret,
             "Content-Length: 0\r\nUpgrade: TLS/1.0, HTTP/1.1\r\n\r\n");

    client->respcode = 426;
    client->refbuf->len = strlen(client->refbuf->data);

    client->reuse = ICECAST_REUSE_KEEPALIVE;

    fastevent_emit(FASTEVENT_TYPE_CLIENT_SEND_RESPONSE, FASTEVENT_FLAG_MODIFICATION_ALLOWED, FASTEVENT_DATATYPE_CLIENT, client);

    fserve_add_client(client, NULL);
}

/* this function is designed to work even if client is in bad state */
static inline void client_send_500(client_t *client, const char *message)
{
    const char header[] = "HTTP/1.0 500 Internal Server Error\r\nContent-Type: text/plain; charset=utf-8\r\n\r\n"
                          "500 - Internal Server Error\n---------------------------\n";
    const ssize_t header_len = sizeof(header) - 1;
    ssize_t ret;

    client->respcode = 500;
    client->refbuf->len = 0;
    fastevent_emit(FASTEVENT_TYPE_CLIENT_SEND_RESPONSE, FASTEVENT_FLAG_MODIFICATION_ALLOWED, FASTEVENT_DATATYPE_CLIENT, client);

    ret = client_send_bytes(client, header, header_len);

    /* only send message if we have one AND if header could have transmitted completly */
    if (message && ret == header_len)
        client_send_bytes(client, message, strlen(message));

    client_destroy(client);
}

void client_send_buffer(client_t *client, int status, const char *mediatype, const char *charset, const char *buffer, ssize_t len, const char *extra_headers)
{
    source_t *source;
    size_t buf_len_ours;
    size_t buf_len;
    ssize_t ret;
    int headerlen;
    char header[128];
    size_t extra_headers_len;

    if (!client)
        return;

    if (status < 200 || status > 599 || (!buffer && len)) {
        client_send_500(client, "BUG.");
        return;
    }

    if (len < 0)
        len = strlen(buffer);

    if (!extra_headers)
        extra_headers = "";

    extra_headers_len = strlen(extra_headers);

    headerlen = snprintf(header, sizeof(header), "Content-Length: %lld\r\n", (long long int)len);
    if (headerlen < 1 || headerlen >= (ssize_t)sizeof(header)) {
        ICECAST_LOG_ERROR("Dropping client as we can not build response headers.");
        client_send_error_by_id(client, ICECAST_ERROR_GEN_HEADER_GEN_FAILED);
    }

    buf_len_ours = len + headerlen + extra_headers_len + 2;
    buf_len = buf_len_ours + 1024;

    if (buf_len < 4096)
        buf_len = 4096;

    client_set_queue(client, NULL);
    client->refbuf = refbuf_new(buf_len);
    client->reuse = ICECAST_REUSE_KEEPALIVE;

    avl_tree_rlock(global.source_tree);
    source = source_find_mount_raw(client->uri);

    ret = util_http_build_header(client->refbuf->data, buf_len, 0,
            0, status, NULL,
            mediatype, charset,
            NULL, source, client);

    if (ret < 0) {
        avl_tree_unlock(global.source_tree);
        ICECAST_LOG_ERROR("Dropping client as we can not build response headers.");
        client_send_error_by_id(client, ICECAST_ERROR_GEN_HEADER_GEN_FAILED);
        return;
    } else if (buf_len < (ret + buf_len_ours)) {
        void *new_data;
        buf_len = buf_len_ours + ret + 64;
        new_data = realloc(client->refbuf->data, buf_len);
        if (new_data) {
            ICECAST_LOG_DEBUG("Client buffer reallocation succeeded.");
            client->refbuf->data = new_data;
            client->refbuf->len = buf_len;
            ret = util_http_build_header(client->refbuf->data, buf_len, 0,
                    0, status, NULL,
                    mediatype, charset,
                    NULL, source, client);
            if (ret == -1 || buf_len < (ret + buf_len_ours)) {
                avl_tree_unlock(global.source_tree);
                ICECAST_LOG_ERROR("Dropping client as we can not build response headers.");
                client_send_error_by_id(client, ICECAST_ERROR_GEN_HEADER_GEN_FAILED);
                return;
            }
        } else {
            avl_tree_unlock(global.source_tree);
            ICECAST_LOG_ERROR("Client buffer reallocation failed. Dropping client.");
            client_send_error_by_id(client, ICECAST_ERROR_GEN_BUFFER_REALLOC);
            return;
        }
    }
    avl_tree_unlock(global.source_tree);

    memcpy(client->refbuf->data + ret, header, headerlen);
    ret += headerlen;

    memcpy(client->refbuf->data + ret, extra_headers, extra_headers_len);
    ret += extra_headers_len;

    memcpy(client->refbuf->data + ret, "\r\n", 2);
    ret += 2;

    if (len) {
        memcpy(client->refbuf->data + ret, buffer, len);
        ret += len;
    }

    client->refbuf->len = ret;
    client->respcode = status;

    fastevent_emit(FASTEVENT_TYPE_CLIENT_SEND_RESPONSE, FASTEVENT_FLAG_MODIFICATION_ALLOWED, FASTEVENT_DATATYPE_CLIENT, client);

    fserve_add_client(client, NULL);
}

void client_send_redirect(client_t *client, const char *uuid, int status, const char *location)
{
    _client_send_report(client, uuid, "Redirecting", status, location);
}

/* this function sends a reportxml file to the client in the prefered format. */
void client_send_reportxml(client_t *client, reportxml_t *report, document_domain_t domain, const char *xsl, admin_format_t admin_format_hint, int status, const char *location)
{
    admin_format_t admin_format;
    xmlDocPtr doc;

    if (!client)
        return;

    if (!report) {
        ICECAST_LOG_ERROR("No report xml given. Sending 500 to client %p", client);
        client_send_500(client, "No report.");
        return;
    }

    if (!status)
        status = 200;

    if (admin_format_hint == ADMIN_FORMAT_AUTO) {
        admin_format = client_get_admin_format_by_content_negotiation(client);
    } else {
        admin_format = admin_format_hint;
    }

    if (!xsl) {
        switch (admin_format) {
            case ADMIN_FORMAT_RAW:
            case ADMIN_FORMAT_JSON:
                /* noop, we don't need to set xsl */
            break;
            case ADMIN_FORMAT_HTML:
                xsl = CLIENT_DEFAULT_REPORT_XSL_HTML;
            break;
            case ADMIN_FORMAT_PLAINTEXT:
                xsl = CLIENT_DEFAULT_REPORT_XSL_PLAINTEXT;
            break;
            default:
                ICECAST_LOG_ERROR("Unsupported admin format and no XSLT file given. Sending 500 to client %p", client);
                client_send_500(client, "Unsupported admin format.");
                return;
            break;
        }
    } else if (admin_format_hint == ADMIN_FORMAT_AUTO) {
        ICECAST_LOG_ERROR("No explicit admin format but XSLT file given. BUG. Sending 500 to client %p", client);
        client_send_500(client, "Admin type AUTO but XSLT.");
        return;
    }

    doc = reportxml_render_xmldoc(report, admin_format == ADMIN_FORMAT_RAW || admin_format == ADMIN_FORMAT_JSON);
    if (!doc) {
        ICECAST_LOG_ERROR("Can not render XML Document from report. Sending 500 to client %p", client);
        client_send_500(client, "Can not render XML Document from report.");
        return;
    }

    if (admin_format == ADMIN_FORMAT_RAW || admin_format == ADMIN_FORMAT_JSON) {
        char extra_header[512] = "";

        if (location) {
            int res = snprintf(extra_header, sizeof(extra_header), "Location: %s\r\n", location);
            if (res < 0 || res >= (ssize_t)sizeof(extra_header)) {
                client_send_error_by_id(client, ICECAST_ERROR_GEN_HEADER_GEN_FAILED);
                return;
            }
        }

        if (admin_format == ADMIN_FORMAT_RAW) {
            xmlChar *buff = NULL;
            int len = 0;
            xmlDocDumpMemory(doc, &buff, &len);
            client_send_buffer(client, status, "text/xml", "utf-8", (const char *)buff, len, extra_header);
            xmlFree(buff);
        } else {
            char *json = xml2json_render_doc_simple(doc, NULL);
            client_send_buffer(client, status, "application/json", "utf-8", json, -1, extra_header);
            free(json);
        }
    } else {
        char *fullpath_xslt_template;
        const char *document_domain_path;
        ssize_t fullpath_xslt_template_len;
        ice_config_t *config;

        config = config_get_config();
        switch (domain) {
            case DOCUMENT_DOMAIN_WEB:
                document_domain_path = config->webroot_dir;
            break;
            case DOCUMENT_DOMAIN_ADMIN:
                document_domain_path = config->adminroot_dir;
            break;
            default:
                config_release_config();
                ICECAST_LOG_ERROR("Invalid document domain. Sending 500 to client %p", client);
                client_send_500(client, "Invalid document domain.");
                return;
            break;
        }
        fullpath_xslt_template_len = strlen(document_domain_path) + strlen(xsl) + strlen(PATH_SEPARATOR) + 1;
        fullpath_xslt_template = malloc(fullpath_xslt_template_len);
        snprintf(fullpath_xslt_template, fullpath_xslt_template_len, "%s%s%s", document_domain_path, PATH_SEPARATOR, xsl);
        config_release_config();

        ICECAST_LOG_DEBUG("Sending XSLT (%s)", fullpath_xslt_template);
        fastevent_emit(FASTEVENT_TYPE_CLIENT_SEND_RESPONSE, FASTEVENT_FLAG_MODIFICATION_ALLOWED, FASTEVENT_DATATYPE_CLIENT, client);
        xslt_transform(doc, fullpath_xslt_template, client, status, location, NULL);
        free(fullpath_xslt_template);
    }

    xmlFreeDoc(doc);
}

static void client_get_reportxml__add_basic_stats(reportxml_t *report)
{
    reportxml_node_t *rootnode, *extension;
    xmlNodePtr xmlroot;
    xmlNodePtr modules;

    rootnode = reportxml_get_root_node(report);

    extension = reportxml_node_new(REPORTXML_NODE_TYPE_EXTENSION, NULL, NULL, NULL);
    reportxml_node_set_attribute(extension, "application", ADMIN_ICESTATS_LEGACY_EXTENSION_APPLICATION);

    reportxml_node_add_child(rootnode, extension);

    refobject_unref(rootnode);

    xmlroot = xmlNewNode(NULL, XMLSTR("icestats"));
    xmlSetProp(xmlroot, XMLSTR("xmlns"), XMLSTR(XMLNS_LEGACY_STATS));
    modules = module_container_get_modulelist_as_xml(global.modulecontainer);
    xmlAddChild(xmlroot, modules);


    reportxml_node_add_xml_child(extension, xmlroot);
    refobject_unref(extension);
    xmlFreeNode(xmlroot);
}

reportxml_t *client_get_reportxml(const char *state_definition, const char *state_akindof, const char *state_text)
{
    reportxml_t *report = NULL;

    if (state_definition) {
        ice_config_t *config;

        config = config_get_config();
        report = reportxml_database_build_report(config->reportxml_db, state_definition, -1);
        config_release_config();
    }

    if (!report) {
        reportxml_node_t *rootnode, *incidentnode, *statenode;

        report = refobject_new(reportxml_t);
        rootnode = reportxml_get_root_node(report);
        incidentnode = reportxml_node_new(REPORTXML_NODE_TYPE_INCIDENT, NULL, NULL, NULL);
        statenode = reportxml_node_new(REPORTXML_NODE_TYPE_STATE, NULL, state_definition, state_akindof);

        if (state_text) {
            reportxml_node_t *textnode;

            textnode = reportxml_node_new(REPORTXML_NODE_TYPE_TEXT, NULL, NULL, NULL);
            reportxml_node_set_content(textnode, state_text);
            reportxml_node_add_child(statenode, textnode);
            refobject_unref(textnode);
        }

        reportxml_node_add_child(incidentnode, statenode);
        reportxml_node_add_child(rootnode, incidentnode);
        refobject_unref(statenode);
        refobject_unref(incidentnode);
        refobject_unref(rootnode);
    }

    client_get_reportxml__add_basic_stats(report);

    return report;
}

admin_format_t client_get_admin_format_by_content_negotiation(client_t *client)
{
    const char *pref;

    if (!client || !client->parser)
        return CLIENT_DEFAULT_ADMIN_FORMAT;

    pref = util_http_select_best(httpp_getvar(client->parser, "accept"), "text/xml", "text/html", "text/plain", "application/json", (const char*)NULL);

    if (strcmp(pref, "text/xml") == 0) {
        return ADMIN_FORMAT_RAW;
    } else if (strcmp(pref, "text/html") == 0) {
        return ADMIN_FORMAT_HTML;
    } else if (strcmp(pref, "text/plain") == 0) {
        return ADMIN_FORMAT_PLAINTEXT;
    } else if (strcmp(pref, "application/json") == 0) {
        return ADMIN_FORMAT_JSON;
    } else {
        return CLIENT_DEFAULT_ADMIN_FORMAT;
    }
}

/* helper function for sending the data to a client */
int client_send_bytes(client_t *client, const void *buf, unsigned len)
{
    int ret = connection_send_bytes(client->con, buf, len);

    if (client->con->error)
        ICECAST_LOG_DEBUG("Client connection died");

    fastevent_emit(FASTEVENT_TYPE_CLIENT_WRITE, FASTEVENT_FLAG_NONE, FASTEVENT_DATATYPE_OBRD, client, buf, (size_t)len, (ssize_t)ret);

    return ret;
}

void client_set_queue(client_t *client, refbuf_t *refbuf)
{
    refbuf_t *to_release = client->refbuf;

    client->refbuf = refbuf;
    if (refbuf)
        refbuf_addref(client->refbuf);
    client->pos = 0;
    if (to_release)
        refbuf_release(to_release);
}

ssize_t client_body_read(client_t *client, void *buf, size_t len)
{
    ssize_t ret;

    ICECAST_LOG_DDEBUG("Reading from body (client=%p)", client);

    if (client->request_body_length != -1) {
        size_t left = (size_t)client->request_body_length - client->request_body_read;
        if (len > left) {
            ICECAST_LOG_DEBUG("Limiting read request to left over body size: left %zu byte, requested %zu byte", left, len);
            len = left;
        }
    }

    ret = client_read_bytes(client, buf, len);

    if (ret > 0) {
        client->request_body_read += ret;
    }

    fastevent_emit(FASTEVENT_TYPE_CLIENT_READ_BODY, FASTEVENT_FLAG_MODIFICATION_ALLOWED, FASTEVENT_DATATYPE_OBRD, client, buf, len, ret);

    return ret;
}

/* we might un-static this if needed at some time in distant future. -- ph3-der-loewe, 2018-04-17 */
static int client_eof(client_t *client)
{
    if (!client)
        return -1;

    if (!client->con)
        return 0;

    if (client->con->tls && tls_got_shutdown(client->con->tls) > 0)
        client->con->error = 1;

    if (client->con->error)
        return 1;

    return 0;
}

int client_body_eof(client_t *client)
{
    int ret = -1;

    if (!client)
        return -1;

    if (client->request_body_length != -1 && client->request_body_read == (size_t)client->request_body_length) {
        ICECAST_LOG_DDEBUG("Reached given body length (client=%p)", client);
        ret = 1;
    } else if (client->encoding) {
        ICECAST_LOG_DDEBUG("Looking for body EOF with encoding (client=%p)", client);
        ret = httpp_encoding_eof(client->encoding, (int(*)(void*))client_eof, client);
    } else {
        ICECAST_LOG_DDEBUG("Looking for body EOF without encoding (client=%p)", client);
        ret = client_eof(client);
    }

    ICECAST_LOG_DDEBUG("... result is: %i (client=%p)", ret, client);
    return ret;
}

client_slurp_result_t client_body_slurp(client_t *client, void *buf, size_t *len)
{
    if (!client || !buf || !len)
        return CLIENT_SLURP_ERROR;

    if (client->request_body_length != -1) {
        /* non-streaming mode */
        size_t left = (size_t)client->request_body_length - client->request_body_read;

        if (!left)
            return CLIENT_SLURP_SUCCESS;

        if (*len < (size_t)client->request_body_length)
            return CLIENT_SLURP_BUFFER_TO_SMALL;

        if (left > 2048)
            left = 2048;

        client_body_read(client, buf + client->request_body_read, left);

        if ((size_t)client->request_body_length == client->request_body_read) {
            *len = client->request_body_read;

            return CLIENT_SLURP_SUCCESS;
        } else {
            return CLIENT_SLURP_NEEDS_MORE_DATA;
        }
    } else {
        /* streaming mode */
        size_t left = *len - client->request_body_read;
        int ret;

        if (left) {
            if (left > 2048)
                left = 2048;

            client_body_read(client, buf + client->request_body_read, left);
        }

        ret = client_body_eof(client);
        switch (ret) {
            case 0:
                if (*len == client->request_body_read) {
                    return CLIENT_SLURP_BUFFER_TO_SMALL;
                }
                return CLIENT_SLURP_NEEDS_MORE_DATA;
            break;
            case 1:
                return CLIENT_SLURP_SUCCESS;
            break;
            default:
                return CLIENT_SLURP_ERROR;
            break;
        }
    }
}

client_slurp_result_t client_body_skip(client_t *client)
{
    char buf[2048];
    int ret;

    ICECAST_LOG_DEBUG("Slurping client %p", client);

    if (!client) {
        ICECAST_LOG_DEBUG("Slurping client %p ... failed", client);
        return CLIENT_SLURP_ERROR;
    }

    if (client->request_body_length != -1) {
        size_t left = (size_t)client->request_body_length - client->request_body_read;

        if (!left) {
            ICECAST_LOG_DEBUG("Slurping client %p ... was a success", client);
            return CLIENT_SLURP_SUCCESS;
        }

        if (left > sizeof(buf))
            left = sizeof(buf);

        client_body_read(client, buf, left);

        if ((size_t)client->request_body_length == client->request_body_read) {
            ICECAST_LOG_DEBUG("Slurping client %p ... was a success", client);
            return CLIENT_SLURP_SUCCESS;
        } else {
            ICECAST_LOG_DEBUG("Slurping client %p ... needs more data", client);
            return CLIENT_SLURP_NEEDS_MORE_DATA;
        }
    } else {
        client_body_read(client, buf, sizeof(buf));
    }

    ret = client_body_eof(client);
    switch (ret) {
        case 0:
            ICECAST_LOG_DEBUG("Slurping client %p ... needs more data", client);
            return CLIENT_SLURP_NEEDS_MORE_DATA;
        break;
        case 1:
            ICECAST_LOG_DEBUG("Slurping client %p ... was a success", client);
            return CLIENT_SLURP_SUCCESS;
        break;
        default:
            ICECAST_LOG_DEBUG("Slurping client %p ... failed", client);
            return CLIENT_SLURP_ERROR;
        break;
    }
}

ssize_t client_get_baseurl(client_t *client, listensocket_t *listensocket, char *buf, size_t len, const char *user, const char *pw, const char *prefix, const char *suffix0, const char *suffix1)
{
    const listener_t *listener = NULL;
    const ice_config_t *config = NULL;
    const char *host = NULL;
    const char *proto = "http";
    int port = 0;
    ssize_t ret;
    tlsmode_t tlsmode = ICECAST_TLSMODE_AUTO;
    protocol_t protocol = ICECAST_PROTOCOL_HTTP;

    if (!buf || !len)
        return -1;

    if (!prefix)
        prefix = "";

    if (!suffix0)
        suffix0 = "";

    if (!suffix1)
        suffix1 = "";

    if (client) {
        host = httpp_getvar(client->parser, "host");

        /* at least a couple of players (fb2k/winamp) are reported to send a
         * host header but without the port number. So if we are missing the
         * port then lets treat it as if no host line was sent */
        if (host && strchr(host, ':') == NULL)
            host = NULL;

        listensocket = client->con->listensocket_effective;
        tlsmode = client->con->tlsmode;
        protocol = client->protocol;
    }

    if (!host && listensocket) {
        listener = listensocket_get_listener(listensocket);
        if (listener) {
            if (listener->bind_address && strncmp(listener->bind_address, "0.", 2) != 0 && strncmp(listener->bind_address, "::", 2) != 0)
                host = listener->bind_address;
            port = listener->port;
            if (!client)
                tlsmode = listener->tls;
        }
    }

    if (!host) {
        config = config_get_config();
        host = config->hostname;
        if (!port)
            port = config->port;
    }

    switch (tlsmode) {
        case ICECAST_TLSMODE_DISABLED:
        case ICECAST_TLSMODE_AUTO:
            switch (protocol) {
                case ICECAST_PROTOCOL_HTTP: proto = "http"; break;
                case ICECAST_PROTOCOL_SHOUTCAST: proto = "icy"; break;
            }
            break;
        case ICECAST_TLSMODE_AUTO_NO_PLAIN:
        case ICECAST_TLSMODE_RFC2817:
        case ICECAST_TLSMODE_RFC2818:
            switch (protocol) {
                case ICECAST_PROTOCOL_HTTP: proto = "https"; break;
                case ICECAST_PROTOCOL_SHOUTCAST: proto = "icys"; break;
            }
            break;
    }

    if (host && port) {
        ret = snprintf(buf, len, "%s%s://%s%s%s%s%s:%i%s%s", prefix, proto, user ? user : "", pw ? ":" : "", pw ? pw : "", (user || pw) ? "@" : "", host, port, suffix0, suffix1);
    } else if (host) {
        ret = snprintf(buf, len, "%s%s://%s%s%s%s%s%s%s", prefix, proto, user ? user : "", pw ? ":" : "", pw ? pw : "", (user || pw) ? "@" : "", host, suffix0, suffix1);
    } else {
        ret = -1;
    }

    if (config)
        config_release_config();
    if (listener)
        listensocket_release_listener(listensocket);

    return ret;
}
