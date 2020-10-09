<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" version="1.0">
    <xsl:include href="includes/web-page.xsl"/>
    <xsl:variable name="title">Authorization Page</xsl:variable>
    <xsl:template name="content">
        <xsl:for-each select="source">
            <xsl:choose>
                <xsl:when test="listeners">
                    <xsl:if test="authenticator">
                        <div class="roundbox">
                            <h3 class="mount">
                                Mount Point <xsl:value-of select="@mount" />
                                <xsl:if test="server_name">
                                    <small><xsl:value-of select="server_name" /></small>
                                </xsl:if>
                            </h3>
                            <form class="alignedform" method="get" action="/admin/buildm3u">
                                <p>
                                    <label for="name">Username: </label>
                                    <input id="name" name="username" type="text" />
                                </p>
                                <p>
                                    <label for="password">Password: </label>
                                    <input id="password" name="password" type="password" />
                                </p>
                                <input type="hidden" name="mount" value="{@mount}" />
                                <input type="submit" value="Login" />
                            </form>
                        </div>
                    </xsl:if>
                </xsl:when>
                <xsl:otherwise>
                    <h3><xsl:value-of select="@mount" /> - Not Connected</h3>
                </xsl:otherwise>
            </xsl:choose>
        </xsl:for-each>
    </xsl:template>
</xsl:stylesheet>
