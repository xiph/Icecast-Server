<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" version="1.0">
    <xsl:output method="html" doctype-system="about:legacy-compat" encoding="utf-8" />
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

                <main role="main">
                    <xsl:call-template name="content" />
                </main>

                <!-- Footer -->
                <footer>
                    <xsl:call-template name="footer" />
                </footer>

            </body>
        </html>
    </xsl:template>
</xsl:stylesheet>
