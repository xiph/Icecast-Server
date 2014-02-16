<xsl:stylesheet xmlns:xsl = "http://www.w3.org/1999/XSL/Transform" version = "1.0" >
<xsl:output omit-xml-declaration="no" method="html" doctype-public="-//W3C//DTD XHTML 1.0 Strict//EN" doctype-system="http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd" indent="yes" encoding="UTF-8" />
<xsl:template match = "/iceresponse" >
<html xmlns="http://www.w3.org/1999/xhtml">
<head>
<title>Icecast Streaming Media Server</title>
<link rel="stylesheet" type="text/css" href="/style.css" />
</head>
<body>
<img align="left" src="/icecast.png" /><h2><center>Icecast2 Admin</center></h2>

<div class="roundcont">
<div class="roundtop">
<img src="/corner_topleft.jpg" class="corner" style="display: none" alt="" />
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
<img src="/corner_bottomleft.jpg" class="corner" style="display: none" alt="" />
</div>
</div>
<p><br /></p>

<h1>Icecast Server Response</h1>
<div class="roundcont">
<div class="roundtop">
<img src="/corner_topleft.jpg" class="corner" style="display: none" alt="" />
</div>
<div class="content">
<h3>Response</h3>
<xsl:for-each select="/iceresponse">
Message : <xsl:value-of select="message" /><br></br>
Return Code: <xsl:value-of select="return" /><br></br>
</xsl:for-each>
<br />
<br />
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
