<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" version="1.0">
    <xsl:template name="mountnav">
        <xsl:param name="mount" select="@mount"/>
        <div class="mountnav">
            <ul class="boxnav">
                <li><a href="/admin/stats.xsl?mount={$mount}#mount-1">Details</a></li>
                <li><a href="/admin/listclients.xsl?mount={$mount}">Clients</a></li>
                <li><a href="/admin/moveclients.xsl?mount={$mount}">Move listeners</a></li>
                <li><a href="/admin/updatemetadata.xsl?mount={$mount}">Metadata</a></li>
                <li><a href="/admin/fallbacks.xsl?mount={$mount}&amp;omode=strict">Set fallback</a></li>
                <li class="critical"><a href="/admin/ui/confirmkillsource.xsl?mount={$mount}">Kill source</a></li>
            </ul>
        </div>
    </xsl:template>
</xsl:stylesheet>
