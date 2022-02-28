<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" version="1.0">
    <xsl:template name="player">
        <xsl:if test="not(allow-direct-access) or allow-direct-access = 'true'">
            <div>
                <ul class="playlists">
                    <li><a href="{@mount}">Direct</a></li>
                    <li><a href="{@mount}.m3u">M3U</a></li>
                    <li><a href="{@mount}.xspf">XSPF</a></li>
                </ul>

                <!-- Playlists section -->
                <h4>Play stream</h4>

                    <!-- Player -->
                <xsl:if test="content-type and ((content-type = 'application/ogg') or (content-type = 'audio/ogg') or (content-type = 'audio/webm'))">
                    <div class="audioplayer">
                        <audio controls="controls" preload="none">
                            <source src="{@mount}" type="{content-type}" />
                        </audio>
                    </div>
                </xsl:if>
            </div>
        </xsl:if>
    </xsl:template>
</xsl:stylesheet>
