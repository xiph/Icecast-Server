#ifndef __AUTH_H__
#define __AUTH_H__

#include "source.h"
#include "client.h"
#include "config.h"

typedef enum
{
    AUTH_OK,
    AUTH_FAILED,
} auth_result;

typedef struct auth_tag
{
    /* Authenticate using the given username and password */
    auth_result (*authenticate)(struct auth_tag *self, 
            char *username, char *password);
    void (*free)(struct auth_tag *self);
    void *state;
} auth_t;

auth_result auth_check_client(source_t *source, client_t *client);

auth_t *auth_get_authenticator(char *type, config_options_t *options);
void *auth_clear(auth_t *authenticator);

#endif


