<xsl:stylesheet xmlns:xsl = "http://www.w3.org/1999/XSL/Transform" version = "1.0" >
<xsl:output method="html" indent="yes" />
<xsl:template match = "/icestats" >
<HTML>
<HEAD>
<title>Icecast 2 Stats</title>
<style type="text/css">
a:hover {color: #BBBBBB;}
a {color: black;}
.default1 {color: #505050; font-family:Verdana; font-size:9pt; font-weight: normal}
.default2 {color: #252525; font-family:Verdana; font-size:9pt; font-weight: normal}
.mount {color: White; font-family:Verdana; font-size:9pt; font-weight: normal}
.icelogo {color: #0099D4; font-family: Verdana; font-size: 25pt; font-weight: normal; letter-spacing : -2.5px;}
.ltv {color: gray; font-family: Verdana; font-size: 9pt; font-weight: normal;}
</style>

</HEAD>
<BODY topmargin="0" leftmargin="0" marginheight="0" marginwidth="0" bgcolor="#EFEFEF" text="#0099D4" link="#0000FF" vlink="#FF00FF" alink="#FF0000" >
<font class="default">

<table border="0" cellpadding="5" cellspacing="5" >
    <tr>
        <td><a href="listmounts.xsl">List MountPoints</a></td>
        <td><a href="moveclients.xsl">Move MountPoints</a></td>
        <td><a href="stats.xsl">Stats</a></td>
        <td><a href="/status.xsl">Status Page</a></td>
    </tr>
</table>
<table width="100%" border="0" cellpadding="0" cellspacing="0">
		<tr>
				<td height="50">
						<font class="icelogo">Icecast 2 Global Server Info</font>
				</td>
		</tr>
		<tr>
				<td height="14" align="right"></td>
		</tr>
		<tr>
				<td bgcolor="#007B79" height="20" align="center"></td>
		</tr>
</table>
<br></br>
<table cellpadding="5" cellspacing="0" border="0" width="100%">
	<tr>
		<td bgcolor="#5BB2EB">
		<table width="100%" border="2" cellpadding="0" cellspacing="0" bgcolor="#FFFFFF">
			<tr>
				<td><font size="+2" color="black">Statistic</font></td>
				<td><font size="+2" color="black">Value</font></td>
			</tr>
<xsl:for-each select="/icestats">
<xsl:for-each select="*">
<xsl:if test = "name()!='source'"> 
			<tr>
				<td><xsl:value-of select="name()" /></td>
				<td><xsl:value-of select="." /></td>
			</tr>
</xsl:if>
</xsl:for-each>
</xsl:for-each>
		</table>
		</td>
	</tr>
</table>
<br></br>
<table width="100%" border="0" cellpadding="0" cellspacing="0">
		<tr>
				<td height="50">
						<font class="icelogo">Icecast 2 Mountpoint Info</font>
				</td>
		</tr>
		<tr>
				<td height="14" align="right"></td>
		</tr>
		<tr>
				<td bgcolor="#007B79" height="20" align="center"></td>
		</tr>
</table>
<br></br>
<xsl:for-each select="source">
<xsl:if test = "listeners!=''"> 
<table cellpadding="5" cellspacing="0" border="0" width="100%">
	<tr>
		<td bgcolor="#5BB2EB">
		<table cellpadding="5" cellspacing="0" border="0" width="100%">
				<tr>
						<td bgcolor="#5BB2EB" align="center">
						<font class="mount">(<xsl:value-of select="@mount" />)<br></br>
						</font>
						</td>
				</tr>
				<tr>
						<td bgcolor="#CCDDDD">
						<a href="listclients.xsl?mount={@mount}">List Clients</a>
						</td>
				</tr>
		</table>
		<table width="100%" border="2" cellpadding="0" cellspacing="0" bgcolor="#FFFFFF">
			<tr>
				<td><font size="+2" color="black">Statistic</font></td>
				<td><font size="+2" color="black">Value</font></td>
			</tr>
		<xsl:for-each select="*">
			<tr>
				<td><xsl:value-of select="name()" /></td>
				<td><xsl:value-of select="." /></td>
			</tr>
		</xsl:for-each>
		</table>
		</td>
	</tr>
</table>
<br></br>
<br></br>
</xsl:if>
</xsl:for-each>
<font class="mount">
<a href="http://www.icecast.org">Icecast development team</a>
</font>
</font>
</BODY>
</HTML>
</xsl:template>
</xsl:stylesheet>
