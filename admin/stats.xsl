<xsl:stylesheet xmlns:xsl = "http://www.w3.org/1999/XSL/Transform" version = "1.0" >
<xsl:output omit-xml-declaration="no" method="xml" doctype-public="-//W3C//DTD XHTML 1.0 Strict//EN" doctype-system="http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd" indent="yes" encoding="UTF-8" />
<xsl:template match = "/icestats" >
<html>
<head>
<title>Icecast Streaming Media Server</title>
<link rel="stylesheet" type="text/css" href="/style.css" />
</head>
<body>
	<center>
	<table border="0" cellpadding="1" cellspacing="3">
	<tr>        
	    <td align="center">
		<a class="nav" href="listmounts.xsl">List Mountpoints</a> | 
        	<a class="nav" href="moveclients.xsl">Move Listeners</a> | 
        	<a class="nav" href="stats.xsl">Stats</a> | 
        	<a class="nav" href="/status.xsl">Status Page</a>
	    </td></tr>
	</table>
	</center>
<h2>Icecast Status Page</h2>
<div class="roundcont">
<div class="roundtop">
<img src="/corner_topleft.jpg" class="corner" style="display: none" />
</div>
<div class="newscontent">
<h3>Global Server Stats</h3>
<table border="0" cellpadding="4">
<xsl:for-each select="/icestats">
<xsl:for-each select="*">
<xsl:if test = "name()!='source'"> 
<tr>
	<td width="130"><xsl:value-of select="name()" /></td>
	<td class="streamdata"><xsl:value-of select="." /></td>
</tr>
</xsl:if>
</xsl:for-each>
</xsl:for-each>
</table>
</div>
<div class="roundbottom">
<img src="/corner_bottomleft.jpg" class="corner" style="display: none" />
</div>
</div>
<br />
<br />

<div class="roundcont">
<div class="roundtop">
<img src="/corner_topleft.jpg" class="corner" style="display: none" />
</div>
<div class="newscontent">
<xsl:for-each select="source">
<xsl:if test = "listeners!=''"> 
<h3>
<xsl:if test="server_name"><xsl:value-of select="server_name" /> </xsl:if>
(<xsl:value-of select="@mount" />)
<xsl:if test="authenticator"> <a href="manageauth.xsl?mount={@mount}"><img border="0" src="/key.gif"/></a> </xsl:if>
</h3>
	<table border="0" cellpadding="1" cellspacing="5" bgcolor="444444">
	<tr>        
	    <td align="center">
		<a class="nav2" href="listclients.xsl?mount={@mount}">List Clients</a> | 
        	<a class="nav2" href="moveclients.xsl?mount={@mount}">Move MountPoints</a> | 
        	<a class="nav2" href="killsource.xsl?mount={@mount}">Kill Source</a>
                <xsl:if test="authenticator"> | <a class="nav2" href="manageauth.xsl?mount={@mount}">Manage Authentication</a></xsl:if>
	    </td></tr>
	</table>
<br />
<table cellpadding="5" cellspacing="0" border="0">
	<xsl:for-each select="*">
	<tr>
		<td width="130"><xsl:value-of select="name()" /></td>
		<td class="streamdata"><xsl:value-of select="." /></td>
	</tr>
	</xsl:for-each>
</table>
<br />
<br />
</xsl:if>
</xsl:for-each>
<xsl:text disable-output-escaping="yes">&amp;</xsl:text>nbsp;
</div>
<div class="roundbottom">
<img src="/corner_bottomleft.jpg" class="corner" style="display: none" />
</div>
</div>
<div class="poster">
<img align="left" src="/icecast.png" />Support icecast development at <a class="nav" href="http://www.icecast.org">www.icecast.org</a></div>
</body>
</html>
</xsl:template>
</xsl:stylesheet>
