<xsl:stylesheet xmlns:xsl = "http://www.w3.org/1999/XSL/Transform" version = "1.0" >
<xsl:output omit-xml-declaration="no" method="xml" doctype-public="-//W3C//DTD XHTML 1.0 Strict//EN" doctype-system="http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd" indent="yes" encoding="UTF-8" />
<xsl:template match = "/icestats" >
<html>
<head>
<title>Icecast Streaming Media Server</title>
<link rel="stylesheet" type="text/css" href="style.css" />
</head>
<body bgcolor="#000" topmargin="0" leftmargin="0" rightmargin="0" bottommargin="0">

<div class="main">
<h1>Icecast2 Status (Server Uptime)</h1>
<iframe scrolling="no" frameborder="0" width="100%" src="/navbar.html" />

<div class="roundcont">
<div class="roundtop">
<img src="/images/corner_topleft.jpg" class="corner" style="display: none" />
</div>
<div class="newscontent">
<h3>Server Stats</h3>
<table border="0" cellpadding="4">
<xsl:for-each select="/icestats">
<xsl:for-each select="server_start">
<xsl:if test = "name()!='source'"> 
<tr>
	<td width="130">Server Uptime</td>
	<td class="streamdata"><xsl:value-of select="." /></td>
</tr>
</xsl:if>
</xsl:for-each>
</xsl:for-each>
</table>
</div>
<div class="roundbottom">
<img src="/images/corner_bottomleft.jpg" class="corner" style="display: none" />
</div>
</div>
<br />
<br />

<div class="poster">Support icecast development at <a class="nav" target="_blank" href="http://www.icecast.org">www.icecast.org</a></div>
</div>
</body>
</html>
</xsl:template>
</xsl:stylesheet>
