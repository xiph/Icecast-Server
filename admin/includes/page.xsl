<xsl:stylesheet xmlns:xsl = "http://www.w3.org/1999/XSL/Transform" version = "1.0">
	<xsl:output method="html" doctype-system="about:legacy-compat" encoding="UTF-8" />
	<!-- Import include files -->
	<xsl:include href="includes/head.xsl"/>
	<xsl:include href="includes/header.xsl"/>
	<xsl:include href="includes/footer.xsl"/>

	<xsl:template match="/node()">
		<html>

			<xsl:call-template name="head">
				<xsl:with-param name="title" select="$title" />
			</xsl:call-template>

			<body>
				<!-- Header/Menu -->
				<xsl:call-template name="header" />

				<xsl:call-template name="content" />

				<!-- Footer -->
				<xsl:call-template name="footer" />

			</body>
		</html>
	</xsl:template>
</xsl:stylesheet>
