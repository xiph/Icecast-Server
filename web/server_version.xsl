<xsl:stylesheet xmlns:xsl = "http://www.w3.org/1999/XSL/Transform" version = "1.0" xmlns="http://www.w3.org/1999/xhtml">
<xsl:output omit-xml-declaration="no" method="xml" doctype-public="-//W3C//DTD XHTML 1.0 Strict//EN" doctype-system="http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd" indent="yes" encoding="UTF-8" />
<xsl:include href="includes/web-page.xsl"/>
<xsl:variable name="title">Server Information</xsl:variable>
<xsl:template name="content">
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
					<td><a href="http://icecast.org/download.php">icecast.org</a></td>
				</tr>
				<tr>
					<td>Subversion</td>
					<td><a href="http://icecast.org/svn.php">icecast.org/svn.php</a></td>
				</tr>
				<tr>
					<td>Documentation</td>
					<td><a href="http://icecast.org/docs.php">icecast.org/docs.php</a></td>
				</tr>
				<tr>
					<td>Stream Directory</td>
					<td><a href="http://dir.xiph.org/index.php">dir.xiph.org</a></td>
				</tr>
				<tr>
					<td>Community</td>
					<td><a href="http://icecast.org/community.php">icecast.org/community.php</a></td>
				</tr>
			</tbody>
		</table>
	</section>
</xsl:template>
</xsl:stylesheet>
