#ifndef __MD5_H__
#define __MD5_H__

#include "config.h"
#include "compat.h"

#define HASH_LEN     16

struct MD5Context
{       
    uint32_t     buf[4];
    uint32_t     bits[2];
    unsigned char in[64];
};

void MD5Init(struct MD5Context *context);
void MD5Update(struct MD5Context *context, unsigned char const *buf, 
        unsigned len);
void MD5Final(unsigned char digest[HASH_LEN], struct MD5Context *context);


#endif


