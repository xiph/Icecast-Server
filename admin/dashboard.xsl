<xsl:stylesheet xmlns:xsl = "http://www.w3.org/1999/XSL/Transform" version = "1.0" xmlns="http://www.w3.org/1999/xhtml">
	<xsl:output omit-xml-declaration="no" method="xml" doctype-public="-//W3C//DTD XHTML 1.0 Strict//EN" doctype-system="http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd" indent="yes" encoding="UTF-8" />
	<xsl:include href="includes/page.xsl"/>
	<xsl:variable name="title">Dashboard</xsl:variable>

	<xsl:template name="content">
		<h2><xsl:value-of select="$title" /></h2>
		<xsl:for-each select="/report/incident">
			<xsl:for-each select="resource[@name='overall-status']">
				<section class="box">
					<h3 class="box_title">Overview for <code><xsl:value-of select="value[@member='global-config']/value[@member='hostname']/@value" /></code></h3>
					<ul class="boxnav">
						<li><a href="reloadconfig.xsl">Reload Configuration</a></li>
					</ul>
					<div class="side-by-side">
						<div>
							<h4>Health</h4>
							<div class="trafficlight colour-{value[@member='status']/@value}">&#160;</div>
						</div>
						<div>
							<h4>Current load</h4>
							<table class="table-block">
								<tbody>
									<xsl:for-each select="value[@member='global-current']/value">
										<tr>
											<xsl:variable name="member" select="@member" />
											<xsl:variable name="of" select="../../value[@member='global-config']/value[@member=$member]/@value" />
											<td><xsl:value-of select="$member" /></td>
											<td class="barmeter">
												<span><xsl:value-of select="@value" /> of <xsl:value-of select="$of" /></span>
												<div style="width: calc(100% * {@value} / {$of});">&#160;</div>
											</td>
										</tr>
									</xsl:for-each>
								</tbody>
							</table>
						</div>
					</div>
				</section>
			</xsl:for-each>
			<xsl:for-each select="resource[@name='maintenance']">
				<section class="box">
					<h3 class="box_title">Maintenance</h3>
					<xsl:choose>
						<xsl:when test="value">
							<ul class="maintenance-container">
								<xsl:for-each select="value">
									<li class="maintenance-level-{value[@member='type']/@value}">
										<p><xsl:value-of select="text/text()" /></p>
										<ul class="references">
											<xsl:for-each select="reference">
												<li><a href="{@href}"><xsl:value-of select="concat(translate(substring(@type, 1, 1), 'abcdefghijklmnopqrstuvwxyz', 'ABCDEFGHIJKLMNOPQRSTUVWXYZ'), substring(@type, 2))" /></a></li>
											</xsl:for-each>
										</ul>
									</li>
								</xsl:for-each>
							</ul>
						</xsl:when>
						<xsl:otherwise>
							<p>Nothing to do.</p>
						</xsl:otherwise>
					</xsl:choose>
				</section>
			</xsl:for-each>
		</xsl:for-each>
	</xsl:template>
</xsl:stylesheet>
