#ifndef __YP_H__
#define __YP_H__

#include <stdio.h>
#define YP_ADD_ALL -1
typedef struct ypdata_tag
{
	char	*sid;
	char	*server_name;
	char	*server_desc;
	char	*server_genre;
	char	*cluster_password;
	char	*server_url;
	char	*listen_url;
	char	*bitrate;
	char	*server_type;
	char	*current_song;
	char	*yp_url;
	int	yp_url_timeout;
	long	yp_last_touch;
	int	yp_touch_freq;
} ypdata_t;

void *yp_touch_thread(void *arg);
int yp_add(void *psource, int which);
int yp_touch(void *psource);
int yp_remove(void *psource);
ypdata_t *yp_create_ypdata();
void yp_destroy_ypdata(ypdata_t *ypdata);

#endif


