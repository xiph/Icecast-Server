<xsl:stylesheet xmlns:xsl = "http://www.w3.org/1999/XSL/Transform" version = "1.0">
	<xsl:output method="html" doctype-system="about:legacy-compat" encoding="UTF-8" />
	<!-- Import include files -->
	<xsl:include href="includes/head.xsl"/>
	<xsl:include href="includes/header.xsl"/>
	<xsl:include href="includes/footer.xsl"/>

	<xsl:template match="/iceresponse">
		<html>

			<xsl:call-template name="head">
				<xsl:with-param name="title">Stats</xsl:with-param>
			</xsl:call-template>

			<body>
				<!-- Header/Menu -->
				<xsl:call-template name="header" />

				<div class="section">
					<h2>Server Response</h2>
					<xsl:for-each select="/iceresponse">
						<div class="article">
							<h3>Response</h3>
							<h4>Message</h4>
							<p><xsl:value-of select="message" /></p>
							<p>(Return Code: <xsl:value-of select="return" />)</p>
						</div>
					</xsl:for-each>
				</div>

				<!-- Footer -->
				<xsl:call-template name="footer" />

			</body>
		</html>
	</xsl:template>
</xsl:stylesheet>
