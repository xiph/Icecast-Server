<?xml version="1.0" encoding="ISO-8859-1"?>
<xsl:stylesheet version="1.0" xmlns:xsl="http://www.w3.org/1999/XSL/Transform">
<xsl:output method="xml" version="1.0" encoding="iso-8859-1" indent="yes"/>
<xsl:template match = "/icestats" >

	<xsl:for-each select="source">
		<SHOUTCASTSERVER>
			<CURRENTLISTENERS><xsl:value-of select="listeners" /></CURRENTLISTENERS>
			<PEAKLISTENERS><xsl:value-of select="listener_peak" /></PEAKLISTENERS>
			<MAXLISTENERS><xsl:value-of select="max_listeners" /></MAXLISTENERS>
			<REPORTEDLISTENERS>NA</REPORTEDLISTENERS>
			<AVERAGETIME>NA</AVERAGETIME>
			<SERVERGENRE><xsl:value-of select="genre" /></SERVERGENRE>
			<SERVERURL><xsl:value-of select="server_url" /></SERVERURL>
			<SERVERTITLE><xsl:value-of select="server_name" /></SERVERTITLE>
			<SONGTITLE><xsl:if test="artist"><xsl:value-of select="artist" /> - </xsl:if><xsl:value-of select="title" /></SONGTITLE>
			<SONGURL><xsl:value-of select="listenurl" /></SONGURL>
			<IRC>NA</IRC>
			<ICQ>NA</ICQ>
			<AIM>NA</AIM>
			<WEBHITS>NA</WEBHITS>
			<STREAMHITS>NA</STREAMHITS>
			<STREAMSTATUS>NA</STREAMSTATUS>
			<BITRATE><xsl:value-of select="bitrate" /></BITRATE>
			<CONTENT><xsl:value-of select="server_type" /></CONTENT>
			<VERSION><xsl:value-of select="server_id" /></VERSION>
			
			<WEBDATA>
				<INDEX>NA</INDEX>
				<LISTEN>NA</LISTEN>
				<PALM7>NA</PALM7>
				<LOGIN>NA</LOGIN>
				<LOGINFAIL>NA</LOGINFAIL>
				<PLAYED>NA</PLAYED>
				<COOKIE>NA</COOKIE>
				<ADMIN>NA</ADMIN>
				<UPDINFO>NA</UPDINFO>
				<KICKSRC>NA</KICKSRC>
				<KICKDST>NA</KICKDST>
				<UNBANDST>NA</UNBANDST>
				<BANDST>NA</BANDST>
				<VIEWBAN>NA</VIEWBAN>
				<UNRIPDST>NA</UNRIPDST>
				<RIPDST>NA</RIPDST>
				<VIEWRIP>NA</VIEWRIP>
				<VIEWXML>NA</VIEWXML>
				<VIEWLOG>NA</VIEWLOG>
				<INVALID>NA</INVALID>
			</WEBDATA>
			
			<LISTENERS>
				<xsl:for-each select="listener">
				<LISTENER>
					<HOSTNAME><xsl:value-of select="IP" /><xsl:if test="username"> (<xsl:value-of select="username" />)</xsl:if></HOSTNAME>
					<USERAGENT><xsl:value-of select="UserAgent" /></USERAGENT>
					<UNDERRUNS>NA</UNDERRUNS>
					<CONNECTTIME><xsl:value-of select="Connected" /></CONNECTTIME>
					<POINTER>NA</POINTER>
					<UID>NA</UID>
				</LISTENER>
				</xsl:for-each>
			</LISTENERS>
		
			
			<SONGHISTORY>
				<SONG>
					<PLAYEDAT>1259797160</PLAYEDAT>
					<TITLE>Little Texas - She&#x27;s Got Her Daddy&#x27;s Money</TITLE>
				</SONG>
			</SONGHISTORY>
		</SHOUTCASTSERVER>
	</xsl:for-each>

</xsl:template>
</xsl:stylesheet>