<xsl:stylesheet xmlns:xsl = "http://www.w3.org/1999/XSL/Transform" version = "1.0" >
<xsl:output method="text" media-type="text/plain" indent="yes" encoding="UTF-8" />
<xsl:template match = "/icestats" >
<pre>
Global,Clients:<xsl:value-of select="connections" />,Sources:<xsl:value-of select="source_client_connections" />,,<xsl:value-of select="listeners" />,,
MountPoint,Connections,Stream Name,Current Listeners,Description,Currently Playing,Stream URL
<xsl:for-each select="source">
<xsl:value-of select="@mount" />,<xsl:value-of select="listener_connections" />,<xsl:value-of select="server_name" />,<xsl:value-of select="listeners" />,<xsl:value-of select="server_description" />,<xsl:value-of select="artist" /> <xsl:value-of select="title" />,<xsl:value-of select="listenurl" />
</xsl:for-each>
</pre>
</xsl:template>
</xsl:stylesheet>
