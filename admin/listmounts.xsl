<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" version="1.0">
	<!-- Import include files -->
	<xsl:include href="includes/page.xsl"/>
	<xsl:include href="includes/mountnav.xsl"/>
	<xsl:include href="includes/player.xsl"/>

	<xsl:variable name="title">Active Mountpoints</xsl:variable>

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
				<div class="section">
					<h2><xsl:value-of select="$title" /></h2>
					<xsl:choose>
						<xsl:when test="source">
							<xsl:for-each select="source">
								<section class="box">
									<h3 class="box_title">Mountpoint <code><xsl:value-of select="@mount" /></code></h3>
									<!-- Mount nav -->
									<xsl:call-template name="mountnav" />
									<xsl:call-template name="player" />
									<p><xsl:value-of select="listeners" /> Listener(s)</p>

									<!-- Mount Authentication -->
									<xsl:if test="authentication">
										<h4>Mount Authentication</h4>
										<xsl:call-template name="authlist" />
									</xsl:if>
								</section>
							</xsl:for-each>
						</xsl:when>
						<xsl:otherwise>
							<aside class="info">
								<strong>No mounts!</strong> There are no active mountpoints.
							</aside>
						</xsl:otherwise>
					</xsl:choose>
				</div>
	</xsl:template>
</xsl:stylesheet>
