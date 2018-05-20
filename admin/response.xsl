<xsl:stylesheet xmlns:xsl = "http://www.w3.org/1999/XSL/Transform" version = "1.0">
	<xsl:output method="html" doctype-system="about:legacy-compat" encoding="UTF-8" />
	<!-- Import include files -->
	<xsl:include href="includes/page.xsl"/>

	<xsl:variable name="title">Server Response</xsl:variable>

	<xsl:template name="content">
				<div class="section">
					<h2><xsl:value-of select="$title" /></h2>
					<xsl:for-each select="/iceresponse">
						<div class="article">
							<h3>Response</h3>
							<h4>Message</h4>
							<p><xsl:value-of select="message" /></p>
							<p>(Return Code: <xsl:value-of select="return" />)</p>
						</div>
					</xsl:for-each>
				</div>
	</xsl:template>
</xsl:stylesheet>
