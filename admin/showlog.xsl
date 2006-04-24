<xsl:stylesheet xmlns:xsl = "http://www.w3.org/1999/XSL/Transform" version = "1.0" >
<xsl:output method="xml" media-type="text/html" indent="yes" encoding="UTF-8"
    doctype-system="http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd"
    doctype-public="-//W3C//DTD XHTML 1.0 Transitional//EN" />
<xsl:template match = "/icestats" >
<html>
<head>
<title>Icecast log files</title>
</head>
<body style="color: white; font-size: 90%">
<table>
<tr><td><pre> 
<xsl:for-each select="/icestats"> <xsl:for-each select="log"> <xsl:value-of select="." /> </xsl:for-each></xsl:for-each>
</pre></td></tr>
</table>
</body>
</html>

</xsl:template>
</xsl:stylesheet>
