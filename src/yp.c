#include <stdio.h>
#include <string.h>

#include <thread/thread.h>

#include "connection.h"
#include "refbuf.h"
#include "client.h"
#include "logging.h"
#include "format.h"
#include "geturl.h"
#include "source.h"
#include "config.h"

#define CATMODULE "YP" 

int yp_submit_url(int curlCon, char *yp_url, char *pURL, char *type)
{
	int ret = 0;

	curl_easy_setopt(getCurlHandle(curlCon), CURLOPT_URL, yp_url);
	curl_easy_setopt(getCurlHandle(curlCon), CURLOPT_POSTFIELDS, pURL);
	curl_easy_setopt(getCurlHandle(curlCon), CURLOPT_TIMEOUT, config_get_config()->yp_url_timeout);

	/* get it! */
	memset(getCurlResult(curlCon), '\000', sizeof(struct MemoryStruct));
	memset(getCurlHeaderResult(curlCon), '\000', sizeof(struct MemoryStruct2));

	curl_easy_perform(getCurlHandle(curlCon));

	printHeaderResult(getCurlHeaderResult(curlCon));

	if (getCurlHeaderResult(curlCon)->response == ACK) {
		INFO2("Successfull ACK from %s (%s)", type, yp_url);
		ret = 1;
	}
	else {
		if (strlen(getCurlHeaderResult(curlCon)->message) > 0) {
			ERROR3("Got a NAK from %s(%s) (%s)", type,getCurlHeaderResult(curlCon)->message, yp_url);
		}
		else {
			ERROR2("Got a NAK from %s(Unknown) (%s)", type, yp_url);
		}
		ret = 0;
	}
	return ret;
}

