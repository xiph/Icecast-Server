<xsl:stylesheet xmlns:xsl = "http://www.w3.org/1999/XSL/Transform" version = "1.0" xmlns="http://www.w3.org/1999/xhtml">
<xsl:output omit-xml-declaration="no" method="xml" doctype-public="-//W3C//DTD XHTML 1.0 Strict//EN" doctype-system="http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd" indent="yes" encoding="UTF-8" />
<xsl:include href="includes/web-page.xsl"/>
<xsl:include href="includes/player.xsl"/>
<xsl:include href="includes/playlist.xsl"/>
<xsl:variable name="title">Status</xsl:variable>
<xsl:template name="content">
	<xsl:text disable-output-escaping="yes">
	&lt;!-- WARNING:
	 DO NOT ATTEMPT TO PARSE ICECAST HTML OUTPUT!
	 The web interface may change completely between releases.
	 If you have a need for automatic processing of server data,
	 please read the appropriate documentation. Latest docs:
	 http://icecast.org/docs/icecast-latest/icecast2_stats.html
	-->
	</xsl:text>
	<!--mount point stats-->
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
										<td class="streamstats">
										<xsl:if test="artist">
											<xsl:value-of select="artist" /> - 
										</xsl:if>
											<xsl:value-of select="title" />
										</td>
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
