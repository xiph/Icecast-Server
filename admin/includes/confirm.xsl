<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" version="1.0" xmlns:xt="http://www.jclark.com/xt" extension-element-prefixes="xt" exclude-result-prefixes="xt">
    <!-- Import include files -->
    <xsl:include href="includes/page.xsl"/>

    <xsl:template name="confirm">
        <xsl:param name="text" />
        <xsl:param name="action-cancel" />
        <xsl:param name="action-confirm" />
        <xsl:param name="params-cancel" />
        <xsl:param name="params-confirm" />

        <h2><xsl:value-of select="$title" /></h2>
        <section class="box">
            <h3 class="box_title">Please confirm</h3>
            <p><xsl:copy-of select="$text" /></p>
            <ul class="boxnav">
                <li>
                    <form method="get" action="{$action-cancel}">
                        <xsl:for-each select="xt:node-set($params-cancel)/*">
                            <input type="hidden" name="{@member}" value="{@value}" />
                        </xsl:for-each>
                        <input type="submit" value="Cancel" />
                    </form>
                </li>
                <li>
                    <form method="post" action="{$action-confirm}">
                        <xsl:for-each select="xt:node-set($params-confirm)/*">
                            <input type="hidden" name="{@member}" value="{@value}" />
                        </xsl:for-each>
                        <input class="critical" type="submit" value="Confirm" />
                    </form>
                </li>
            </ul>
        </section>
    </xsl:template>
</xsl:stylesheet>
