/* httpp.h
**
** http parsing library
*/

#ifndef __HTTPP_H
#define __HTTPP_H

#include "avl.h"

#define HTTPP_VAR_PROTOCOL "__protocol"
#define HTTPP_VAR_VERSION "__version"
#define HTTPP_VAR_URI "__uri"
#define HTTPP_VAR_REQ_TYPE "__req_type"

typedef enum httpp_request_type_tag {
	httpp_req_none, httpp_req_get, httpp_req_post, httpp_req_head, httpp_req_source, httpp_req_play, httpp_req_stats, httpp_req_unknown
} httpp_request_type_e;

typedef struct http_var_tag {
	char *name;
	char *value;
} http_var_t;

typedef struct http_varlist_tag {
	http_var_t var;
	struct http_varlist_tag *next;
} http_varlist_t;

typedef struct http_parser_tag {
	httpp_request_type_e req_type;
	char *uri;

	avl_tree *vars;
} http_parser_t;

http_parser_t *httpp_create_parser(void);
void httpp_initialize(http_parser_t *parser, http_varlist_t *defaults);
int httpp_parse(http_parser_t *parser, char *http_data, unsigned long len);
void httpp_setvar(http_parser_t *parser, char *name, char *value);
char *httpp_getvar(http_parser_t *parser, char *name);
void httpp_destroy(http_parser_t *parser);
 
#endif



