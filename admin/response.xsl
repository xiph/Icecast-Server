<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" version="1.0">
	<!-- Import include files -->
	<xsl:include href="includes/page.xsl"/>

	<xsl:variable name="title">Server Response</xsl:variable>

	<xsl:template name="content">
		<h2><xsl:value-of select="$title" /></h2>
		<xsl:for-each select="/iceresponse">
			<section class="box">
				<h3 class="box_title">Response</h3>
				<h4>Message</h4>
				<p><xsl:value-of select="message" /></p>
				<p>(Return Code: <xsl:value-of select="return" />)</p>
			</section>
		</xsl:for-each>
	</xsl:template>
</xsl:stylesheet>
