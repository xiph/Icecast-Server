<xsl:stylesheet xmlns:xsl = "http://www.w3.org/1999/XSL/Transform" version = "1.0" >
<xsl:output omit-xml-declaration="no" method="xml" doctype-public="-//W3C//DTD XHTML 1.0 Strict//EN" doctype-system="http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd" indent="yes" encoding="UTF-8" />
<xsl:template match = "/icestats" >
<html>
<head>
<link rel="stylesheet" type="text/css" href="/style.css" />
</head>
<div class="logs">
<body>
<table>
<tr><td><pre>
<xsl:for-each select="/icestats"> <xsl:for-each select="log"> <xsl:value-of select="." /> </xsl:for-each></xsl:for-each>
</pre></td></tr>
</table>
</body>
</div>
</html>

</xsl:template>
</xsl:stylesheet>
