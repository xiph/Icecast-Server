<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" version="1.0">
    <xsl:include href="includes/page.xsl"/>
    <xsl:variable name="title">Dashboard</xsl:variable>

    <xsl:template name="content">
        <h2><xsl:value-of select="$title" /></h2>
        <xsl:for-each select="/report/incident">
            <xsl:for-each select="resource[@name='overall-status']">
                <section class="box">
                    <h3 class="box_title">Overview for <code><xsl:value-of select="value[@member='global-config']/value[@member='hostname']/@value" /></code></h3>
                    <ul class="boxnav">
                        <li><a href="/admin/reloadconfig.xsl?omode=normal">Reload Configuration</a></li>
                    </ul>
                    <div class="side-by-side">
                        <div>
                            <h4>Health</h4>
                            <div class="trafficlight colour-{value[@member='status']/@value}">&#160;</div>
                        </div>
                        <div>
                            <h4>Current load</h4>
                            <table class="table-block bartable">
                                <tbody>
                                    <xsl:for-each select="value[@member='global-current']/value">
                                        <tr>
                                            <xsl:variable name="member" select="@member" />
                                            <xsl:variable name="of" select="../../value[@member='global-config']/value[@member=$member]/@value" />
                                            <td><xsl:value-of select="concat(translate(substring(@member, 1, 1), 'abcdefghijklmnopqrstuvwxyz', 'ABCDEFGHIJKLMNOPQRSTUVWXYZ'), substring(@member, 2))" /></td>
                                            <td><meter min="0" max="{$of}" value="{@value}"></meter></td>
                                            <td><xsl:value-of select="@value" /> of <xsl:value-of select="$of" /></td>
                                        </tr>
                                    </xsl:for-each>
                                </tbody>
                            </table>
                        </div>
                    </div>
                </section>
            </xsl:for-each>
        </xsl:for-each>
        <section class="box">
            <h3 class="box_title">Maintenance</h3>
            <xsl:choose>
                <xsl:when test="/report/incident/state/text">
                    <ul class="maintenance-container">
                        <xsl:for-each select="/report/incident">
                            <li class="maintenance-level-{resource[@name='maintenance']/value[@member='type']/@value}">
                                <p><xsl:value-of select="state/text/text()" /></p>
                                <ul class="references">
                                    <xsl:for-each select="reference">
                                        <li><a href="{@href}"><xsl:value-of select="concat(translate(substring(@type, 1, 1), 'abcdefghijklmnopqrstuvwxyz', 'ABCDEFGHIJKLMNOPQRSTUVWXYZ'), substring(@type, 2))" /></a></li>
                                    </xsl:for-each>
                                </ul>
                            </li>
                        </xsl:for-each>
                    </ul>
                </xsl:when>
                <xsl:otherwise>
                    <p>Nothing to do.</p>
                </xsl:otherwise>
            </xsl:choose>
        </section>
    </xsl:template>
</xsl:stylesheet>
