<xsl:stylesheet xmlns:xsl = "http://www.w3.org/1999/XSL/Transform" version = "1.0" >
<xsl:output omit-xml-declaration="no" method="xml" doctype-public="-//W3C//DTD XHTML 1.0 Strict//EN" doctype-system="http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd" indent="yes" encoding="UTF-8" />
<xsl:template match = "/icestats" >
<html>
<head>
<title>Icecast Streaming Media Server</title>
<link rel="stylesheet" type="text/css" href="/style.css" />
</head>
<body topmargin="0" leftmargin="0" rightmargin="0" bottommargin="0">
<img align="left" src="/icecast.png" /><h2><center>Icecast2 Admin</center></h2>
<br />

<div class="roundcont">
<div class="roundtop">
<img src="/corner_topleft.jpg" class="corner" style="display: none" />
</div>
	<table border="0" cellpadding="1" cellspacing="3">
	<tr>        
	    <td align="center">
	        <a class="nav" href="stats.xsl">Admin Home</a> |
		    <a class="nav" href="listmounts.xsl">List Mountpoints</a> | 
        	<a class="nav" href="moveclients.xsl">Move Listeners</a> | 
        	<a class="nav" href="/status.xsl">Index</a>
	    </td></tr>
	</table>
<div class="roundbottom">
<img src="/corner_bottomleft.jpg" class="corner" style="display: none" />
</div>
</div>
<br />
<br />


<h1>Active Mountpoints</h1>
<div class="roundcont">
<div class="roundtop">
<img src="/corner_topleft.jpg" class="corner" style="display: none" />
</div>
<div class="newscontent">
<xsl:for-each select="source">
<h3>
<xsl:choose>
<xsl:when test="authenticator">
<a href="/auth.xsl"><img border="0" src="/key.png"/></a> Authentication Required
</xsl:when>
<xsl:otherwise>
<a href="{@mount}.m3u"><img border="0" src="/tunein.png"/></a> Mount Point
</xsl:otherwise>
</xsl:choose>
<xsl:if test="server_name"><xsl:value-of select="server_name" /> </xsl:if>
(<xsl:value-of select="@mount" />)
<xsl:if test="authenticator"> <a href="manageauth.xsl?mount={@mount}"><img border="0" src="/key.png"/></a> </xsl:if>


</h3>
	<table border="0" cellpadding="1" cellspacing="5" bgcolor="444444">
	<tr>        
	    <td align="center">
			<a class="nav2" href="listclients.xsl?mount={@mount}">List Clients</a> | 
        	<a class="nav2" href="moveclients.xsl?mount={@mount}">Move Listeners</a> | 
			<a class="nav2" href="updatemetadata.xsl?mount={@mount}">Update Metadata</a> |
        	<a class="nav2" href="killsource.xsl?mount={@mount}">Kill Source</a>
                <xsl:if test="authenticator"> | <a class="nav2" href="manageauth.xsl?mount={@mount}">Manage Authentication</a></xsl:if>
	    </td></tr>
	</table>
<br />
<p><xsl:value-of select="listeners" /> Listener(s)</p>
<br></br>
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
