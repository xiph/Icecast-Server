/* httpp.h
**
** http parsing library
** 
** This program is distributed under the GNU General Public License, version 2.
** A copy of this license is included with this source.
*/

#ifndef __HTTPP_H
#define __HTTPP_H

#include <avl/avl.h>

#define HTTPP_VAR_PROTOCOL "__protocol"
#define HTTPP_VAR_VERSION "__version"
#define HTTPP_VAR_URI "__uri"
#define HTTPP_VAR_RAWURI "__rawuri"
#define HTTPP_VAR_REQ_TYPE "__req_type"
#define HTTPP_VAR_ERROR_MESSAGE "__errormessage"
#define HTTPP_VAR_ERROR_CODE "__errorcode"
#define HTTPP_VAR_ICYPASSWORD "__icy_password"

typedef enum httpp_request_type_tag {
    httpp_req_none, httpp_req_get, httpp_req_post, httpp_req_head,
    httpp_req_source, httpp_req_play, httpp_req_stats, httpp_req_unknown
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
    avl_tree *queryvars;
} http_parser_t;

#ifdef _mangle
# define httpp_create_parser _mangle(httpp_create_parser)
# define httpp_initialize _mangle(httpp_initialize)
# define httpp_parse _mangle(httpp_parse)
# define httpp_parse_icy _mangle(httpp_parse_icy)
# define httpp_parse_response _mangle(httpp_parse_response)
# define httpp_setvar _mangle(httpp_setvar)
# define httpp_getvar _mangle(httpp_getvar)
# define httpp_set_query_param _mangle(httpp_set_query_param)
# define httpp_get_query_param _mangle(httpp_get_query_param)
# define httpp_destroy _mangle(httpp_destroy)
# define httpp_clear _mangle(httpp_clear)
#endif

http_parser_t *httpp_create_parser(void);
void httpp_initialize(http_parser_t *parser, http_varlist_t *defaults);
int httpp_parse(http_parser_t *parser, char *http_data, unsigned long len);
int httpp_parse_icy(http_parser_t *parser, char *http_data, unsigned long len);
int httpp_parse_response(http_parser_t *parser, char *http_data, unsigned long len, char *uri);
void httpp_setvar(http_parser_t *parser, const char *name, const char *value);
char *httpp_getvar(http_parser_t *parser, const char *name);
void httpp_set_query_param(http_parser_t *parser, char *name, char *value);
char *httpp_get_query_param(http_parser_t *parser, char *name);
void httpp_destroy(http_parser_t *parser);
void httpp_clear(http_parser_t *parser);
 
#endif
