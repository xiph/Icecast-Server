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

/** 
 * Client authentication functions
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>

#include "auth.h"
#include "source.h"
#include "client.h"
#include "cfgfile.h"
#include "httpp/httpp.h"
#include "md5.h"

#include "logging.h"
#define CATMODULE "auth"


int auth_is_listener_connected(source_t *source, char *username)
{
    client_t *client;
    avl_node *client_node;

    avl_tree_rlock(source->client_tree);

    client_node = avl_get_first(source->client_tree);
    while(client_node) {
        client = (client_t *)client_node->key;
        if (client->username) {
            if (!strcmp(client->username, username)) {
                avl_tree_unlock(source->client_tree);
                return 1;
            }
        }
        client_node = avl_get_next(client_node);
    }

    avl_tree_unlock(source->client_tree);
    return 0;

}

auth_result auth_check_client(source_t *source, client_t *client)
{
    auth_t *authenticator = source->authenticator;
    auth_result result;

    if(authenticator) {
        /* This will look something like "Basic QWxhZGRpbjpvcGVuIHNlc2FtZQ==" */
        char *header = httpp_getvar(client->parser, "authorization");
        char *userpass, *tmp;
        char *username, *password;
    
        if(header == NULL)
            return AUTH_FAILED;
    
        if(strncmp(header, "Basic ", 6)) {
            INFO0("Authorization not using Basic");
            return 0;
        }
        
        userpass = util_base64_decode(header+6);
        if(userpass == NULL) {
            WARN1("Base64 decode of Authorization header \"%s\" failed",
                    header+6);
            return AUTH_FAILED;
        }
        
        tmp = strchr(userpass, ':');
        if(!tmp) { 
            free(userpass);
            return AUTH_FAILED;
        }

        *tmp = 0;
        username = userpass;
        password = tmp+1;

        result = authenticator->authenticate(
                authenticator, source, username, password);

        if(result == AUTH_OK)
            client->username = strdup(username);

        free(userpass);

        return result;
    }
    else
        return AUTH_FAILED;
}

static auth_t *auth_get_htpasswd_auth(config_options_t *options);

auth_t *auth_get_authenticator(char *type, config_options_t *options)
{
    auth_t *auth = NULL;
    if(!strcmp(type, "htpasswd")) {
        auth = auth_get_htpasswd_auth(options);
        auth->type = strdup(type);
    }
    else {
        ERROR1("Unrecognised authenticator type: \"%s\"", type);
        return NULL;
    }

    if(!auth)
        ERROR1("Couldn't configure authenticator of type \"%s\"", type);
    
    return auth;
}

typedef struct {
    char *filename;
    int allow_duplicate_users;
    rwlock_t file_rwlock;
} htpasswd_auth_state;

static void htpasswd_clear(auth_t *self) {
    htpasswd_auth_state *state = self->state;
    free(state->filename);
    thread_rwlock_destroy(&state->file_rwlock);
    free(state);
    free(self->type);
    free(self);
}

static int get_line(FILE *file, char *buf, int len)
{
    if(fgets(buf, len, file)) {
        int len = strlen(buf);
        if(len > 0 && buf[len-1] == '\n') {
            buf[--len] = 0;
            if(len > 0 && buf[len-1] == '\r')
                buf[--len] = 0;
        }
        return 1;
    }
    return 0;
}

/* md5 hash */
static char *get_hash(char *data, int len)
{
    struct MD5Context context;
    unsigned char digest[16];

    MD5Init(&context);

    MD5Update(&context, data, len);

    MD5Final(digest, &context);

    return util_bin_to_hex(digest, 16);
}

#define MAX_LINE_LEN 512

