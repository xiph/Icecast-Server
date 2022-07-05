<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" version="1.0">
    <xsl:template name="head">
        <xsl:param name="title"/>
        <head>
            <meta charset="utf-8" />
            <title><xsl:if test="$title"><xsl:value-of select="$title"/> — </xsl:if>Icecast Admin</title>

            <link rel="stylesheet" type="text/css" href="/assets/css/style.css" />

            <meta name="theme-color" content="#001826" />
            <meta name="viewport" content="width=device-width, initial-scale=1.0, user-scalable=yes" />
            <meta name="description" content="Icecast Server status page" />
        </head>
    </xsl:template>
</xsl:stylesheet>
