#ifndef __GETURL_H__
#define __GETURL_H__

#include <stdio.h>

#include <curl/curl.h>
#include <curl/types.h>
#include <curl/easy.h>

#define NUM_CONNECTIONS 10
#define NAK 0
#define ACK 1
#define YP_RESPONSE_SIZE 2046
#define YP_SID_SIZE 255

struct MemoryStruct {
	char memory[YP_RESPONSE_SIZE];
	size_t size;
};
struct MemoryStruct2 {
	char 	sid[YP_SID_SIZE];
	char 	message[YP_RESPONSE_SIZE];
	int	touchFreq;
	int	response;
	size_t size;
};

typedef struct tag_CurlConnection {
	struct MemoryStruct	result;
	struct MemoryStruct2	headerresult;
	CURL		*curl_handle;
	int		inUse;
} CurlConnection;


int curl_initialize();
void curl_shutdown();
CURL *getCurlHandle(int which);
struct MemoryStruct *getCurlResult(int which);
struct MemoryStruct2 *getCurlHeaderResult(int which);
void printHeaderResult(struct MemoryStruct2 *mem);
int getCurlConnection();
int releaseCurlConnection(int which);
#endif

