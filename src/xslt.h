#include <libxml/xmlmemory.h>
#include <libxml/debugXML.h>
#include <libxml/HTMLtree.h>
#include <libxml/xmlIO.h>
#include <libxslt/xslt.h>
#include <libxslt/xsltInternals.h>
#include <libxslt/transform.h>
#include <libxslt/xsltutils.h>


#include <thread/thread.h>
#include <avl/avl.h>
#include <httpp/httpp.h>
#include <net/sock.h>


#include "connection.h"

#include "global.h"
#include "refbuf.h"
#include "client.h"
#include "stats.h"


void transformXSLT(xmlDocPtr doc, char *xslfilename, client_t *client);

