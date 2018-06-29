<xsl:stylesheet xmlns:xsl = "http://www.w3.org/1999/XSL/Transform" version = "1.0">
    <xsl:output omit-xml-declaration="yes" media-type="text/plain" method="text" indent="no" encoding="UTF-8" />
	<xsl:template name="content" match="/report">
        	<xsl:for-each select="/report/incident">
            		<xsl:text>Report:&#xa;</xsl:text>
            		<xsl:value-of select="state/text" />
			<xsl:text>&#xa;</xsl:text>
			<xsl:if test="state/@definition">
				<xsl:text>Error code: </xsl:text>
				<xsl:value-of select="state/@definition" />
				<xsl:text>&#xa;</xsl:text>
			</xsl:if>
        	</xsl:for-each>
	</xsl:template>
</xsl:stylesheet>