/* Not efficient; opens and scans the entire file for every request */
static auth_result htpasswd_auth(auth_t *auth, source_t *source, char *username, char *password)
{
    htpasswd_auth_state *state = auth->state;
    FILE *passwdfile = NULL;
    char line[MAX_LINE_LEN];
    char *sep;

    thread_rwlock_rlock(&state->file_rwlock);
    if (!state->allow_duplicate_users) {
        if (auth_is_listener_connected(source, username)) {
            thread_rwlock_unlock(&state->file_rwlock);
            return AUTH_FORBIDDEN;
        }
    }
    passwdfile = fopen(state->filename, "rb");
    if(passwdfile == NULL) {
        WARN2("Failed to open authentication database \"%s\": %s", 
                state->filename, strerror(errno));
        thread_rwlock_unlock(&state->file_rwlock);
        return AUTH_FAILED;
    }

    while(get_line(passwdfile, line, MAX_LINE_LEN)) {
        if(!line[0] || line[0] == '#')
            continue;

        sep = strchr(line, ':');
        if(sep == NULL) {
            DEBUG0("No separator in line");
            continue;
        }

        *sep = 0;
        if(!strcmp(username, line)) {
            /* Found our user, now: does the hash of password match hash? */
            char *hash = sep+1;
            char *hashed_password = get_hash(password, strlen(password));
            if(!strcmp(hash, hashed_password)) {
                fclose(passwdfile);
                free(hashed_password);
                thread_rwlock_unlock(&state->file_rwlock);
                return AUTH_OK;
            }
            free(hashed_password);
            /* We don't keep searching through the file */
            break; 
        }
    }

    fclose(passwdfile);

    thread_rwlock_unlock(&state->file_rwlock);
    return AUTH_FAILED;
}

static auth_t *auth_get_htpasswd_auth(config_options_t *options)
{
    auth_t *authenticator = calloc(1, sizeof(auth_t));
    htpasswd_auth_state *state;

    authenticator->authenticate = htpasswd_auth;
    authenticator->free = htpasswd_clear;

    state = calloc(1, sizeof(htpasswd_auth_state));

    state->allow_duplicate_users = 1;
    while(options) {
        if(!strcmp(options->name, "filename"))
            state->filename = strdup(options->value);
        if(!strcmp(options->name, "allow_duplicate_users"))
            state->allow_duplicate_users = atoi(options->value);
        options = options->next;
    }

    if(!state->filename) {
        free(state);
        free(authenticator);
        ERROR0("No filename given in options for authenticator.");
        return NULL;
    }

    authenticator->state = state;
    DEBUG1("Configured htpasswd authentication using password file %s", 
            state->filename);

    thread_rwlock_create(&state->file_rwlock);

    return authenticator;
}

int auth_htpasswd_existing_user(auth_t *auth, char *username)
{
    FILE *passwdfile;
    htpasswd_auth_state *state;
    int ret = AUTH_OK;
    char line[MAX_LINE_LEN];
    char *sep;

    state = auth->state;
    passwdfile = fopen(state->filename, "rb");

    if(passwdfile == NULL) {
        WARN2("Failed to open authentication database \"%s\": %s", 
                state->filename, strerror(errno));
        return AUTH_FAILED;
    }
    while(get_line(passwdfile, line, MAX_LINE_LEN)) {
        if(!line[0] || line[0] == '#')
            continue;
        sep = strchr(line, ':');
        if(sep == NULL) {
            DEBUG0("No separator in line");
            continue;
        }
        *sep = 0;
        if (!strcmp(username, line)) {
            /* We found the user, break out of the loop */
            ret = AUTH_USEREXISTS;
            break;
        }
    }

    fclose(passwdfile);
    return ret;

}
int auth_htpasswd_adduser(auth_t *auth, char *username, char *password)
{
    FILE *passwdfile;
    char *hashed_password = NULL;
    htpasswd_auth_state *state;

    if (auth_htpasswd_existing_user(auth, username) == AUTH_USEREXISTS) {
        return AUTH_USEREXISTS;
    }
    state = auth->state;
    passwdfile = fopen(state->filename, "ab");

    if(passwdfile == NULL) {
        WARN2("Failed to open authentication database \"%s\": %s", 
                state->filename, strerror(errno));
        return AUTH_FAILED;
    }

    hashed_password = get_hash(password, strlen(password));
    if (hashed_password) {
        fprintf(passwdfile, "%s:%s\n", username, hashed_password);
        free(hashed_password);
    }

    fclose(passwdfile);
    return AUTH_USERADDED;
}

int auth_adduser(source_t *source, char *username, char *password)
{
    int ret = 0;
    htpasswd_auth_state *state;

    if (source->authenticator) {
        if (!strcmp(source->authenticator->type, "htpasswd")) {
            state = source->authenticator->state;
            thread_rwlock_wlock(&state->file_rwlock);
            ret = auth_htpasswd_adduser(source->authenticator, username, password);
            thread_rwlock_unlock(&state->file_rwlock);
        }
    }
    return ret;
}

