/* refbuf.c
**
** reference counting buffer implementation
**
*/

#include <stdlib.h>
#include <string.h>

#include "thread.h"

#include "refbuf.h"

mutex_t _refbuf_mutex;

void refbuf_initialize(void)
{
	thread_mutex_create(&_refbuf_mutex);
}

void refbuf_shutdown(void)
{
	thread_mutex_destroy(&_refbuf_mutex);
}

refbuf_t *refbuf_new(unsigned long size)
{
	refbuf_t *refbuf;

	refbuf = (refbuf_t *)malloc(sizeof(refbuf_t));
	refbuf->data = (void *)malloc(size);
	refbuf->len = size;
	refbuf->_count = 1;

	return refbuf;
}

void refbuf_addref(refbuf_t *self)
{
	thread_mutex_lock(&_refbuf_mutex);
	self->_count++;
	thread_mutex_unlock(&_refbuf_mutex);
}

void refbuf_release(refbuf_t *self)
{
	thread_mutex_lock(&_refbuf_mutex);
	self->_count--;
	if (self->_count == 0) {
		free(self->data);
		free(self);
	}
	thread_mutex_unlock(&_refbuf_mutex);
}

void refbuf_queue_add(refbuf_queue_t **queue, refbuf_t *refbuf)
{
	refbuf_queue_t *node;
	refbuf_queue_t *item = (refbuf_queue_t *)malloc(sizeof(refbuf_queue_t));

	item->refbuf = refbuf;
	item->next = NULL;

	if (*queue == NULL) {
		*queue = item;
        (*queue)->total_length = item->refbuf->len;
	} else {
		node = *queue;
		while (node->next) node = node->next;
		node->next = item;
        (*queue)->total_length += item->refbuf->len;
	}
}

refbuf_t *refbuf_queue_remove(refbuf_queue_t **queue)
{
	refbuf_queue_t *item;
	refbuf_t *refbuf;

	if (*queue == NULL) return NULL;

	item = *queue;
	*queue = item->next;
	item->next = NULL;

	refbuf = item->refbuf;
	item->refbuf = NULL;

    if(*queue)
        (*queue)->total_length = item->total_length - refbuf->len;
	
	free(item);

       
	return refbuf;
}

void refbuf_queue_insert(refbuf_queue_t **queue, refbuf_t *refbuf)
{
	refbuf_queue_t *item = (refbuf_queue_t *)malloc(sizeof(refbuf_queue_t));

	item->refbuf = refbuf;
	item->next = *queue;
    if(item->next)
        item->total_length = item->next->total_length + item->refbuf->len;
    else
        item->total_length = item->refbuf->len;
	*queue = item;
}

int refbuf_queue_size(refbuf_queue_t **queue)
{
	refbuf_queue_t *node = *queue;
	int size = 0;

	while (node) {
		node = node->next;
		size++;
	}
	
	return size;
}

int refbuf_queue_length(refbuf_queue_t **queue)
{
    if(*queue)
        return (*queue)->total_length;
    else
        return 0;
}


