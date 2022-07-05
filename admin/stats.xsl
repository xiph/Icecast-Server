<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" version="1.0">
    <!-- Import include files -->
    <xsl:include href="includes/page.xsl"/>
    <xsl:include href="includes/mountnav.xsl"/>
    <xsl:include href="includes/player.xsl"/>
    <xsl:include href="includes/playlist.xsl"/>
    <xsl:include href="includes/authlist.xsl"/>

    <xsl:param name="param-showall" />
    <xsl:param name="param-has-mount" />
    <xsl:variable name="title">Server status</xsl:variable>

    <xsl:template name="content">
        <h2>Server status</h2>

        <!-- Global stats table -->
        <section class="box">
            <h3 class="box_title">Global server stats</h3>
            <!-- Global subnav -->
            <div class="stats">
                <ul class="boxnav">
                    <li><a href="/admin/reloadconfig.xsl?omode=normal">Reload Configuration</a></li>
                    <li><a href="/admin/stats.xsl?showall=true">Show all mounts</a></li>
                </ul>
            </div>

            <h4>Statistics</h4>

            <table class="table-block">
                <thead>
                    <tr>
                        <th>Key</th>
                        <th>Value</th>
                    </tr>
                </thead>
                <tbody>
                    <xsl:for-each select="/icestats/*[not(self::source) and not(self::authentication) and not(self::modules)]">
                        <tr>
                            <td><xsl:value-of select="name()" /></td>
                            <td><xsl:value-of select="text()" /></td>
                        </tr>
                    </xsl:for-each>
                </tbody>
            </table>

            <!-- Global Auth -->
            <xsl:if test="authentication">
                <h4>Authentication</h4>
                <xsl:call-template name="authlist" />
            </xsl:if>
        </section>

        <!-- Mount stats -->
        <xsl:if test="$param-showall or $param-has-mount">
            <xsl:for-each select="source">
                <section class="box" id="mount-{position()}">
                    <h3 class="box_title">Mountpoint <code><xsl:value-of select="@mount" /></code></h3>
                    <!-- Mount nav -->
                    <xsl:call-template name="mountnav" />
                    <xsl:call-template name="player" />
                    <h4>Further information</h4>
                    <table class="table-block">
                        <thead>
                            <tr>
                                <th>Key</th>
                                <th>Value</th>
                            </tr>
                        </thead>
                        <tbody>
                            <xsl:for-each select="*[not(self::metadata) and not(self::authentication) and not(self::authenticator) and not(self::listener)]">
                                <tr>
                                    <td><xsl:value-of select="name()" /></td>
                                    <td><xsl:value-of select="text()" /></td>
                                </tr>
                            </xsl:for-each>
                        </tbody>
                    </table>

                    <!-- Extra metadata -->
                    <xsl:if test="metadata/*">
                        <h4>Extra Metadata</h4>
                        <table class="table-block">
                            <tbody>
                                <xsl:for-each select="metadata/*">
                                    <tr>
                                        <td><xsl:value-of select="name()" /></td>
                                        <td><xsl:value-of select="text()" /></td>
                                    </tr>
                                </xsl:for-each>
                            </tbody>
                        </table>
                    </xsl:if>

                    <!-- Extra playlist -->
                    <xsl:call-template name="playlist" />

                    <!-- Mount Authentication -->
                    <xsl:if test="authentication/*">
                        <h4>Mount Authentication</h4>
                        <xsl:call-template name="authlist" />
                    </xsl:if>

                </section>
            </xsl:for-each>
        </xsl:if>
    </xsl:template>
</xsl:stylesheet>
