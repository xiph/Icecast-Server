<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" version="1.0">
<xsl:include href="includes/web-page.xsl"/>
<xsl:variable name="title">Server Information</xsl:variable>
<xsl:template name="content">
	<h2>Version</h2>
	<section class="box">
		<h3 class="box_title">Server Information</h3>
		<table class="table-keys">
			<tbody>
				<xsl:for-each select="/icestats">
				<tr>
					<td>Location</td>
					<td><xsl:value-of select="location" /></td>
				</tr>
				<tr>
					<td>Admin</td>
					<td><xsl:value-of select="admin" /></td>
				</tr>
				<tr>
					<td>Host</td>
					<td><xsl:value-of select="host" /></td>
				</tr>
				<tr>
					<td>Version</td>
					<td><xsl:value-of select="server_id" /></td>
				</tr>
				</xsl:for-each>
				<tr>
					<td>Download</td>
					<td><a href="https://icecast.org/download/">icecast.org/download/</a></td>
				</tr>
				<tr>
					<td>Git</td>
					<td><a href="https://icecast.org/download/#git">icecast.org/download/#git</a></td>
				</tr>
				<tr>
					<td>Documentation</td>
					<td><a href="https://icecast.org/docs/">icecast.org/docs/</a></td>
				</tr>
				<tr>
					<td>Stream Directory</td>
					<td><a href="https://dir.xiph.org/">dir.xiph.org</a></td>
				</tr>
				<tr>
					<td>Community</td>
					<td><a href="https://icecast.org/contact/">icecast.org/contact/</a></td>
				</tr>
			</tbody>
		</table>
	</section>
</xsl:template>
</xsl:stylesheet>