void *yp_touch_thread(void *arg)
{
	yp_touch((source_t *)arg);
	thread_exit(0);
	return NULL;
}
int yp_remove(void *psource)
{
	char	*pURL = NULL;
	int	pURLsize = 0;
	int	ret = 0;
	int	curlCon = 0;
	char	*p1 = NULL;
	int	i = 0;

	int	regenSID = 0;
	long	currentTime = 0;
	source_t	*source = (source_t *)psource;

	currentTime = time(&currentTime);
	
	for (i=0;i<source->num_yp_directories;i++) {
		source->ypdata[i]->yp_last_touch = currentTime;
		
		if (source->ypdata[i]->sid == 0) {
			return 0;
		}
		else {
			if (strlen(source->ypdata[i]->sid) == 0) {
				return 0;
			}
		}

		if (source->ypdata) {
			pURLsize = strlen("action=remove&sid=") + 1;
			pURLsize += strlen(source->ypdata[i]->sid);
			pURLsize += 1024;

			pURL = (char *)malloc(pURLsize);
			memset(pURL, '\000', pURLsize);
			sprintf(pURL, "action=remove&sid=%s", 
					source->ypdata[i]->sid);

			curlCon = getCurlConnection();
			if (curlCon < 0) {
				ERROR0("Unable to get auth connection");
			}
			else {

				/* specify URL to get */
				ret = yp_submit_url(curlCon, source->ypdata[i]->yp_url, pURL, "yp_remove");
			}

			if (pURL) {
				free(pURL);
			}

			releaseCurlConnection(curlCon);
		}
	}

	return 1;
}
int yp_touch(void *psource)
{
	char	*pURL = NULL;
	int	pURLsize = 0;
	int	ret = 0;
	int	curlCon = 0;
	char	*p1 = NULL;
	int	i = 0;

	int	regenSID = 0;
	long	currentTime = 0;
	source_t	*source = (source_t *)psource;

	currentTime = time(&currentTime);
	
	for (i=0;i<source->num_yp_directories;i++) {

		source->ypdata[i]->yp_last_touch = currentTime;
	
		if (source->ypdata[i]->sid == 0) {
			regenSID = 1;
		}
		else {
			if (strlen(source->ypdata[i]->sid) == 0) {
				regenSID = 1;
			}
		}

		if (regenSID) {
			if (!yp_add(source, i)) {
				return 0;
			}
		}

		if (source->ypdata) {
			pURLsize = strlen("action=touch&sid=&st=&listeners=") + 1;
			if (source->ypdata[i]->current_song) {
				pURLsize += strlen(source->ypdata[i]->current_song);
			}
			else {
				source->ypdata[i]->current_song = (char *)malloc(1);
				memset(source->ypdata[i]->current_song, '\000', 1);
			}
			if (source->ypdata[i]->sid) {
				pURLsize += strlen(source->ypdata[i]->sid);
			}
			else {
				source->ypdata[i]->sid = (char *)malloc(1);
				memset(source->ypdata[i]->sid, '\000', 1);
			}
			pURLsize += 1024;
			pURL = (char *)malloc(pURLsize);
			memset(pURL, '\000', pURLsize);
			sprintf(pURL, "action=touch&sid=%s&st=%s&listeners=%d", 
					source->ypdata[i]->sid,
					source->ypdata[i]->current_song,
					source->listeners);

			curlCon = getCurlConnection();
			if (curlCon < 0) {
				ERROR0("Unable to get auth connection");
			}
			else {

				/* specify URL to get */
				ret = yp_submit_url(curlCon, source->ypdata[i]->yp_url, pURL, "yp_touch");
			}

			if (pURL) {
				free(pURL);
			}

			releaseCurlConnection(curlCon);
		}
	}

	return 1;
}
int yp_add(void *psource, int which)
{
	char	*pURL = NULL;
	int	pURLsize = 0;
	int	ret = 0;
	int	curlCon = 0;
	char	*p1 = NULL;
	int	i = 0;

	int	ok = 0;
	source_t	*source = (source_t *)psource;
	
	for (i=0;i<source->num_yp_directories;i++) {
		if (which != -1) {
			if (i == which) {
				ok = 1;
			}
			else {
				ok = 0;
			}
		}
		else {
			ok = 1;
		}

		if (ok) {
			if (source->ypdata[i]) {
				pURLsize = strlen("action=add&sn=&genre=&cpswd=&desc=&url=&listenurl=&type=&b=") + 1;
				if (source->ypdata[i]->server_name) {
					pURLsize += strlen(source->ypdata[i]->server_name);
				}
				else {
					source->ypdata[i]->server_name = (char *)malloc(1);
					memset(source->ypdata[i]->server_name, '\000', 1);
				}
				if (source->ypdata[i]->server_desc) {
					pURLsize += strlen(source->ypdata[i]->server_desc);
				}
				else {
					source->ypdata[i]->server_desc = (char *)malloc(1);
					memset(source->ypdata[i]->server_desc, '\000', 1);
				}
				if (source->ypdata[i]->server_genre) {
					pURLsize += strlen(source->ypdata[i]->server_genre);
				}
				else {
					source->ypdata[i]->server_genre = (char *)malloc(1);
					memset(source->ypdata[i]->server_genre, '\000', 1);
				}
				if (source->ypdata[i]->cluster_password) {
					pURLsize += strlen(source->ypdata[i]->cluster_password);
				}
				else {
					source->ypdata[i]->cluster_password = (char *)malloc(1);
					memset(source->ypdata[i]->cluster_password, '\000', 1);
				}
				if (source->ypdata[i]->server_url) {
					pURLsize += strlen(source->ypdata[i]->server_url);
				}
				else {
					source->ypdata[i]->server_url = (char *)malloc(1);
					memset(source->ypdata[i]->server_url, '\000', 1);
				}
				if (source->ypdata[i]->listen_url) {
					pURLsize += strlen(source->ypdata[i]->listen_url);
				}
				else {
					source->ypdata[i]->listen_url = (char *)malloc(1);
					memset(source->ypdata[i]->listen_url, '\000', 1);
				}
				if (source->ypdata[i]->server_type) {
					pURLsize += strlen(source->ypdata[i]->server_type);
				}
				else {
					source->ypdata[i]->server_type = (char *)malloc(1);
					memset(source->ypdata[i]->server_type, '\000', 1);
				}
				if (source->ypdata[i]->bitrate) {
					pURLsize += strlen(source->ypdata[i]->bitrate);
				}
				else {
					source->ypdata[i]->bitrate = (char *)malloc(1);
					memset(source->ypdata[i]->bitrate, '\000', 1);
				}
				if (source->ypdata[i]->current_song) {
					pURLsize += strlen(source->ypdata[i]->current_song);
				}
				else {
					source->ypdata[i]->current_song = (char *)malloc(1);
					memset(source->ypdata[i]->current_song, '\000', 1);
				}
				pURLsize += 1024;
				pURL = (char *)malloc(pURLsize);
				memset(pURL, '\000', pURLsize);
				sprintf(pURL, "action=add&sn=%s&genre=%s&cpswd=%s&desc=%s&url=%s&listenurl=%s&type=%s&b=%s", 
						source->ypdata[i]->server_name,
						source->ypdata[i]->server_genre,
						source->ypdata[i]->cluster_password,
						source->ypdata[i]->server_desc,
						source->ypdata[i]->server_url,
						source->ypdata[i]->listen_url,
						source->ypdata[i]->server_type,
						source->ypdata[i]->bitrate);

				curlCon = getCurlConnection();
				if (curlCon < 0) {
					ERROR0("Unable to get auth connection");
				}
				else {

					/* specify URL to get */
					ret = yp_submit_url(curlCon, source->ypdata[i]->yp_url, pURL, "yp_add");

					if (ret) {
						if (strlen(getCurlHeaderResult(curlCon)->sid) > 0) {
							if (source->ypdata) {
								if (source->ypdata[i]->sid) {
									free(source->ypdata[i]->sid);
									source->ypdata[i]->sid = NULL;
								}
								source->ypdata[i]->sid = (char *)malloc(strlen(getCurlHeaderResult(curlCon)->sid) +1);
								memset(source->ypdata[i]->sid, '\000', strlen(getCurlHeaderResult(curlCon)->sid) +1);
								strcpy(source->ypdata[i]->sid, getCurlHeaderResult(curlCon)->sid);
								source->ypdata[i]->yp_touch_freq = getCurlHeaderResult(curlCon)->touchFreq;
							}
						}
					}
				}

				if (pURL) {
					free(pURL);
				}

				releaseCurlConnection(curlCon);
			}
		}
	}

	return 1;
}

ypdata_t *create_ypdata()
{
	ypdata_t *tmp;

	tmp = (ypdata_t *)malloc(sizeof(ypdata_t));
	memset(tmp, '\000', sizeof(ypdata_t));
	return(tmp);

}
void destroy_ypdata(ypdata_t *ypdata)
{
	if (ypdata) {
		if (ypdata->sid) {
			free(ypdata->sid);
		}
		if (ypdata->server_name) {
			free(ypdata->server_name);
		}
		if (ypdata->server_desc) {
			free(ypdata->server_desc);
		}
		if (ypdata->server_genre) {
			free(ypdata->server_genre);
		}
		if (ypdata->cluster_password) {
			free(ypdata->cluster_password);
		}
		if (ypdata->server_url) {
			free(ypdata->server_url);
		}
		if (ypdata->listen_url) {
			free(ypdata->listen_url);
		}
		if (ypdata->current_song) {
			free(ypdata->current_song);
		}
		if (ypdata->bitrate) {
			free(ypdata->bitrate);
		}
		if (ypdata->server_type) {
			free(ypdata->server_type);
		}
		free(ypdata);
	}
}
