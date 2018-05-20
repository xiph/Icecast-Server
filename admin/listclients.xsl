<xsl:stylesheet xmlns:xsl = "http://www.w3.org/1999/XSL/Transform" version = "1.0">
	<xsl:output method="html" doctype-system="about:legacy-compat" encoding="UTF-8" indent="yes" />
	<!-- Import include files -->
	<xsl:include href="includes/page.xsl"/>
	<xsl:include href="includes/mountnav.xsl"/>

	<xsl:variable name="title">Listener Stats</xsl:variable>

	<xsl:template name="content">
				<div class="section">
					<h2><xsl:value-of select="$title" /></h2>

					<xsl:for-each select="source">
						<div class="article">
							<h3>Mountpoint <xsl:value-of select="@mount" /></h3>
							<!-- Mount nav -->
							<xsl:call-template name="mountnav" />
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
												<th>Action</th>
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
													<td><a href="killclient.xsl?mount={../@mount}&amp;id={id}">Kick</a></td>
												</tr>
											</xsl:for-each>
										</tbody>
									</table>
								</xsl:when>
								<xsl:otherwise>
									<p>No listeners connected</p>
								</xsl:otherwise>
							</xsl:choose>
						</div>
					</xsl:for-each>

				</div>
	</xsl:template>
</xsl:stylesheet>
