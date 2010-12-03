<xsl:stylesheet version="1.0" xmlns:xsl="http://www.w3.org/1999/XSL/Transform">
<xsl:output method="html" version="1.0" encoding="iso-8859-1" indent="yes"/>
<xsl:template match = "/icestats" >

	<xsl:for-each select="source">
	   <xsl:if test="position()=1">
		<xsl:value-of select="listeners" />,1,<xsl:value-of select="listener_peak" />,<xsl:value-of select="max_listeners" />,<xsl:value-of select="listeners" />,<xsl:value-of select="bitrate" />,<xsl:if test="artist"><xsl:value-of select="artist" /> - </xsl:if><xsl:value-of select="title" />
	   </xsl:if>
	</xsl:for-each>

</xsl:template>
</xsl:stylesheet>