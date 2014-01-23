<xsl:stylesheet xmlns:xsl = "http://www.w3.org/1999/XSL/Transform" version = "1.0" >
<xsl:output omit-xml-declaration="no" method="html" doctype-public="-//W3C//DTD XHTML 1.0 Strict//EN" doctype-system="http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd" indent="yes" encoding="UTF-8" />
<xsl:template match = "/icestats" >
<html xmlns="http://www.w3.org/1999/xhtml">
<head>
<title>Icecast Streaming Media Server</title>
<link rel="stylesheet" type="text/css" href="style.css" />
</head>
<body>
<h2>Icecast2 Status</h2>
<!--index header menu -->
<div class="roundcont">
<div class="roundtop">
<img src="/corner_topleft.jpg" class="corner" style="display: none" alt="" />
</div>
<table border="0" width="100%" id="table1" cellspacing="0" cellpadding="4">
	<tr>
		<td style="background-color: #656565">
		<a class="nav" href="admin/">Administration</a>
		<a class="nav" href="status.xsl">Server Status</a>
		<a class="nav" href="server_version.xsl">Version</a></td>
	</tr>
</table>
<div class="roundbottom">
<img src="/corner_bottomleft.jpg" class="corner" style="display: none" alt="" />
</div>
</div>
<p><br /></p>
<!--end index header menu -->
<!--mount point stats-->
<xsl:for-each select="source">
<xsl:choose>
<xsl:when test="listeners">
<div class="roundcont">
<div class="roundtop">
<img src="/corner_topleft.jpg" class="corner" style="display: none" alt="" />
</div>
<xsl:text disable-output-escaping="yes">
&lt;!-- WARNING:
     DO NOT ATTEMPT TO PARSE ICECAST HTML OUTPUT!
     The web interface may change completely between releases.
     If you have a need for automatic processing of server data,
     please read the appropriate documentation. Latest docs:
     http://icecast.org/docs/icecast-latest/icecast2_stats.html
--></xsl:text>
<div class="content">
    <div class="dummy"></div>   
    <div class="streamheading">
        <table cellspacing="0" cellpadding="0">
            <colgroup align="left" />
            <colgroup align="right" width="300" />
            <tr>
                <td><h3>Mount Point <xsl:value-of select="@mount" /></h3></td>
                <xsl:choose>
                    <xsl:when test="authenticator">
                        <td align="right"><a class="auth" href="/auth.xsl">Login</a></td>
                    </xsl:when>
                    <xsl:otherwise>
                        <td align="right">
                            <a href="{@mount}.m3u">M3U</a>
                            <a href="{@mount}.xspf">XSPF</a>
                            <!-- <a href="{@mount}.vclt">VCLT</a> -->
                        </td>
                    </xsl:otherwise>
                </xsl:choose>
        </tr></table>
    </div>

<table border="0" cellpadding="4">
<xsl:if test="server_name">
<tr><td>Stream Name:</td><td class="streamstats"> <xsl:value-of select="server_name" /></td></tr>
</xsl:if>
<xsl:if test="server_description">
<tr><td>Stream Description:</td><td class="streamstats"> <xsl:value-of select="server_description" /></td></tr>
</xsl:if>
<xsl:if test="server_type">
<tr><td>Content Type:</td><td class="streamstats"><xsl:value-of select="server_type" /></td></tr>
</xsl:if>
<xsl:if test="stream_start">
<tr><td>Mount started:</td><td class="streamstats"><xsl:value-of select="stream_start" /></td></tr>
</xsl:if>
<xsl:if test="bitrate">
<tr><td>Bitrate:</td><td class="streamstats"> <xsl:value-of select="bitrate" /></td></tr>
</xsl:if>
<xsl:if test="quality">
<tr><td>Quality:</td><td class="streamstats"> <xsl:value-of select="quality" /></td></tr>
</xsl:if>
<xsl:if test="video_quality">
<tr><td>Video Quality:</td><td class="streamstats"> <xsl:value-of select="video_quality" /></td></tr>
</xsl:if>
<xsl:if test="frame_size">
<tr><td>Framesize:</td><td class="streamstats"> <xsl:value-of select="frame_size" /></td></tr>
</xsl:if>
<xsl:if test="frame_rate">
<tr><td>Framerate:</td><td class="streamstats"> <xsl:value-of select="frame_rate" /></td></tr>
</xsl:if>
<xsl:if test="listeners">
<tr><td>Listeners (current):</td><td class="streamstats"> <xsl:value-of select="listeners" /></td></tr>
</xsl:if>
<xsl:if test="listener_peak">
<tr><td>Listeners (peak):</td><td class="streamstats"> <xsl:value-of select="listener_peak" /></td></tr>
</xsl:if>
<xsl:if test="genre">
<tr><td>Genre:</td><td class="streamstats"> <xsl:value-of select="genre" /></td></tr>
</xsl:if>
<xsl:if test="server_url">
<tr><td>Stream URL:</td><td class="streamstats"> <a href="{server_url}"><xsl:value-of select="server_url" /></a></td></tr>
</xsl:if>
<tr><td>Currently playing:</td><td class="streamstats"> 
<xsl:if test="artist"><xsl:value-of select="artist" /> - </xsl:if><xsl:value-of select="title" /></td></tr>
</table>
</div>
<div class="roundbottom">
<img src="/corner_bottomleft.jpg" class="corner" style="display: none" alt="" />
</div>
</div>
<p><br /></p>
</xsl:when>
<xsl:otherwise>
<h3><xsl:value-of select="@mount" /> - Not Connected</h3>
</xsl:otherwise>
</xsl:choose>

</xsl:for-each>
<p><xsl:text disable-output-escaping="yes">&amp;</xsl:text>nbsp;</p>


<div class="poster">Support icecast development at <a class="nav" href="http://www.icecast.org">www.icecast.org</a></div>
</body>
</html>
</xsl:template>
</xsl:stylesheet>
