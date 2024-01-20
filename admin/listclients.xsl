<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" version="1.0">
    <!-- Import include files -->
    <xsl:include href="includes/page.xsl"/>
    <xsl:include href="includes/mountnav.xsl"/>

    <xsl:variable name="title">Listener Stats</xsl:variable>

    <xsl:template name="content">
        <div class="section">
            <h2><xsl:value-of select="$title" /></h2>

            <xsl:for-each select="source">
                <section class="box">
                    <h3 class="box_title">Mountpoint <code><xsl:value-of select="@mount" /></code></h3>
                    <!-- Mount nav -->
                    <xsl:call-template name="mountnav" />
                    <h4>Listeners</h4>
                    <xsl:choose>
                        <xsl:when test="listener">
                            <table class="table-flipscroll">
                                <thead>
                                    <tr>
                                        <th>IP</th>
                                        <th>Username</th>
                                        <th>Role</th>
                                        <th>Sec. connected</th>
                                        <th>User Agent</th>
                                        <th>Location</th>
                                        <th class="actions">Action</th>
                                    </tr>
                                </thead>
                                <tbody>
                                    <xsl:for-each select="listener">
                                        <tr>
                                            <td><xsl:value-of select="ip" /></td>
                                            <td><xsl:value-of select="username" /></td>
                                            <td><xsl:value-of select="role" /></td>
                                            <td><xsl:value-of select="connected" /></td>
                                            <td><xsl:value-of select="useragent" /></td>
                                            <td>
                                                <xsl:value-of select="geoip/country/@iso-alpha-2" />
                                                <xsl:if test="geoip/location/@latitude and geoip/location/@longitude">&#160;<a href="https://www.openstreetmap.org/?mlat={geoip/location/@latitude}&amp;mlon={geoip/location/@longitude}&amp;zoom=7">On OSM</a></xsl:if>
                                            </td>
                                            <td class="actions">
                                                <a class="critical" href="/admin/ui/confirmkillclient.xsl?mount={../@mount}&amp;id={id}">Kick</a>
                                                <a href="/admin/moveclients.xsl?mount={../@mount}&amp;id={id}">Move</a>
                                            </td>
                                        </tr>
                                    </xsl:for-each>
                                </tbody>
                            </table>
                        </xsl:when>
                        <xsl:otherwise>
                            <p>No listeners connected</p>
                        </xsl:otherwise>
                    </xsl:choose>
                </section>
            </xsl:for-each>

        </div>
    </xsl:template>
</xsl:stylesheet>
