<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" version="1.0">
    <!-- Import include files -->
    <xsl:include href="includes/page.xsl"/>
    <xsl:include href="includes/mountnav.xsl"/>

    <xsl:variable name="title">Manage Authentication</xsl:variable>

    <xsl:template name="content">
        <div class="section">
            <h2><xsl:value-of select="$title" /></h2>
            <xsl:if test="iceresponse">
                <aside class="error">
                    <xsl:value-of select="iceresponse/message" />
                </aside>
            </xsl:if>
            <xsl:for-each select="role">
                <section class="box">
                    <h3 class="box_title">Role <code><xsl:value-of select="@name" /></code> (<code><xsl:value-of select="@type" /></code>)
                        <xsl:if test="server_name">
                            <xsl:text> </xsl:text><small><xsl:value-of select="server_name" /></small>
                        </xsl:if>
                    </h3>
                    <xsl:choose>
                        <xsl:when test="users/user">
                            <table class="table-flipscroll">
                                <thead>
                                    <tr>
                                        <th>User</th>
                                        <xsl:if test="@can-deleteuser = 'true'">
                                            <th class="actions">Action</th>
                                        </xsl:if>
                                    </tr>
                                </thead>
                                <tbody>
                                    <xsl:for-each select="users/user">
                                        <tr>
                                            <td><xsl:value-of select="username" /></td>
                                            <xsl:if test="../../@can-deleteuser = 'true'">
                                                <td class="actions">
                                                    <a class="critical" href="/admin/ui/confirmdeleteuser.xsl?id={../../@id}&amp;username={username}">Delete</a>
                                                </td>
                                            </xsl:if>
                                        </tr>
                                    </xsl:for-each>
                                </tbody>
                            </table>
                        </xsl:when>
                        <xsl:otherwise>
                            <p>No Users</p>
                        </xsl:otherwise>
                    </xsl:choose>
                    <!-- Form to add Users -->
                    <xsl:if test="@can-adduser = 'true'">
                        <h4>Add User</h4>
                        <form method="post" action="/admin/manageauth.xsl">
                            <input type="hidden" name="id" value="{@id}"/>
                            <input type="hidden" name="action" value="add"/>

                            <label for="username" class="hidden">Username: </label>
                            <input type="text" id="username" name="username" value="" placeholder="Username" required="required" />
                            &#160;
                            <label for="password" class="hidden">Password: </label>
                            <input type="password" id="password" name="password" value="" placeholder="Password" required="required" />
                            &#160;
                            <input type="submit" value="Add new user" />
                        </form>
                    </xsl:if>
                </section>
            </xsl:for-each>
        </div>
    </xsl:template>
</xsl:stylesheet>
