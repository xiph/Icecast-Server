#ifndef __GLOBAL_H__
#define __GLOBAL_H__

#define ICE_LISTEN_QUEUE 5

#define ICE_RUNNING 1
#define ICE_HALTING 2

typedef struct ice_global_tag
{
	int serversock;

	int running;

	int sources;
	int clients;

	avl_tree *source_tree;
} ice_global_t;

extern ice_global_t global;

void global_initialize(void);
void global_shutdown(void);
void global_lock(void);
void global_unlock(void);

#endif  /* __GLOBAL_H__ */
