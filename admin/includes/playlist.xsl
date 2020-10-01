<xsl:stylesheet xmlns:xsl = "http://www.w3.org/1999/XSL/Transform" version = "1.0">
	<xsl:output method="html" doctype-system="about:legacy-compat" encoding="UTF-8" indent="yes" />
	<xsl:template name="playlist">
		<xsl:if test="playlist/*">
			<h4>Playlist</h4>
			<div class="playlist-container">
				<table class="table-block">
					<thead>
						<tr>
							<th>Album</th>
							<th width="10%">Track</th>
							<th>Creator</th>
							<th width="33%">Title</th>
						</tr>
					</thead>
					<tbody>
						<xsl:for-each select="playlist/trackList/track">
							<tr>
								<td><xsl:value-of select="album" /></td>
								<td><xsl:value-of select="trackNum" /></td>
								<td><xsl:value-of select="creator" /></td>
								<td><xsl:value-of select="title" /></td>
							</tr>
						</xsl:for-each>
					</tbody>
				</table>
			</div>
		</xsl:if>
	</xsl:template>
</xsl:stylesheet>
