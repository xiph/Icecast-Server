<xsl:stylesheet xmlns:xsl = "http://www.w3.org/1999/XSL/Transform" version = "1.0" >
<xsl:output method="html" indent="yes" />
<xsl:template match = "/icestats" >
<html>
<head>
<title>Icecast Streaming Media Server</title>
<link rel="stylesheet" type="text/css" href="style.css" />
</head>
<body bgcolor="black">
<table border="0" width="100%%">
<tr>
<td width="50"></td>
<td>
<h2>Icecast Status Page</h2>
<div class="roundcont">
<div class="roundtop">
<img src="corner_topleft.jpg" class="corner" style="display: none" />
</div>
<div class="newscontent">
<xsl:for-each select="source">
<xsl:choose>
<xsl:when test="listeners">
<h3>
<xsl:if test="server_name"><xsl:value-of select="server_name" /> </xsl:if>
(<xsl:value-of select="@mount" />)</h3>
<table border="0" cellpadding="4">
<xsl:if test="server_name">
<tr><td>Stream Title:</td><td class="streamdata"> <xsl:value-of select="server_name" /></td></tr>
</xsl:if>
<xsl:if test="server_description">
<tr><td>Stream Description:</td><td class="streamdata"> <xsl:value-of select="server_description" /></td></tr>
</xsl:if>
<xsl:if test="type">
<tr><td width="130"> Stream Type:</td><td class="streamdata"><xsl:value-of select="type" /></td></tr>
</xsl:if>
<xsl:if test="bitrate">
<tr><td>Bitrate:</td><td class="streamdata"> <xsl:value-of select="bitrate" /></td></tr>
</xsl:if>
<xsl:if test="listeners">
<tr><td>Stream Listeners:</td><td class="streamdata"> <xsl:value-of select="listeners" /></td></tr>
</xsl:if>
<xsl:if test="genre">
<tr><td>Stream Genre:</td><td class="streamdata"> <xsl:value-of select="genre" /></td></tr>
</xsl:if>
<xsl:if test="server_url">
<tr><td>Stream URL:</td><td class="streamdata"> <a href="{@server_url}"><xsl:value-of select="server_url" /></a></td></tr>
</xsl:if>
<tr><td>Current Song:</td><td class="streamdata"> 
<xsl:if test="artist"><xsl:value-of select="artist" /> - </xsl:if><xsl:value-of select="title" /></td></tr>
<tr><td>Listen:</td><td class="streamdata"> <a href="{@mount}.m3u">Click to Listen</a></td></tr>
</table>
</xsl:when>
<xsl:otherwise>
<h3><xsl:value-of select="@mount" /> - Not Connected</h3>
</xsl:otherwise>
</xsl:choose>
<br></br>
<br></br>
</xsl:for-each>
</div>
<div class="roundbottom">
<img src="corner_bottomleft.jpg" class="corner" style="display: none" />
</div>
</div>
<br></br><br></br>
</td>
<td width="25"></td></tr>
</table>
<div class="poster"><img align="left" src="/icecast.png" />Support icecast development at <a class="nav" href="http://www.icecast.org">www.icecast.org</a></div>
</body>
</html>
</xsl:template>
</xsl:stylesheet>
