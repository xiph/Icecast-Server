<xsl:stylesheet xmlns:xsl = "http://www.w3.org/1999/XSL/Transform" version = "1.0" >
<xsl:output method="xml" media-type="text/html" indent="yes" encoding="UTF-8"
    doctype-system="http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd"
    doctype-public="-//W3C//DTD XHTML 1.0 Transitional//EN" />

<xsl:template match = "/icestats" >
<html>
<head>
<title>Icecast Streaming Media Server</title>
<link rel="stylesheet" type="text/css" href="/style.css" />
</head>
<body bgcolor="#000" topmargin="0" leftmargin="0" rightmargin="0" bottommargin="0">

<div class="main">
<h1>Moving Listeners From (<xsl:value-of select="current_source" />)</h1>
<iframe scrolling="no" frameborder="0" width="100%" src="/adminbar.html" />

<xsl:variable name = "currentmount" ><xsl:value-of select="current_source" /></xsl:variable>
<div class="roundcont">
<div class="roundtop">
<img src="/images/corner_topleft.jpg" class="corner" style="display: none" />
</div>
<div class="newscontent">
<h3>Move to which mountpoint ?</h3>
<xsl:for-each select="source">
	<table border="0" cellpadding="6" cellspacing="5" >
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
<img src="/images/corner_bottomleft.jpg" class="corner" style="display: none" />
</div>
</div>
<div class="poster">Support icecast development at <a class="nav" href="http://www.icecast.org">www.icecast.org</a></div>
</div>
</body>
</html>

</xsl:template>
</xsl:stylesheet>
