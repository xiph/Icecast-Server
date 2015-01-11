<xsl:stylesheet xmlns:xsl = "http://www.w3.org/1999/XSL/Transform" version = "1.0">
	<xsl:output method="html" doctype-system="about:legacy-compat" encoding="UTF-8" />
	<!-- Import include files -->
	<xsl:include href="includes/head.xsl"/>
	<xsl:include href="includes/header.xsl"/>
	<xsl:include href="includes/footer.xsl"/>

	<xsl:include href="includes/mountnav.xsl"/>

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


	<xsl:template match="/icestats">
		<html>

			<xsl:call-template name="head">
				<xsl:with-param name="title">Stats</xsl:with-param>
			</xsl:call-template>

			<body>
				<!-- Header/Menu -->
				<xsl:call-template name="header" />

				<div class="section">
					<h2>Administration</h2>

					<!-- Global stats table -->
					<div class="article">
						<h3>Global server stats</h3>
						<!-- Global subnav -->
						<div class="nav">
							<ul>
								<li><a href="reloadconfig.xsl">Reload Configuration</a></li>
							</ul>
						</div>
						<table class="table-block">
							<thead>
								<tr>
									<th>Key</th>
									<th>Value</th>
								</tr>
							</thead>
							<tbody>
								<xsl:for-each select="/icestats/*[not(self::source) and not(self::authentication)]">
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
					</div>

					<!-- Mount stats -->
					<xsl:for-each select="source">
						<div class="article">
							<h3>Mountpoint <xsl:value-of select="@mount" /></h3>
							<!-- Mount nav -->
							<xsl:call-template name="mountnav" />
							<h4>Play stream</h4>
							<xsl:choose>
								<xsl:when test="authenticator">
									<a class="play" href="/auth.xsl">Auth</a>
								</xsl:when>
								<xsl:otherwise>
									<a class="play" href="{@mount}.m3u">&#9658; <span>M3U</span></a>
									<xsl:text> </xsl:text>
									<a class="play" href="{@mount}.xspf">&#9658; <span>XSPF</span></a>
									<xsl:text> </xsl:text>
									<a class="play" href="{@mount}.vclt">&#9658; <span>VCLT</span></a>
								</xsl:otherwise>
							</xsl:choose>
							<h4>Further information</h4>
							<table class="table-block">
								<thead>
									<tr>
										<th>Key</th>
										<th>Value</th>
									</tr>
								</thead>
								<tbody>
									<xsl:for-each select="*[not(self::metadata) and not(self::authentication) and not(self::authenticator)]">
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

							<!-- Mount Authentication -->
							<xsl:if test="authentication">
								<h4>Mount Authentication</h4>
								<xsl:call-template name="authlist" />
							</xsl:if>

						</div>
					</xsl:for-each>
				</div>

				<!-- Footer -->
				<xsl:call-template name="footer" />

			</body>
		</html>
	</xsl:template>
</xsl:stylesheet>
