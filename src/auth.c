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
                authenticator, username, password);

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
} htpasswd_auth_state;

static void htpasswd_clear(auth_t *self) {
    htpasswd_auth_state *state = self->state;
    free(state->filename);
    free(state);
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
static auth_result htpasswd_auth(auth_t *auth, char *username, char *password)
{
    htpasswd_auth_state *state = auth->state;
    FILE *passwdfile = fopen(state->filename, "rb");
    char line[MAX_LINE_LEN];
    char *sep;

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
            DEBUG0("No seperator in line");
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
                return AUTH_OK;
            }
            free(hashed_password);
            /* We don't keep searching through the file */
            break; 
        }
    }

    fclose(passwdfile);

    return AUTH_FAILED;
}

static auth_t *auth_get_htpasswd_auth(config_options_t *options)
{
    auth_t *authenticator = calloc(1, sizeof(auth_t));
    htpasswd_auth_state *state;

    authenticator->authenticate = htpasswd_auth;
    authenticator->free = htpasswd_clear;

    state = calloc(1, sizeof(htpasswd_auth_state));

    while(options) {
        if(!strcmp(options->name, "filename"))
            state->filename = strdup(options->value);
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

    return authenticator;
}

