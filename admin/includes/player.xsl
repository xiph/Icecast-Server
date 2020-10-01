<xsl:stylesheet xmlns:xsl = "http://www.w3.org/1999/XSL/Transform" version = "1.0">
	<xsl:output method="html" doctype-system="about:legacy-compat" encoding="UTF-8" indent="yes" />
	<xsl:template name="player">
		<div>
			<ul class="playlists">
				<li><a href="{@mount}">Direct</a></li>
				<li><a href="{@mount}.m3u">M3U</a></li>
				<li><a href="{@mount}.xspf">XSPF</a></li>
			</ul>

			<!-- Playlists section -->
			<h4>Play stream</h4>

			<!-- Player -->
			<xsl:if test="server_type and ((server_type = 'application/ogg') or (server_type = 'audio/ogg') or (server_type = 'audio/webm'))">
				<div class="audioplayer">
					<audio controls="controls" preload="none">
						<source src="{@mount}" type="{server_type}" />
					</audio>
				</div>
			</xsl:if>
		</div>
	</xsl:template>
</xsl:stylesheet>
