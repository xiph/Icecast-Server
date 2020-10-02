<xsl:stylesheet xmlns:xsl = "http://www.w3.org/1999/XSL/Transform" version = "1.0">
	<xsl:output method="html" doctype-system="about:legacy-compat" encoding="UTF-8" indent="yes" />
	<xsl:template name="header">

		<header>
			<nav id="main-nav" role="primary">
				<a href="/" id="branding">
					<img src="/assets/img/icecast.png" alt="Logo" />
					<h1>Icecast Server administration</h1>
				</a>
				<ul>
					<li class="adminlink"><a href="/admin/stats.xsl">Server status</a></li>
					<li class="adminlink"><a href="/admin/listmounts.xsl">Mountpoint list</a></li>
					<li class="adminlink"><a href="/admin/showlog.xsl">Logfiles</a></li>
					<xsl:for-each select="(/report/extension/icestats | /icestats | /iceresponse)/modules/module">
						<xsl:if test="@management-url and @management-title">
							<li class="adminlink"><a href="{@management-url}"><xsl:value-of select="@management-title" /></a></li>
						</xsl:if>
					</xsl:for-each>
					<li class="right"><a href="/status.xsl">Public area</a></li>
				</ul>
			</nav>
		</header>

	</xsl:template>
</xsl:stylesheet>
