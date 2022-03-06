<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" version="1.0">
    <xsl:include href="includes/page.xsl"/>
    <xsl:include href="includes/authlist.xsl"/>
    <xsl:variable name="title">Listen Sockets</xsl:variable>

    <xsl:template name="content">
        <h2><xsl:value-of select="$title" /></h2>
        <xsl:for-each select="/report/incident">
            <section class="box">
                <h3 class="box_title">Listen Socket <code><xsl:value-of select="resource/value[@member='id']/@value" /></code></h3>
                <h4>Overview</h4>
                <table class="table-block">
                    <thead>
                        <tr>
                            <th>Key</th>
                            <th>Value</th>
                        </tr>
                    </thead>
                    <tbody>
                        <xsl:if test="resource/value[@member='id']/@state = 'set'">
                            <tr>
                                <td>ID</td>
                                <td><xsl:value-of select="resource/value[@member='id']/@value" /></td>
                            </tr>
                        </xsl:if>
                        <xsl:if test="resource/value[@member='on_behalf_of']/@state = 'set'">
                        <tr>
                            <td>On behalf of</td>
                            <td><xsl:value-of select="resource/value[@member='on_behalf_of']/@value" /></td>
                        </tr>
                        </xsl:if>
                        <tr>
                            <td>Type</td>
                            <td><xsl:value-of select="resource/value[@member='type']/@value" /></td>
                        </tr>
                        <tr>
                            <td>Family</td>
                            <td><xsl:value-of select="resource/value[@member='family']/@value" /></td>
                        </tr>
                    </tbody>
                </table>

                <h4>Config</h4>
                <table class="table-block">
                    <thead>
                        <tr>
                            <th>Key</th>
                            <th>Value</th>
                        </tr>
                    </thead>
                    <tbody>
                        <xsl:for-each select="resource/value[@member='config']/value">
                            <xsl:if test="@state != 'unset'">
                            <tr>
                                <td><xsl:value-of select="@member" /></td>
                                <td><xsl:value-of select="@value" /></td>
                            </tr>
                            </xsl:if>
                        </xsl:for-each>
                    </tbody>
                </table>

                <xsl:if test="resource/value[@member='headers']/value">
                    <h4>Header</h4>
                    <table class="table-block">
                        <thead>
                            <tr>
                                <th>Type</th>
                                <th>Name</th>
                                <th>Value</th>
                                <th>Status</th>
                            </tr>
                        </thead>
                        <tbody>
                            <xsl:for-each select="resource/value[@member='headers']/value">
                                <tr>
                                    <td><xsl:value-of select="value[@member='type']/@value" /></td>
                                    <td><xsl:value-of select="value[@member='name']/@value" /></td>
                                    <td><xsl:value-of select="value[@member='value']/@value" /></td>
                                    <td><xsl:value-of select="value[@member='status']/@value" /></td>
                                </tr>
                            </xsl:for-each>
                        </tbody>
                    </table>
                </xsl:if>

                <xsl:for-each select="resource/extension/icestats">
                    <h4>Authentication</h4>
                    <xsl:call-template name="authlist" />
                </xsl:for-each>
            </section>
        </xsl:for-each>
    </xsl:template>
</xsl:stylesheet>
