#ifndef __ADMIN_H__
#define __ADMIN_H__

#include "refbuf.h"
#include "client.h"

void admin_handle_request(client_t *client, char *uri);

#endif  /* __ADMIN_H__ */
