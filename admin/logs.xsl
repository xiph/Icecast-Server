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
<body>

<div class="main">
<h1>Icecast2 logs</h1>
<iframe frameborder="0" scrolling="no" height="50" src="/adminbar.html" />

<div class="roundcont">
<div class="roundtop">
<img src="/images/corner_topleft.jpg" class="corner" style="display: none" />
</div>
<div class="newscontent">
<h3>Access log</h3>
<iframe frameborder="0" width="100%" height="400" src="showlog.xsl?log=accesslog">
no frame support however contents can be found <a href="showlog.xsl?log=accesslog">here</a>
</iframe>
</div>
<div class="roundbottom">
<img src="/images/corner_bottomleft.jpg" class="corner" style="display: none" />
</div>
</div>
<br />
<br />

<div class="roundcont">
<div class="roundtop">
<img src="/images/corner_topleft.jpg" class="corner" style="display: none" />
</div>
<div class="newscontent">
<h3>Error log</h3>
<iframe frameborder="0" width="100%" height="400" padding="5"  src="showlog.xsl?log=errorlog">
no frame support however contents can be found <a href="showlog.xsl?log=errorlog">here</a>
</iframe>
</div>
<div class="roundbottom">
<img src="/images/corner_bottomleft.jpg" class="corner" style="display: none" />
</div>
</div>
<br />
<br />

<div class="roundcont">
<div class="roundtop">
<img src="/images/corner_topleft.jpg" class="corner" style="display: none" />
</div>
<div class="newscontent">
<h3>Playlist log</h3>
<iframe frameborder="0" width="100%" height="300" src="showlog.xsl?log=playlistlog">
no frame support however contents can be found <a href="showlog.xsl?log=playlistlog">here</a>
</iframe>
</div>
<div class="roundbottom">
<img src="/images/corner_bottomleft.jpg" class="corner" style="display: none" />
</div>
</div>
<br />
<br />

<div class="poster">Support icecast development at <a class="nav" href="http://www.icecast.org">www.icecast.org</a></div>
</div>
</body>
</html>

</xsl:template>
</xsl:stylesheet>
