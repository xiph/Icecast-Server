<xsl:stylesheet xmlns:xsl = "http://www.w3.org/1999/XSL/Transform" version = "1.0" xmlns="http://www.w3.org/1999/xhtml">
    <xsl:output omit-xml-declaration="no" method="xml" doctype-public="-//W3C//DTD XHTML 1.0 Strict//EN" doctype-system="http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd" indent="yes" encoding="UTF-8" />
	<!-- Import include files -->

	<xsl:template match="/node()">
		<html>
		    <head>
		        <title><xsl:value-of select="$title"/> — Icecast Streaming Media Server</title>
		    	<link rel="stylesheet" type="text/css" href="style.css" />
			    <meta name="viewport" content="width=device-width, initial-scale=1.0, user-scalable=yes" />
		    </head>
		    <body>
			    <h1 id="header">Icecast <xsl:value-of select="$title"/></h1>
		    	<!--index header menu -->
			    <div id="menu">
				    <ul>
		    			<li><a href="admin/">Administration</a></li>
			    		<li><a href="status.xsl">Server Status</a></li>
				    	<li><a href="server_version.xsl">Version</a></li>
		    		</ul>
			    </div>
		    	<!--end index header menu -->
		        <xsl:call-template name="content" namespace="http://www.w3.org/1999/xhtml" />
		    	<div id="footer">
			    	Support icecast development at <a href="http://www.icecast.org">www.icecast.org</a>
		    	</div>
		    </body>
		</html>
    </xsl:template>
</xsl:stylesheet>
