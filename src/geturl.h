/* -*- c-basic-offset: 4; indent-tabs-mode: nil; -*- */
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

struct curl_memory_struct {
    char memory[YP_RESPONSE_SIZE];
    size_t size;
};
struct curl_memory_struct2 {
    char sid[YP_SID_SIZE];
    char message[YP_RESPONSE_SIZE];
    int touch_interval;
    int response;
    size_t size;
};

typedef struct tag_curl_connection {
    struct curl_memory_struct result;
    struct curl_memory_struct2 header_result;
    CURL *curl_handle;
    int in_use;
} curl_connection;


int curl_initialize();
void curl_shutdown();
CURL *curl_get_handle(int which);
struct curl_memory_struct *curl_get_result(int which);
struct curl_memory_struct2 *curl_get_header_result(int which);
void curl_print_header_result(struct curl_memory_struct2 *mem);
int curl_get_connection();
int curl_release_connection(int which);

#endif
