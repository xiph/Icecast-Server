<xsl:stylesheet xmlns:xsl = "http://www.w3.org/1999/XSL/Transform" version = "1.0">
	<xsl:output method="html" doctype-system="about:legacy-compat" encoding="UTF-8" />
	<!-- Import include files -->
	<xsl:include href="includes/page.xsl"/>
	<xsl:include href="includes/mountnav.xsl"/>
	<xsl:include href="includes/player.xsl"/>

	<xsl:variable name="title">Server status</xsl:variable>

	<!-- Auth template -->
	<xsl:template name="authlist">
		<ul>
			<xsl:for-each select="authentication/role">
				<li>Role
					<xsl:if test="@name">
						<xsl:value-of select="@name" />
					</xsl:if>
					of type <xsl:value-of select="@type" />
					<xsl:if test="@management-url">
						<xsl:choose>
							<xsl:when test="@can-adduser='true' or @can-deleteuser='true'">
								(<a href="{@management-url}">Manage</a>)
							</xsl:when>
							<xsl:when test="@can-listuser='true'">
								(<a href="{@management-url}">List</a>)
							</xsl:when>
						</xsl:choose>
					</xsl:if>
				</li>
			</xsl:for-each>
		</ul>
	</xsl:template>


	<xsl:template name="content">
					<h2>Server status</h2>

					<!-- Global stats table -->
					<section class="box">
						<h3 class="box_title">Global server stats</h3>
						<!-- Global subnav -->
						<div class="stats">
							<ul class="boxnav">
								<li><a href="reloadconfig.xsl">Reload Configuration</a></li>
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
					<xsl:for-each select="source">
						<section class="box">
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
							<xsl:if test="playlist/*">
								<h4>Playlist</h4>
								<table class="table-block">
									<tbody>
										<tr>
											<th>Album</th>
											<th>Track</th>
											<th>Creator</th>
											<th>Title</th>
										</tr>
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
							</xsl:if>

							<!-- Mount Authentication -->
							<xsl:if test="authentication">
								<h4>Mount Authentication</h4>
								<xsl:call-template name="authlist" />
							</xsl:if>

						</section>
					</xsl:for-each>
	</xsl:template>
</xsl:stylesheet>
