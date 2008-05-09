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

<xsl:variable name = "currentmount" ><xsl:value-of select="current_source" /></xsl:variable>
<h1>Moving Listeners From (<xsl:value-of select="current_source" />)</h1>
<div class="roundcont">
<div class="roundtop">
<img src="/corner_topleft.jpg" class="corner" style="display: none" />
</div>
<div class="newscontent">
<h3>Move to which mountpoint ?</h3>
<xsl:for-each select="source">
	<table border="0" cellpadding="1" cellspacing="5" >
	<tr>        
		<td>Move from (<xsl:copy-of select="$currentmount" />) to (<xsl:value-of select="@mount" />)</td>
		<td><xsl:value-of select="listeners" /> Listeners</td>
		<td><a class="nav2" href="moveclients.xsl?mount={$currentmount}&amp;destination={@mount}">Move Clients</a></td>
	</tr>        
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