int auth_htpasswd_deleteuser(auth_t *auth, char *username)
{
    FILE *passwdfile;
    FILE *tmp_passwdfile;
    htpasswd_auth_state *state;
    char line[MAX_LINE_LEN];
    char *sep;
    char *tmpfile = NULL;
    int tmpfile_len = 0;

    state = auth->state;
    passwdfile = fopen(state->filename, "rb");

    if(passwdfile == NULL) {
        WARN2("Failed to open authentication database \"%s\": %s", 
                state->filename, strerror(errno));
        return AUTH_FAILED;
    }
    tmpfile_len = strlen(state->filename) + 6;
    tmpfile = calloc(1, tmpfile_len);
    sprintf(tmpfile, ".%s.tmp", state->filename);

    tmp_passwdfile = fopen(tmpfile, "wb");

    if(tmp_passwdfile == NULL) {
        WARN2("Failed to open temporary authentication database \"%s\": %s", 
                tmpfile, strerror(errno));
        fclose(passwdfile);
        free(tmpfile);
        return AUTH_FAILED;
    }


    while(get_line(passwdfile, line, MAX_LINE_LEN)) {
        if(!line[0] || line[0] == '#')
            continue;

        sep = strchr(line, ':');
        if(sep == NULL) {
            DEBUG0("No separator in line");
            continue;
        }

        *sep = 0;
        if (strcmp(username, line)) {
            /* We did not match on the user, so copy it to the temp file */
            /* and put the : back in */
            *sep = ':';
            fprintf(tmp_passwdfile, "%s\n", line);
        }
    }

    fclose(tmp_passwdfile);
    fclose(passwdfile);

    /* Now move the contents of the tmp file to the original */
#ifdef _WIN32
    /* Windows won't let us rename a file if the destination file
       exists...so, lets remove the original first */
    if (remove(state->filename) != 0) {
        ERROR3("Problem moving temp authentication file to original \"%s\" - \"%s\": %s", 
                tmpfile, state->filename, strerror(errno));
    }
    else {
#endif
        if (rename(tmpfile, state->filename) != 0) {
            ERROR3("Problem moving temp authentication file to original \"%s\" - \"%s\": %s", 
                tmpfile, state->filename, strerror(errno));
	}
#ifdef _WIN32
    }
#endif

    free(tmpfile);

    return AUTH_USERDELETED;
}
int auth_deleteuser(source_t *source, char *username)
{
    htpasswd_auth_state *state;

    int ret = 0;
    if (source->authenticator) {
        if (!strcmp(source->authenticator->type, "htpasswd")) {
            state = source->authenticator->state;
            thread_rwlock_wlock(&state->file_rwlock);
            ret = auth_htpasswd_deleteuser(source->authenticator, username);
            thread_rwlock_unlock(&state->file_rwlock);
        }
    }
    return ret;
}

int auth_get_htpasswd_userlist(auth_t *auth, xmlNodePtr srcnode)
{
    htpasswd_auth_state *state;
    FILE *passwdfile;
    char line[MAX_LINE_LEN];
    char *sep;
    char *passwd;
    xmlNodePtr newnode;

    state = auth->state;

    passwdfile = fopen(state->filename, "rb");

    if(passwdfile == NULL) {
        WARN2("Failed to open authentication database \"%s\": %s", 
                state->filename, strerror(errno));
        return AUTH_FAILED;
    }

    while(get_line(passwdfile, line, MAX_LINE_LEN)) {
        if(!line[0] || line[0] == '#')
            continue;

        sep = strchr(line, ':');
        if(sep == NULL) {
            DEBUG0("No separator in line");
            continue;
        }

        *sep = 0;
        newnode = xmlNewChild(srcnode, NULL, "User", NULL);
        xmlNewChild(newnode, NULL, "username", line);
        passwd = sep+1;
        xmlNewChild(newnode, NULL, "password", passwd);
    }

    fclose(passwdfile);
    return AUTH_OK;
}

int auth_get_userlist(source_t *source, xmlNodePtr srcnode)
{
    int ret = 0;
    htpasswd_auth_state *state;

    if (source->authenticator) {
        if (!strcmp(source->authenticator->type, "htpasswd")) {
            state = source->authenticator->state;
            thread_rwlock_rlock(&state->file_rwlock);
            ret = auth_get_htpasswd_userlist(source->authenticator, srcnode);
            thread_rwlock_unlock(&state->file_rwlock);
        }
    }
    return ret;
}

