<xsl:stylesheet xmlns:xsl = "http://www.w3.org/1999/XSL/Transform" version = "1.0" >
<xsl:output method="html" indent="yes" />
<xsl:template match = "/icestats" >
<HTML>
<HEAD>
<title>Icecast 2 Stats</title>
<style type="text/css">
a:hover {color: black; font-family:Verdana}
.default1 {color: #505050; font-family:Verdana; font-size:9pt; font-weight: normal}
.default2 {color: #252525; font-family:Verdana; font-size:9pt; font-weight: normal}
.mount {color: White; font-family:Verdana; font-size:9pt; font-weight: normal}
.icelogo {color: #0099D4; font-family: Verdana; font-size: 25pt; font-weight: normal; letter-spacing : -2.5px;}
.ltv {color: gray; font-family: Verdana; font-size: 9pt; font-weight: normal;}
</style>

</HEAD>
<BODY topmargin="0" leftmargin="0" marginheight="0" marginwidth="0" bgcolor="#EFEFEF" text="#0099D4" link="#0000FF" vlink="#FF00FF" alink="#FF0000" >
<font class="default">
<table width="100%" border="0" cellpadding="0" cellspacing="0">
<tr>
<td height="50">
<font class="icelogo">Icecast 2 Status</font>
</td>
</tr>
<tr>
<td height="14" align="right">
</td>
</tr>
<tr>
<td bgcolor="#007B79" height="20" align="center">
</td>
</tr>
</table>

<br></br>
<xsl:for-each select="source">
<table cellpadding="0" cellspacing="0" border="0" width="100%">
<tr>
	<td bgcolor="#B4E6FF"  colspan="5">
	<table cellspacing="0" cellpadding="0" border="0">
		<tr>
		<td></td>
		</tr>
		<tr>
		<td></td>
		</tr>
		<tr>
		<td></td>
		</tr>
	</table>
	</td>
</tr>
</table>
<table cellpadding="5" cellspacing="0" border="0" width="100%">
<tr>
<td bgcolor="#5BB2EB" colspan="2" align="center">
<center>
<font class="mount">Current Stream Information<br></br>
<xsl:value-of select="@mount" />
</font>
</center>
</td>
</tr>
</table>
<table cellpadding="0" cellspacing="0" border="0" width="100%">
<tr>
	<td bgcolor="#B4E6FF"  colspan="5">
	<table cellspacing="0" cellpadding="0" border="0">
		<tr>
		<td></td>
		</tr>
	</table>
	</td>
</tr>
</table>
<table cellpadding="2" cellspacing="0" border="0" align="center">
<tr>
<td width="100" >
</td>
</tr>
<tr>
<td width="100" >
<font class="default1">Stream Status: </font>
</td>
<td>
<font class="default2">
<b><xsl:value-of select="listeners" /> listeners</b>
</font>
</td>
</tr>
<tr>
<td width="100" >
<font class="default1">Stream Title: </font>
</td>
<td>
<font class="default2">
<b><xsl:value-of select="description" /></b>
</font>
</td>
</tr>
<tr>
<td width="100" >
<font class="default1">Stream Genre: </font>
</td>
<td>
<font class="default2">
<b></b>
</font>
</td>
</tr>
<tr>
<td width="100" >
<font class="default1">Stream URL: </font>
</td>
<td>
<font class="default2">
<b>
<xsl:value-of select="url" />
</b>
</font>
</td>
</tr>
<tr>
<td width="100" >
<font class="default1">Current Song: </font>
</td>
<td>
<font class="default2">
<b><xsl:value-of select="artist" /> - <xsl:value-of select="title" /></b>
</font>
</td>
</tr>
<tr>
<td width="100" >
<font class="default1">Listen: </font>
</td>
<td>
<font class="default2">
<a href="{@mount}">Here</a>
</font>
</td>
</tr>
</table>
<table cellpadding="0" cellspacing="0" border="0" width="100%">
<tr>
	<td bgcolor="#B4E6FF"  colspan="5">
	<table cellspacing="0" cellpadding="0" border="0">
		<tr>
		<td></td>
		</tr>
	</table>
	</td>
</tr>
</table>
<br></br>
<br></br>
</xsl:for-each>
<table celspacing="0" cellpadding="0" border="0">
<tr>
<td  colspan="5" align="center">
<font class="mount">
<a href="http://www.icecast.org">Icecast development team</a>
</font>
</td>
</tr>
</table>
</font>
</BODY>
</HTML>
</xsl:template>
</xsl:stylesheet>
