<xsl:stylesheet xmlns:xsl = "http://www.w3.org/1999/XSL/Transform" version = "1.0">
	<xsl:output method="html" doctype-system="about:legacy-compat" encoding="UTF-8" indent="yes" />
	<xsl:template name="mountnav">
		<xsl:param name="mount" select="@mount"/>
		<div class="mountnav">
			<ul class="boxnav">
				<li><a href="stats.xsl?mount={$mount}#mount-1">Details</a></li>
				<li><a href="listclients.xsl?mount={$mount}">Clients</a></li>
				<li><a href="moveclients.xsl?mount={$mount}">Move listeners</a></li>
				<li><a href="updatemetadata.xsl?mount={$mount}">Metadata</a></li>
				<li class="critical"><a href="killsource.xsl?mount={$mount}">Kill source</a></li>
			</ul>
		</div>
	</xsl:template>
</xsl:stylesheet>
