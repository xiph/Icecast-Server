<xsl:stylesheet xmlns:xsl = "http://www.w3.org/1999/XSL/Transform" version = "1.0" >
<xsl:output omit-xml-declaration="no" method="html" doctype-public="-//W3C//DTD XHTML 1.0 Strict//EN" doctype-system="http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd" indent="yes" encoding="UTF-8" />
<xsl:template match = "/icestats" >
<html xmlns="http://www.w3.org/1999/xhtml">
<head>
<title>Icecast Streaming Media Server</title>
<link rel="stylesheet" type="text/css" href="style.css" />
</head>
<body>
<h2>Server Information</h2>
<p><br /></p>
<!--index header menu -->
<div class="roundcont">
<div class="roundtop">
<img src="/corner_topleft.jpg" class="corner" style="display: none" alt="" />
</div>
<table border="0" style="width: 100%" id="table1" cellspacing="0" cellpadding="4">
	<tr>
		<td style="background-color: #656565">
	    <a class="nav" href="admin/">Administration</a>
		<a class="nav" href="status.xsl">Server Status</a>
		<a class="nav" href="server_version.xsl">Version</a></td>
	</tr>
</table>
<div class="roundbottom">
<img src="/corner_bottomleft.jpg" class="corner" style="display: none" alt="" />
</div>
</div>
<p><br /></p>
<!--end index header menu -->

<div class="roundcont">
<div class="roundtop">
<img src="/corner_topleft.jpg" class="corner" style="display: none" alt="" />
</div>
<div class="content">
<h3>Server Information</h3>
<table border="0" cellpadding="4">
<xsl:for-each select="/icestats">
<tr>
	<td style="width: 130px">Location</td>
	<td class="streamdata"><xsl:value-of select="location" /></td>
</tr>
<tr>
	<td style="width: 130px">Admin</td>
	<td class="streamdata"><xsl:value-of select="admin" /></td>
</tr>
<tr>
    <td style="width: 130px">Host</td>
    <td class="streamdata"><xsl:value-of select="host" /></td>
</tr>
<tr>
    <td style="width: 130px">Version</td>
    <td class="streamdata"><xsl:value-of select="server_id" /></td>
</tr>
</xsl:for-each>
<tr>
	<td style="width: 130px">Download</td>
	<td class="streamdata"><a class="nav" href="http://icecast.org/download.php">icecast.org</a></td>
</tr>
<tr>
	<td style="width: 130px">Subversion</td>
	<td class="streamdata"><a class="nav" href="http://icecast.org/svn.php">click here</a></td>
</tr>
<tr>
	<td style="width: 130px">Documentation</td>
	<td class="streamdata"><a class="nav" href="http://icecast.org/docs.php">click here</a></td>
</tr>
<tr>
	<td style="width: 130px">Stream Directory </td>
	<td class="streamdata"><a class="nav" href="http://dir.xiph.org/index.php">dir.xiph.org</a></td>
</tr>
<tr>
	<td style="width: 130px">Community</td>
	<td class="streamdata"><a class="nav" href="http://icecast.org/community.php">click here</a></td>
</tr>
</table>
</div>
<div class="roundbottom">
<img src="/corner_bottomleft.jpg" class="corner" style="display: none" alt="" />
</div>
</div>
<p><br /></p>

<div class="poster">Support icecast development at <a class="nav" href="http://www.icecast.org">www.icecast.org</a></div>
</body>
</html>
</xsl:template>
</xsl:stylesheet>
