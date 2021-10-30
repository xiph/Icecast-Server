<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" version="1.0">
    <xsl:include href="includes/web-page.xsl"/>
    <xsl:include href="includes/player.xsl"/>
    <xsl:include href="includes/playlist.xsl"/>
    <xsl:variable name="title">Status</xsl:variable>
    <xsl:template name="content">
        <!--mount point stats-->
        <script>
            <![CDATA[
            function reload() {
                var xhr = new XMLHttpRequest;
                xhr.open('GET', '/admin/publicstats');
                xhr.setRequestHeader('Accept', 'text/xml, */*; q=0');

                xhr.onload = function () {
                    if (xhr.readyState === xhr.DONE && xhr.status === 200) {
                        var doc = xhr.responseXML;
                        var iterator = doc.evaluate('/icestats/source', doc, null, XPathResult.UNORDERED_NODE_ITERATOR_TYPE, null);

                        var thisNode = iterator.iterateNext();

                        while (thisNode) {
                            var us = document.querySelector('[data-mount="' + thisNode.attributes.mount.nodeValue + '"]');
                            if (us) {
                                us.innerText = doc.evaluate('display-title', thisNode, null, XPathResult.STRING_TYPE, null).stringValue;
                            }
                            thisNode = iterator.iterateNext();
                        }
                    }
                };

                xhr.send();
            }

            setInterval(reload, 5000);
            ]]>
        </script>
        <h2>Status</h2>
        <xsl:choose>
            <xsl:when test="source">
                <xsl:for-each select="source">
                    <xsl:choose>
                        <xsl:when test="listeners">
                            <section class="box">
                                <h3 class="box_title">Mountpoint <code><xsl:value-of select="@mount" /></code></h3>
                                <xsl:call-template name="player" />

                                <!-- Stream info and stats -->
                                <h4>Further information</h4>
                                <div class="mountcont">
                                    <table class="table-keys">
                                        <tbody>
                                            <xsl:if test="server_name">
                                                <tr>
                                                    <td>Stream Name:</td>
                                                    <td><xsl:value-of select="server_name" /></td>
                                                </tr>
                                            </xsl:if>
                                            <xsl:if test="server_description">
                                                <tr>
                                                    <td>Stream Description:</td>
                                                    <td><xsl:value-of select="server_description" /></td>
                                                </tr>
                                            </xsl:if>
                                            <xsl:if test="server_type">
                                                <tr>
                                                    <td>Content Type:</td>
                                                    <td><xsl:value-of select="server_type" /></td>
                                                </tr>
                                            </xsl:if>
                                            <xsl:if test="stream_start">
                                                <tr>
                                                    <td>Stream started:</td>
                                                    <td class="streamstats"><xsl:value-of select="stream_start" /></td>
                                                </tr>
                                            </xsl:if>
                                            <xsl:if test="bitrate">
                                                <tr>
                                                    <td>Bitrate:</td>
                                                    <td class="streamstats"><xsl:value-of select="bitrate" /></td>
                                                </tr>
                                            </xsl:if>
                                            <xsl:if test="quality">
                                                <tr>
                                                    <td>Quality:</td>
                                                    <td class="streamstats"><xsl:value-of select="quality" /></td>
                                                </tr>
                                            </xsl:if>
                                            <xsl:if test="video_quality">
                                                <tr>
                                                    <td>Video Quality:</td>
                                                    <td class="streamstats"><xsl:value-of select="video_quality" /></td>
                                                </tr>
                                            </xsl:if>
                                            <xsl:if test="frame_size">
                                                <tr>
                                                    <td>Framesize:</td>
                                                    <td class="streamstats"><xsl:value-of select="frame_size" /></td>
                                                </tr>
                                            </xsl:if>
                                            <xsl:if test="frame_rate">
                                                <tr>
                                                    <td>Framerate:</td>
                                                    <td class="streamstats"><xsl:value-of select="frame_rate" /></td>
                                                </tr>
                                            </xsl:if>
                                            <xsl:if test="listeners">
                                                <tr>
                                                    <td>Listeners (current):</td>
                                                    <td class="streamstats"><xsl:value-of select="listeners" /></td>
                                                </tr>
                                            </xsl:if>
                                            <xsl:if test="listener_peak">
                                                <tr>
                                                    <td>Listeners (peak):</td>
                                                    <td class="streamstats"><xsl:value-of select="listener_peak" /></td>
                                                </tr>
                                            </xsl:if>
                                            <xsl:if test="genre">
                                                <tr>
                                                    <td>Genre:</td>
                                                    <td class="streamstats"><xsl:value-of select="genre" /></td>
                                                </tr>
                                            </xsl:if>
                                            <xsl:if test="server_url">
                                                <tr>
                                                    <td>Stream URL:</td>
                                                    <td class="streamstats">
                                                        <a href="{server_url}"><xsl:value-of select="server_url" /></a>
                                                    </td>
                                                </tr>
                                            </xsl:if>
                                            <tr>
                                                <td>Currently playing:</td>
                                                <td class="streamstats" data-mount="{@mount}"><xsl:value-of select="display-title" /></td>
                                            </tr>
                                        </tbody>
                                    </table>
                                    <!-- Extra playlist -->
                                    <xsl:call-template name="playlist" />
                                </div>
                            </section>
                        </xsl:when>
                        <xsl:otherwise>
                            <h3><xsl:value-of select="@mount" /> - Not Connected</h3>
                        </xsl:otherwise>
                    </xsl:choose>
                </xsl:for-each>
            </xsl:when>
            <xsl:otherwise>
                <aside class="info">
                    <strong>No mounts!</strong> There are no active mountpoints.
                </aside>
            </xsl:otherwise>
        </xsl:choose>
    </xsl:template>
</xsl:stylesheet>
