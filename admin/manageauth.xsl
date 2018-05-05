<xsl:stylesheet xmlns:xsl = "http://www.w3.org/1999/XSL/Transform" version = "1.0">
	<xsl:output method="html" doctype-system="about:legacy-compat" encoding="UTF-8" />
	<!-- Import include files -->
	<xsl:include href="includes/page.xsl"/>
	<xsl:include href="includes/mountnav.xsl"/>

	<xsl:variable name="title">Stats</xsl:variable>

	<xsl:template name="content">
				<div class="section">
					<h2>Manage Authentication</h2>
					<xsl:if test="iceresponse">
						<div class="aside error">
							<xsl:value-of select="iceresponse/message" />
						</div>
					</xsl:if>
					<xsl:for-each select="role">
						<div class="article">
							<h3>Role <xsl:value-of select="@name" /> (<xsl:value-of select="@type" />)
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
													<th>Action</th>
												</xsl:if>
											</tr>
										</thead>
										<tbody>
											<xsl:for-each select="users/user">
												<tr>
													<td><xsl:value-of select="username" /></td>
													<xsl:if test="../../@can-deleteuser = 'true'">
														<td>
															<a href="manageauth.xsl?id={../../@id}&amp;username={username}&amp;action=delete">Delete</a>
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
								<form method="get" action="manageauth.xsl">
									<label for="username" class="hidden">Username</label>
									<input type="text" id="username" name="username" value="" placeholder="Username" required="required" />
									<label for="password" class="hidden">Password</label>
									<input type="password" id="password" name="password" value="" placeholder="Password" required="required" />
									<input type="hidden" name="id" value="{@id}"/>
									<input type="hidden" name="action" value="add"/>
									<input type="submit" value="Add new user" />
								</form>
							</xsl:if>
						</div>
					</xsl:for-each>
				</div>
	</xsl:template>
</xsl:stylesheet>
