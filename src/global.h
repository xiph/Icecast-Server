#ifndef __GLOBAL_H__
#define __GLOBAL_H__

#define ICE_LISTEN_QUEUE 5

#define ICE_RUNNING 1
#define ICE_HALTING 2

#define ICECAST_VERSION_STRING "Icecast 2.0-alpha2/cvs"

#define MAX_LISTEN_SOCKETS 10

#include "thread/thread.h"

typedef struct ice_global_tag
{
    int serversock[MAX_LISTEN_SOCKETS];
    int server_sockets;

    int running;

    int sources;
    int clients;
    int schedule_config_reread;

    avl_tree *source_tree;

    cond_t shutdown_cond;
} ice_global_t;

extern ice_global_t global;

void global_initialize(void);
void global_shutdown(void);
void global_lock(void);
void global_unlock(void);

#endif  /* __GLOBAL_H__ */
