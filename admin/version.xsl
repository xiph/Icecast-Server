<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" version="1.0">
    <xsl:include href="includes/page.xsl"/>
    <xsl:variable name="title">Version</xsl:variable>

    <xsl:template name="valuetable">
        <table class="table-block">
            <thead>
                <tr>
                    <th>Key</th>
                    <th>Value</th>
                </tr>
            </thead>
            <tbody>
                <xsl:for-each select="value">
                    <xsl:if test="not(*)">
                        <tr>
                            <td><xsl:value-of select="@member" /></td>
                            <td><xsl:value-of select="@value" /></td>
                        </tr>
                    </xsl:if>
                </xsl:for-each>
            </tbody>
        </table>
    </xsl:template>

    <xsl:template name="flagslist">
        <ul>
            <xsl:for-each select="value[@type='flag']">
                <xsl:if test="@value = 'true'">
                    <li><xsl:value-of select="@member" /></li>
                </xsl:if>
            </xsl:for-each>
        </ul>
    </xsl:template>

    <xsl:template name="content">
        <h2><xsl:value-of select="$title" /></h2>
        <xsl:for-each select="/report/incident">
            <section class="box">
                <h3 class="box_title">Details</h3>
                <h4>Overview</h4>
                <xsl:for-each select="resource[@type='result']">
                    <xsl:call-template name="valuetable" />

                    <xsl:for-each select="value[@member='uname']">
                        <h4>uname</h4>
                        <xsl:call-template name="valuetable" />
                    </xsl:for-each>

                    <xsl:for-each select="value[@member='config']">
                        <h4>Configuration</h4>
                        <xsl:call-template name="valuetable" />
                    </xsl:for-each>

                    <xsl:for-each select="value[@member='dependencies']">
                        <h4>Dependencies</h4>
                        <table class="table-block">
                            <thead>
                                <tr>
                                    <th>Dependency</th>
                                    <th>Compile time version</th>
                                    <th>Runtime time version</th>
                                </tr>
                            </thead>
                            <tbody>
                                <xsl:for-each select="value[@type='structure']">
                                    <tr>
                                        <td><xsl:value-of select="@member" /></td>
                                        <td><xsl:value-of select="value[@member='compiletime']/@value" /></td>
                                        <td><xsl:value-of select="value[@member='runtime']/@value" /></td>
                                    </tr>
                                </xsl:for-each>
                            </tbody>
                        </table>
                    </xsl:for-each>

                    <xsl:for-each select="value[@member='flags']">
                        <h4>Flags</h4>

                        <xsl:for-each select="value[@member='compiletime']">
                            <h5>Compile time</h5>
                            <xsl:call-template name="flagslist" />
                        </xsl:for-each>

                        <xsl:for-each select="value[@member='runtime']">
                            <h5>Runtime time</h5>
                            <xsl:call-template name="flagslist" />
                        </xsl:for-each>
                    </xsl:for-each>
                </xsl:for-each>
            </section>

            <section class="box">
                <h3 class="box_title">Summary for reporting</h3>
                <xsl:for-each select="resource[@type='result']">
                    <pre>
<xsl:for-each select="value[@type!='structure']"><xsl:value-of select="@member" />: <xsl:value-of select="@value" /><xsl:text>
</xsl:text></xsl:for-each>
<xsl:for-each select="value[@member='uname' and @state='set']/value">uname: <xsl:value-of select="@member" />: <xsl:value-of select="@value" /><xsl:text>
</xsl:text></xsl:for-each>
<xsl:for-each select="value[@member='config' and @state='set']/value">config: <xsl:value-of select="@member" />: <xsl:value-of select="@value" /><xsl:text>
</xsl:text></xsl:for-each>
<xsl:for-each select="value[@member='dependencies' and @state='set']/value">dependency: <xsl:value-of select="@member" />: <xsl:for-each select="value"><xsl:if test="@state = 'set'">[<xsl:value-of select="@member" />] <xsl:value-of select="@value" /><xsl:text> </xsl:text></xsl:if></xsl:for-each><xsl:text>
</xsl:text></xsl:for-each>
<xsl:for-each select="value[@member='flags' and @state='set']/value">flags: <xsl:value-of select="@member" />: <xsl:for-each select="value[@type='flag']"><xsl:if test="@value = 'true'"><xsl:value-of select="@member" /><xsl:text> </xsl:text></xsl:if></xsl:for-each><xsl:text>
</xsl:text></xsl:for-each>
                    </pre>
                </xsl:for-each>
            </section>
        </xsl:for-each>
    </xsl:template>
</xsl:stylesheet>
