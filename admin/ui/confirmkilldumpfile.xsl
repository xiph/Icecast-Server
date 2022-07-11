<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" version="1.0" xmlns:xt="http://www.jclark.com/xt" extension-element-prefixes="xt" exclude-result-prefixes="xt">
    <!-- Import include files -->
    <xsl:include href="includes/confirm.xsl"/>
    <xsl:variable name="title">Confirm stopping dump file</xsl:variable>
    <xsl:template name="content">
        <xsl:for-each select="/report/incident">
            <xsl:variable name="get-parameters" select="resource[@type='parameter']/value[@member='get-parameters']" />
            <xsl:variable name="mount" select="$get-parameters/value[@member='mount']" />
            <xsl:call-template name="confirm">
                <xsl:with-param name="text">Please confirm stopping the dumpfile for <code><xsl:value-of select="$mount/@value" /></code>.</xsl:with-param>
                <xsl:with-param name="action-cancel">/admin/streamlist.xsl</xsl:with-param>
                <xsl:with-param name="action-confirm">/admin/dumpfilecontrol.xsl</xsl:with-param>
                <xsl:with-param name="params-cancel">
                    <xsl:copy-of select="$mount" />
                </xsl:with-param>
                <xsl:with-param name="params-confirm">
                    <xsl:copy-of select="$mount" />
                    <value member="action" value="kill" />
                </xsl:with-param>
            </xsl:call-template>
        </xsl:for-each>
    </xsl:template>
</xsl:stylesheet>

