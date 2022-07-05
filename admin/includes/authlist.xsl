<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" version="1.0">
    <xsl:template name="authlist">
        <ul>
            <xsl:for-each select="authentication/role">
                <li>Role
                    <xsl:if test="@name">
                        <xsl:value-of select="@name" />
                    </xsl:if>
                    of type <xsl:value-of select="@type" />
                    <xsl:if test="@management-url">
                        <xsl:choose>
                            <xsl:when test="@can-adduser='true' or @can-deleteuser='true'">
                                (<a href="{@management-url}">Manage</a>)
                            </xsl:when>
                            <xsl:when test="@can-listuser='true'">
                                (<a href="{@management-url}">List</a>)
                            </xsl:when>
                        </xsl:choose>
                    </xsl:if>
                </li>
            </xsl:for-each>
        </ul>
    </xsl:template>
</xsl:stylesheet>
