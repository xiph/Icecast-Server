<xsl:stylesheet xmlns:xsl = "http://www.w3.org/1999/XSL/Transform" version = "1.0" >
<xsl:output omit-xml-declaration="no" method="xml" doctype-public="-//W3C//DTD XHTML 1.0 Strict//EN" doctype-system="http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd" indent="yes" encoding="UTF-8" />
<xsl:template match = "/icestats" >
<xsl:for-each select="/icestats">
<xsl:for-each select="server_start">
<xsl:if test = "name()!='source'"> 
<UPTIME><xsl:value-of select="." /></UPTIME>
</xsl:if>
</xsl:for-each>
</xsl:for-each>
</xsl:template>
</xsl:stylesheet>
