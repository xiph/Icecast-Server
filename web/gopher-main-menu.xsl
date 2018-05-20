<xsl:stylesheet xmlns:xsl = "http://www.w3.org/1999/XSL/Transform" version = "1.0">
<xsl:output omit-xml-declaration="yes" media-type="application/x-gopher-menu"
        method="text" indent="no" encoding="UTF-8" />
<xsl:template match = "/icestats">
<xsl:text>iIcecast Server	*	*	*
iVersion: </xsl:text><xsl:value-of select="server_id" /><xsl:text>	*	*	*
</xsl:text>
	<xsl:for-each select="source">
		<xsl:choose>
			<xsl:when test="listeners">
				<xsl:text>s</xsl:text>
				<xsl:value-of select="server_name" />
				<xsl:text>	</xsl:text>
				<xsl:value-of select="@mount" />
				<xsl:text>	</xsl:text>
				<xsl:value-of select="../host" />
				<xsl:text>	8000</xsl:text>
			</xsl:when>
			<xsl:otherwise>
				<xsl:text>i</xsl:text>
				<xsl:value-of select="@mount" />
				<xsl:text> is offline	*	*	*</xsl:text>
			</xsl:otherwise>
		</xsl:choose>
	</xsl:for-each>
</xsl:template>
</xsl:stylesheet>
