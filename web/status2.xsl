<xsl:stylesheet xmlns:xsl = "http://www.w3.org/1999/XSL/Transform" version = "1.0" >
<xsl:output omit-xml-declaration="no" method="xml" doctype-public="-//W3C//DTD XHTML 1.0 Strict//EN" doctype-system="http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd" indent="yes" encoding="UTF-8" />
<xsl:template match = "/icestats" >
<pre>
MountPoint,Connections,Stream Name,Current Listeners,Description,Currently Playing,Stream URL 
Global,Client:<xsl:value-of select="connections" /> Source: <xsl:value-of select="source_connections" />,,<xsl:value-of select="listeners" />,,
<xsl:for-each select="source">
<xsl:value-of select="@mount" />,,<xsl:value-of select="name" />,<xsl:value-of select="listeners" />,<xsl:value-of select="description" />,<xsl:value-of select="artist" /> - <xsl:value-of select="title" />,<xsl:value-of select="url" />
</xsl:for-each>
</pre>
</xsl:template>
</xsl:stylesheet>
