<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" version="1.0">
    <xsl:include href="includes/page.xsl"/>
    <xsl:variable name="title">Logfiles</xsl:variable>

    <xsl:template name="content">
        <h2><xsl:value-of select="$title" /></h2>
        <xsl:for-each select="/report/incident">
            <section class="box">
                <h3 class="box_title">Logfile <code><xsl:value-of select="resource[@name='logcontent']/value/value[@member='logfile']/@value" /></code></h3>
                <ul class="boxnav">
                    <xsl:for-each select="resource[@name='logfiles']/value/value">
                        <li><a href="?logfile={@value}"><xsl:value-of select="@value" /></a></li>
                    </xsl:for-each>
                    <li class="critical"><a href="/admin/marklog.xsl?omode=normal">Mark logfiles</a></li>
                </ul>
                <ul class="codeblock">
                    <xsl:for-each select="resource[@name='logcontent']/value/value[@member='lines']/value">
                        <li><pre><xsl:value-of select="@value" /></pre></li>
                    </xsl:for-each>
                </ul>
            </section>
        </xsl:for-each>
    </xsl:template>
</xsl:stylesheet>
