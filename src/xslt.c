#include <string.h>
#include <libxml/xmlmemory.h>
#include <libxml/debugXML.h>
#include <libxml/HTMLtree.h>
#include <libxml/xmlIO.h>
#include <libxml/DOCBparser.h>
#include <libxml/xinclude.h>
#include <libxml/catalog.h>
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


void transformXSLT(xmlDocPtr doc, char *xslfilename, client_t *client)
{
    xmlOutputBufferPtr outputBuffer;
	xmlDocPtr	res;
	xsltStylesheetPtr cur;
	const char *params[16 + 1];
    size_t count,nBytes;

	params[0] = NULL;

	xmlSubstituteEntitiesDefault(1);
	xmlLoadExtDtdDefaultValue = 1;

	cur = xsltParseStylesheetFile(xslfilename);
	if (cur == NULL) {
		sock_write_string(client->con->sock, (char *)"Could not parse XSLT file");
        return;
	}

    res = xsltApplyStylesheet(cur, doc, params);

    outputBuffer = xmlAllocOutputBuffer(NULL);

    count = xsltSaveResultTo(outputBuffer, res, cur);

    /*  Add null byte to end. */
    nBytes = xmlOutputBufferWrite(outputBuffer, 1, "");

	sock_write_string(client->con->sock, (char *)outputBuffer->buffer->content);

    xmlFree(outputBuffer);
    xsltFreeStylesheet(cur);
    xmlFreeDoc(res);

    xsltCleanupGlobals(); /* Neccesary? */
}

