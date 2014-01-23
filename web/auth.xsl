<xsl:stylesheet xmlns:xsl = "http://www.w3.org/1999/XSL/Transform" version = "1.0" >
<xsl:output omit-xml-declaration="no" method="html" doctype-public="-//W3C//DTD XHTML 1.0 Strict//EN" doctype-system="http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd" indent="yes" encoding="UTF-8" />
<xsl:template match = "/icestats" >
<html xmlns="http://www.w3.org/1999/xhtml">
<head>
<title>Icecast Streaming Media Server</title>
<link rel="stylesheet" type="text/css" href="style.css" />
</head>
<body>
<table border="0" width="100%%">
<tr>
<td width="50"></td>
<td>
<h2>Authorization Page</h2>
<div class="roundcont">
<div class="roundtop">
<img src="/corner_topleft.jpg" class="corner" style="display: none" alt="" />
</div>
<div class="content">
<xsl:for-each select="source">
<xsl:choose>
<xsl:when test="listeners">
<xsl:if test="authenticator">
<xsl:if test="server_name"><xsl:value-of select="server_name" /> </xsl:if>
<h3>(<xsl:value-of select="@mount" />)</h3>
<form method="GET" action="/admin/buildm3u">
<table border="0" cellpadding="4">
<tr><td>Username : <input type="text" name="username"/></td></tr>
<tr><td>Password : <input type="password" name="password"/></td></tr>
<tr><td><input type="Submit" value="Login"/></td></tr>
</table>
<input type="hidden" name="mount" value="{@mount}"/>
</form>
</xsl:if>
</xsl:when>
<xsl:otherwise>
<h3><xsl:value-of select="@mount" /> - Not Connected</h3>
</xsl:otherwise>
</xsl:choose>
<br />
<br />
</xsl:for-each>
<xsl:text disable-output-escaping="yes">&amp;</xsl:text>nbsp;
</div>
<div class="roundbottom">
<img src="/corner_bottomleft.jpg" class="corner" style="display: none" alt="" />
</div>
</div>
<br /><br />
</td>
<td width="25"></td></tr>
</table>
<div class="poster">Support icecast development at <a class="nav" href="http://www.icecast.org">www.icecast.org</a></div>
</body>
</html>
</xsl:template>
</xsl:stylesheet>
