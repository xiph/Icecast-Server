#ifndef __FSERVE_H__
#define __FSERVE_H__

#include <stdio.h>

typedef struct
{
    client_t *client;

    FILE *file;
    int offset;
    int datasize;
    unsigned char *buf;
} fserve_t;

void fserve_initialize(void);
void fserve_shutdown(void);
int fserve_client_create(client_t *httpclient, char *path);


#endif


