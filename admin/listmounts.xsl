<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" version="1.0">
    <!-- Import include files -->
    <xsl:include href="includes/page.xsl"/>
    <xsl:include href="includes/mountnav.xsl"/>
    <xsl:include href="includes/player.xsl"/>
    <xsl:include href="includes/authlist.xsl"/>

    <xsl:variable name="title">Active Mountpoints</xsl:variable>

    <xsl:template name="content">
        <div class="section">
            <h2><xsl:value-of select="$title" /></h2>
            <xsl:choose>
                <xsl:when test="source">
                    <xsl:for-each select="source">
                        <section class="box">
                            <h3 class="box_title">Mountpoint <code><xsl:value-of select="@mount" /></code></h3>
                            <!-- Mount nav -->
                            <div class="side-by-side">
                                <div class="trafficlight colour-{health/text()}">&#160;</div>
                                <xsl:call-template name="mountnav" />
                            </div>
                            <xsl:call-template name="player" />
                            <p><xsl:value-of select="listeners" /> Listener(s)</p>

                            <xsl:if test="maintenance/*">
                                <h4>Maintenance</h4>
                                <ul class="maintenance-container">
                                    <xsl:for-each select="maintenance/*">
                                        <li class="maintenance-level-{@maintenance-level}">
                                            <p><xsl:value-of select="text()" /></p>
                                        </li>
                                    </xsl:for-each>
                                </ul>
                            </xsl:if>

                            <!-- Mount Authentication -->
                            <xsl:if test="authentication">
                                <h4>Mount Authentication</h4>
                                <xsl:call-template name="authlist" />
                            </xsl:if>
                        </section>
                    </xsl:for-each>
                </xsl:when>
                <xsl:otherwise>
                    <aside class="info">
                        <strong>No mounts!</strong> There are no active mountpoints.
                    </aside>
                </xsl:otherwise>
            </xsl:choose>
        </div>
    </xsl:template>
</xsl:stylesheet>
