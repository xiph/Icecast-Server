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

#include <curl/curl.h>
#include <curl/types.h>
#include <curl/easy.h>


#define CATMODULE "CURL" 

static CurlConnection	curlConnections[NUM_CONNECTIONS];
static int		nunConnections = NUM_CONNECTIONS;
mutex_t _curl_mutex;

size_t
WriteMemoryCallback(void *ptr, size_t size, size_t nmemb, void *data)
{
	register int realsize = size * nmemb;

	struct MemoryStruct *mem = (struct MemoryStruct *)data;

	if ((realsize + mem->size) < YP_RESPONSE_SIZE-1) {
		strncat(mem->memory, ptr, realsize);
	}

	return realsize;
}
size_t
HeaderMemoryCallback(void *ptr, size_t size, size_t nmemb, void *data)
{
	char	*p1 = 0;
	char	*p2 = 0;
	int	copylen = 0;
	register int realsize = size * nmemb;
	struct MemoryStruct2 *mem = (struct MemoryStruct2 *)data;

	if (!strncmp(ptr, "SID: ", strlen("SID: "))) {
		p1 = (char *)ptr + strlen("SID: ");
		p2 = strchr((const char *)p1, '\r');
		memset(mem->sid, '\000', sizeof(mem->sid));
		if (p2) {
			if (p2-p1 > sizeof(mem->sid)-1) {
				copylen = sizeof(mem->sid)-1;
			}
			else {
				copylen = p2-p1;
			}
			strncpy(mem->sid, p1, copylen);
		}
		else {
			strncpy(mem->sid, p1, sizeof(mem->sid)-1);
			strcpy(mem->sid, p1);
		}
	}
	if (!strncmp(ptr, "YPMessage: ", strlen("YPMessage: "))) {
		p1 = (char *)ptr + strlen("YPMessage: ");
		p2 = strchr((const char *)p1, '\r');
		memset(mem->message, '\000', sizeof(mem->message));
		if (p2) {
			if (p2-p1 > sizeof(mem->message)-1) {
				copylen = sizeof(mem->message)-1;
			}
			else {
				copylen = p2-p1;
			}
			strncpy(mem->message, p1, copylen);
		}
		else {
			strncpy(mem->message, p1, sizeof(mem->message)-1);
			strcpy(mem->message, p1);
		}
	}
	if (!strncmp(ptr, "TouchFreq: ", strlen("TouchFreq: "))) {
		p1 = (char *)ptr + strlen("TouchFreq: ");
		mem->touchFreq = atoi(p1);
	}
	if (!strncmp(ptr, "YPResponse: ", strlen("YPResponse: "))) {
		p1 = (char *)ptr + strlen("YPResponse: ");
		mem->response = atoi(p1);
	}
	return realsize;
}

int curl_initialize()
{
	int i = 0;
	thread_mutex_create(&_curl_mutex);

	memset(&curlConnections, '\000', sizeof(curlConnections));
	for (i=0;i<NUM_CONNECTIONS;i++) {
		curlConnections[i].curl_handle = curl_easy_init();
		curl_easy_setopt(curlConnections[i].curl_handle, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
		curl_easy_setopt(curlConnections[i].curl_handle, CURLOPT_WRITEHEADER, (void *)&(curlConnections[i].headerresult));
		curl_easy_setopt(curlConnections[i].curl_handle, CURLOPT_HEADERFUNCTION, HeaderMemoryCallback);
		curl_easy_setopt(curlConnections[i].curl_handle, CURLOPT_FILE, (void *)&(curlConnections[i].result));
	}
	return(1);

}

void curl_shutdown()
{
	int i = 0;
	for (i=0;i<NUM_CONNECTIONS;i++) {
		curl_easy_cleanup(curlConnections[i].curl_handle);
		memset(&(curlConnections[i]), '\000', sizeof(curlConnections[i]));
	}

}

int getCurlConnection()
{
	int found = 0;
	int	curlConnection = -1;
	int i = 0;
	while (!found) {
		thread_mutex_lock(&_curl_mutex);
		for (i=0;i<NUM_CONNECTIONS;i++) {
			if (!curlConnections[i].inUse) {
				found = 1;
				curlConnections[i].inUse = 1;
				curlConnection = i;
				break;
			}
		}
		thread_mutex_unlock(&_curl_mutex);
#ifdef WIN32
		Sleep(200);
#else
		usleep(200);
#endif
	}
	return(curlConnection);
}
int releaseCurlConnection(int which)
{
	thread_mutex_lock(&_curl_mutex);
	curlConnections[which].inUse = 0;
	memset(&(curlConnections[which].result), '\000', sizeof(curlConnections[which].result));
	memset(&(curlConnections[which].headerresult), '\000', sizeof(curlConnections[which].headerresult));
	thread_mutex_unlock(&_curl_mutex);

	return 1;
}


void printHeaderResult(struct MemoryStruct2 *mem) {
	DEBUG1("SID -> (%s)", mem->sid);
	DEBUG1("Message -> (%s)", mem->message);
	DEBUG1("Touch Freq -> (%d)", mem->touchFreq);
	DEBUG1("Response -> (%d)", mem->response);
}


CURL *getCurlHandle(int which)
{
	return curlConnections[which].curl_handle;
}
struct MemoryStruct *getCurlResult(int which)
{
	return &(curlConnections[which].result);
}

struct MemoryStruct2 *getCurlHeaderResult(int which)
{
	return &(curlConnections[which].headerresult);
}
