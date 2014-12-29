<xsl:stylesheet xmlns:xsl = "http://www.w3.org/1999/XSL/Transform" version = "1.0">
	<xsl:output method="html" doctype-system="about:legacy-compat" encoding="UTF-8" indent="yes" />
	<xsl:template name="header">

		<div class="header">
			<h1>
				<a href="/" title="Home page">Icecast</a>
				<xsl:text> </xsl:text>
				<span>administration</span>
			</h1>
			<div class="nav">
				<label for="toggle-nav" class="nobar" title="Toggle navigation"></label>
				<input type="checkbox" id="toggle-nav" />
				<ul>
					<li class="on"><a href="/admin/stats.xsl">Administration</a></li>
					<li><a href="/admin/listmounts.xsl">Mountpoint list</a></li>
					<li><a href="/status.xsl">Public area</a></li>
				</ul>
			</div>
		</div>

	</xsl:template>
</xsl:stylesheet>