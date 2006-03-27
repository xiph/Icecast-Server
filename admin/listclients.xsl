<xsl:stylesheet xmlns:xsl = "http://www.w3.org/1999/XSL/Transform" version = "1.0" >
<xsl:output omit-xml-declaration="no" method="xml" doctype-public="-//W3C//DTD XHTML 1.0 Strict//EN" doctype-system="http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd" indent="yes" encoding="UTF-8" />
<xsl:template match = "/icestats" >
<html>
<head>
<title>Icecast Streaming Media Server</title>
<link rel="stylesheet" type="text/css" href="/style.css" />
</head>
<body bgcolor="#000" topmargin="0" leftmargin="0" rightmargin="0" bottommargin="0">

<div class="main">
<h1>Listener Stats</h1>
<iframe scrolling="no" frameborder="0" width="100%" src="/adminbar.html" />

<div class="roundcont">
<div class="roundtop">
<img src="/images/corner_topleft.jpg" class="corner" style="display: none" />
</div>
<div class="newscontent">
<xsl:for-each select="source">
<h3>

<xsl:choose>
<xsl:when test="authenticator">
<a href="auth.xsl"><img border="0" src="/images/key.png"/></a> Authentication Required
</xsl:when>
<xsl:otherwise>
<a href="{@mount}.m3u"><img border="0" src="/images/tunein.png"/></a> Mount Point
</xsl:otherwise>
</xsl:choose>
<xsl:if test="server_name"><xsl:value-of select="server_name" /> </xsl:if>
(<xsl:value-of select="@mount" />)
<xsl:if test="authenticator"><a href="manageauth.xsl?mount={@mount}"><img border="0" src="/images/key.png"/></a> </xsl:if>

</h3>
	<table border="0" cellpadding="1" cellspacing="5" bgcolor="444444">
	<tr>        
	    <td align="center">
			<a class="nav2" href="listclients.xsl?mount={@mount}">List Clients</a> | 
        	<a class="nav2" href="moveclients.xsl?mount={@mount}">Move Listeners</a> | 
			<a class="nav2" href="updatemetadata.xsl?mount={@mount}">Update Metadata</a> |
        	<a class="nav2" href="killsource.xsl?mount={@mount}">Kill Source</a>
	    </td></tr>
	</table>
<br />
<table cellspacing="1" border="1" bordercolor="#C0C0C0" >
		<tr>
				<td ><center><b>IP</b></center></td>
				<td ><center><b>Connected For</b></center></td>
				<td ><center><b>User Agent</b></center></td>
				<td ><center><b>Action</b></center></td>
		</tr>
<xsl:variable name = "themount" ><xsl:value-of select="@mount" /></xsl:variable>
<xsl:for-each select="listener">
		<tr>
				<td align="center"><xsl:value-of select="IP" /><xsl:if test="username"> (<xsl:value-of select="username" />)</xsl:if></td>
				<td align="center"><xsl:value-of select="Connected" /> seconds</td>
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
<img src="/images/corner_bottomleft.jpg" class="corner" style="display: none" />
</div>
</div>
<div class="poster">Support icecast development at <a class="nav" href="http://www.icecast.org">www.icecast.org</a></div>
</div>
</body>
</html>
</xsl:template>
</xsl:stylesheet>
