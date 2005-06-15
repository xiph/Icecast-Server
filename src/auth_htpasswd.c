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
#include <sys/types.h>
#include <sys/stat.h>

#include "auth.h"
#include "source.h"
#include "client.h"
#include "cfgfile.h"
#include "httpp/httpp.h"
#include "md5.h"

#include "logging.h"
#define CATMODULE "auth_htpasswd"

static auth_result htpasswd_adduser (auth_t *auth, const char *username, const char *password);
static auth_result htpasswd_deleteuser(auth_t *auth, const char *username);
static auth_result htpasswd_userlist(auth_t *auth, xmlNodePtr srcnode);

typedef struct {
    char *filename;
    rwlock_t file_rwlock;
} htpasswd_auth_state;

static void htpasswd_clear(auth_t *self) {
    htpasswd_auth_state *state = self->state;
    free(state->filename);
    thread_rwlock_destroy(&state->file_rwlock);
    free(state);
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
static char *get_hash(const char *data, int len)
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
static auth_result htpasswd_auth (auth_client *auth_user)
{
    auth_t *auth = auth_user->client->auth;
    htpasswd_auth_state *state = auth->state;
    client_t *client = auth_user->client;
    FILE *passwdfile;
    char line[MAX_LINE_LEN];
    char *sep;

    if (client->username == NULL || client->password == NULL)
        return AUTH_FAILED;

    passwdfile = fopen(state->filename, "rb");
    thread_rwlock_rlock(&state->file_rwlock);
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
        if(!strcmp(client->username, line)) {
            /* Found our user, now: does the hash of password match hash? */
            char *hash = sep+1;
            char *hashed_password = get_hash(client->password, strlen(client->password));
            if(!strcmp(hash, hashed_password)) {
                fclose(passwdfile);
                free(hashed_password);
                thread_rwlock_unlock(&state->file_rwlock);
                if (auth_postprocess_client (auth_user) < 0)
                    return AUTH_FAILED;
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

int  auth_get_htpasswd_auth (auth_t *authenticator, config_options_t *options)
{
    htpasswd_auth_state *state;

    authenticator->authenticate = htpasswd_auth;
    authenticator->free = htpasswd_clear;
    authenticator->adduser = htpasswd_adduser;
    authenticator->deleteuser = htpasswd_deleteuser;
    authenticator->listuser = htpasswd_userlist;

    state = calloc(1, sizeof(htpasswd_auth_state));

    while(options) {
        if(!strcmp(options->name, "filename"))
            state->filename = strdup(options->value);
        options = options->next;
    }

    if(!state->filename) {
        free(state);
        ERROR0("No filename given in options for authenticator.");
        return -1;
    }

    authenticator->state = state;
    DEBUG1("Configured htpasswd authentication using password file %s", 
            state->filename);

    thread_rwlock_create(&state->file_rwlock);

    return 0;
}

int auth_htpasswd_existing_user(auth_t *auth, const char *username)
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


static int _auth_htpasswd_adduser(auth_t *auth, const char *username, const char *password)
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


static auth_result htpasswd_adduser (auth_t *auth, const char *username, const char *password)
{
    auth_result ret = 0;
    htpasswd_auth_state *state;

    state = auth->state;
    thread_rwlock_wlock (&state->file_rwlock);
    ret = _auth_htpasswd_adduser (auth, username, password);
    thread_rwlock_unlock (&state->file_rwlock);

    return ret;
}


static auth_result htpasswd_deleteuser(auth_t *auth, const char *username)
{
    FILE *passwdfile;
    FILE *tmp_passwdfile;
    htpasswd_auth_state *state;
    char line[MAX_LINE_LEN];
    char *sep;
    char *tmpfile = NULL;
    int tmpfile_len = 0;
    struct stat file_info;

    state = auth->state;
    passwdfile = fopen(state->filename, "rb");

    if(passwdfile == NULL) {
        WARN2("Failed to open authentication database \"%s\": %s", 
                state->filename, strerror(errno));
        return AUTH_FAILED;
    }
    tmpfile_len = strlen(state->filename) + 6;
    tmpfile = calloc(1, tmpfile_len);
    snprintf (tmpfile, tmpfile_len, "%s.tmp", state->filename);
    if (stat (tmpfile, &file_info) == 0)
    {
        WARN1 ("temp file \"%s\" exists, rejecting operation", tmpfile);
        free (tmpfile);
        fclose (passwdfile);
        return AUTH_FAILED;
    }

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
    /* Windows won't let us rename a file if the destination file
       exists...so, lets remove the original first */
    if (remove(state->filename) != 0) {
        ERROR3("Problem moving temp authentication file to original \"%s\" - \"%s\": %s", 
                tmpfile, state->filename, strerror(errno));
    }
    else {
        if (rename(tmpfile, state->filename) != 0) {
            ERROR3("Problem moving temp authentication file to original \"%s\" - \"%s\": %s", 
                    tmpfile, state->filename, strerror(errno));
        }
    }
    free(tmpfile);

    return AUTH_USERDELETED;
}


static auth_result htpasswd_userlist(auth_t *auth, xmlNodePtr srcnode)
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

