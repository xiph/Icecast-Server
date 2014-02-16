<xsl:stylesheet xmlns:xsl = "http://www.w3.org/1999/XSL/Transform" version = "1.0" >
<xsl:output omit-xml-declaration="no" method="html" doctype-public="-//W3C//DTD XHTML 1.0 Strict//EN" doctype-system="http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd" indent="yes" encoding="UTF-8" />
<xsl:template match = "/icestats" >
<html xmlns="http://www.w3.org/1999/xhtml">
<head>
<title>Icecast Streaming Media Server</title>
<link rel="stylesheet" type="text/css" href="/style.css" />
</head>
<body>
<h2>Icecast2 Admin</h2>

<div class="roundcont">
<div class="roundtop">
<img src="/corner_topleft.jpg" class="corner" style="display: none" alt="" />
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
<img src="/corner_bottomleft.jpg" class="corner" style="display: none" alt="" />
</div>
</div>
<p><br /></p>

<h1>Update Metadata</h1>
<div class="roundcont">
<div class="roundtop">
<img src="/corner_topleft.jpg" class="corner" style="display: none" alt="" />
</div>
<div class="content">
<xsl:for-each select="source">
<h3>
<xsl:if test="server_name"><xsl:value-of select="server_name" /> </xsl:if>
(<xsl:value-of select="@mount" />)</h3>

<form method="get" action="/admin/metadata.xsl">
<table border="0" cellpadding="4">
	<tr><td><p>Metadata : <input type="text" name="song"/></p></td></tr>
<tr><td><input type="submit" value="Update"/></td></tr>
</table>
<div>
	<input type="hidden" name="mount" value="{@mount}"/>
	<input type="hidden" name="mode" value="updinfo"/>
	<input type="hidden" name="charset" value="UTF-8"/>
</div>
</form>

<br />
</xsl:for-each>
<xsl:text disable-output-escaping="yes">&amp;</xsl:text>nbsp;
</div>
<div class="roundbottom">
<img src="/corner_bottomleft.jpg" class="corner" style="display: none" alt="" />
</div>
</div>
<div class="poster">Support icecast development at <a class="nav" href="http://www.icecast.org">www.icecast.org</a></div>
</body>
</html>
</xsl:template>
</xsl:stylesheet>
