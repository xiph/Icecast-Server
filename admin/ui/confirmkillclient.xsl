<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" version="1.0" xmlns:xt="http://www.jclark.com/xt" extension-element-prefixes="xt" exclude-result-prefixes="xt">
    <!-- Import include files -->
    <xsl:include href="includes/confirm.xsl"/>
    <xsl:variable name="title">Confirm killing client</xsl:variable>
    <xsl:template name="content">
        <xsl:for-each select="/report/incident">
            <xsl:variable name="get-parameters" select="resource[@type='parameter']/value[@member='get-parameters']" />
            <xsl:variable name="mount" select="$get-parameters/value[@member='mount']" />
            <xsl:call-template name="confirm">
                <xsl:with-param name="text">Please confirm killing the client <code><xsl:value-of select="$get-parameters/value[@member='id']/@value" /></code> on <code><xsl:value-of select="$mount/@value" /></code>.</xsl:with-param>
                <xsl:with-param name="action-cancel">/admin/listclients.xsl</xsl:with-param>
                <xsl:with-param name="action-confirm">/admin/killclient.xsl</xsl:with-param>
                <xsl:with-param name="params-cancel">
                    <xsl:copy-of select="$mount" />
                </xsl:with-param>
                <xsl:with-param name="params-confirm">
                    <xsl:copy-of select="$mount" />
                    <xsl:copy-of select="$get-parameters/value[@member='id']" />
                </xsl:with-param>
            </xsl:call-template>
        </xsl:for-each>
    </xsl:template>
</xsl:stylesheet>
