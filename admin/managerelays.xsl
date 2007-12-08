<xsl:stylesheet xmlns:xsl = "http://www.w3.org/1999/XSL/Transform" version = "1.0" >
<xsl:output method="xml" media-type="text/html" indent="yes" encoding="UTF-8"
    doctype-system="http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd"
    doctype-public="-//W3C//DTD XHTML 1.0 Transitional//EN" />

<xsl:template match = "/icerelaystats" >
<html>
<head>
<title>Icecast Streaming Media Server</title>
<link rel="stylesheet" type="text/css" href="/style.css" />
</head>
<body>

<div class="main">

<div class="roundcont">
<div class="roundtop">
<img src="/images/corner_topleft.jpg" class="corner" style="display: none" />
</div>
<div class="newscontent">
<xsl:for-each select="relay">
<h3>
<xsl:value-of select="localmount" />
<xsl:choose>
<xsl:when test = "enable!='0'">
    (<a href="managerelays.xsl?relay={localmount}&amp;enable=0">click to disable</a>)
</xsl:when>
<xsl:otherwise>
    (<a href="managerelays.xsl?relay={localmount}&amp;enable=1">click to enable</a>)
</xsl:otherwise>
</xsl:choose>
</h3>
<table border="0" cellpadding="4">
    <xsl:for-each select="master">
        <tr><td>Master</td> <td class="streamdata"> <xsl:value-of select="server" /></td>
        <td class="streamdata"> <xsl:value-of select="port" /></td>
        <td class="streamdata"> <xsl:value-of select="mount" /></td></tr>
    </xsl:for-each>
    <tr> <td>on demand</td> <td class="streamdata"> <xsl:value-of select="on_demand" /></td> </tr>
    <tr> <td>slave relay</td> <td class="streamdata"> <xsl:value-of select="from_master" /></td> </tr>
</table>
<br />
<br></br>
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
