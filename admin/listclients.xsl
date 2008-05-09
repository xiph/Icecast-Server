<xsl:stylesheet xmlns:xsl = "http://www.w3.org/1999/XSL/Transform" version = "1.0" >
<xsl:output omit-xml-declaration="no" method="html" doctype-public="-//W3C//DTD XHTML 1.0 Strict//EN" doctype-system="http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd" indent="yes" encoding="UTF-8" />
<xsl:template match = "/icestats" >
<html>
<head>
<title>Icecast Streaming Media Server</title>
<link rel="stylesheet" type="text/css" href="/style.css" />
</head>
<body topmargin="0" leftmargin="0" rightmargin="0" bottommargin="0">
<h2>Icecast2 Admin</h2>
<br />

<div class="roundcont">
<div class="roundtop">
<img src="/corner_topleft.jpg" class="corner" style="display: none" />
</div>
	<table border="0" cellpadding="1" cellspacing="3">
	<tr>        
	    <td align="center">
	        <a class="nav" href="stats.xsl">Admin Home</a>
		    <a class="nav" href="listmounts.xsl">List Mountpoints</a>
        	<a class="nav" href="moveclients.xsl">Move Listeners</a>
        	<a class="nav" href="/status.xsl">Index</a>
	    </td></tr>
	</table>
<div class="roundbottom">
<img src="/corner_bottomleft.jpg" class="corner" style="display: none" />
</div>
</div>
<br />
<br />



<h1>Listener Stats</h1>
<div class="roundcont">
<div class="roundtop">
<img src="/corner_topleft.jpg" class="corner" style="display: none" />
</div>
<div class="newscontent">
<xsl:for-each select="source">
<div class="streamheader">
    <table cellspacing="0" cellpadding="0" >
        <colgroup align="left" />
        <colgroup align="right" width="300" />
        <tr>
            <td><h3>Mount Point <xsl:value-of select="@mount" /></h3></td>
            <xsl:choose>
                <xsl:when test="authenticator">
                    <td align="right"><a class="auth" href="/auth.xsl">Login</a></td>
                </xsl:when>
                <xsl:otherwise>
                    <td align="right">
                        <a href="{@mount}.m3u">M3U</a>
                        <a href="{@mount}.xspf">XSPF</a></td>
                </xsl:otherwise>
            </xsl:choose>
    </tr></table>
</div>

<table border="0" cellpadding="1" cellspacing="5" bgcolor="444444">
	<tr>        
	    <td align="center">
			<a class="nav2" href="listclients.xsl?mount={@mount}">List Clients</a>
        	<a class="nav2" href="moveclients.xsl?mount={@mount}">Move Listeners</a>
			<a class="nav2" href="updatemetadata.xsl?mount={@mount}">Update Metadata</a>
        	<a class="nav2" href="killsource.xsl?mount={@mount}">Kill Source</a>
	    </td></tr>
	</table>
<br />
<table cellspacing="0" cellpadding="5" border="1" bordercolor="#C0C0C0" >
		<tr>
				<td ><center><b>IP</b></center></td>
				<td ><center><b>Seconds Connected</b></center></td>
				<td ><center><b>User Agent</b></center></td>
				<td ><center><b>Action</b></center></td>
		</tr>
<xsl:variable name = "themount" ><xsl:value-of select="@mount" /></xsl:variable>
<xsl:for-each select="listener">
		<tr>
				<td align="center"><xsl:value-of select="IP" /><xsl:if test="username"> (<xsl:value-of select="username" />)</xsl:if></td>
				<td align="center"><xsl:value-of select="Connected" /></td>
				<td align="center"><xsl:value-of select="UserAgent" /></td>
				<td align="center"><a href="killclient.xsl?mount={$themount}&amp;id={ID}">Kick</a></td>
		</tr>
</xsl:for-each>
</table>
<br />
<br />
</xsl:for-each>
<xsl:text disable-output-escaping="yes">&amp;</xsl:text>nbsp;
</div>
<div class="roundbottom">
<img src="/corner_bottomleft.jpg" class="corner" style="display: none" />
</div>
</div>
<div class="poster">Support icecast development at <a class="nav" href="http://www.icecast.org">www.icecast.org</a></div>
</body>
</html>
</xsl:template>
</xsl:stylesheet>
