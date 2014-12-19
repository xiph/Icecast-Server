<xsl:stylesheet xmlns:xsl = "http://www.w3.org/1999/XSL/Transform" version = "1.0" >
<xsl:output omit-xml-declaration="no" method="xml" doctype-public="-//W3C//DTD XHTML 1.0 Strict//EN" doctype-system="http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd" indent="yes" encoding="UTF-8" />
<xsl:template match = "/icestats" >
<html xmlns="http://www.w3.org/1999/xhtml">
<head>
	<title>Icecast Streaming Media Server</title>
	<link rel="stylesheet" type="text/css" href="/style.css" />
	<meta name="viewport" content="width=device-width, initial-scale=1.0, user-scalable=yes" />
</head>
<body>
	<h1>Icecast2 Admin</h1>
	<!--index header menu -->
	<div id="menu">
		<ul>
			<li><a href="stats.xsl">Admin Home</a></li>
			<li><a href="listmounts.xsl">Mountpoint List</a></li>
			<li><a href="/status.xsl">Public Home</a></li>
		</ul>
	</div>
	<!--end index header menu -->
	<h2>Manage Authentication</h2>
	<xsl:if test="iceresponse">
		<div class="roundbox">
			<h3>Message</h3>
			<xsl:for-each select="iceresponse">
				<xsl:value-of select="message" /><br />
			</xsl:for-each>
		</div>
	</xsl:if>
	<xsl:for-each select="role">
		<div class="roundbox">
			<h3>Role <xsl:value-of select="@name" /> (<xsl:value-of select="@type" />)
				<xsl:if test="server_name">
					<small><xsl:value-of select="server_name" /></small>
				</xsl:if>
			</h3>
			<ul class="nav">
				<li><a href="listclients.xsl?mount={@mount}">List Clients</a></li>
				<li><a href="moveclients.xsl?mount={@mount}">Move Listeners</a></li>
				<li><a href="updatemetadata.xsl?mount={@mount}">Update Metadata</a></li>
				<li><a href="manageauth.xsl?mount={@mount}">Manage Authentication</a></li>
				<li><a href="killsource.xsl?mount={@mount}">Kill Source</a></li>
			</ul>
			<xsl:if test="users">
				<table class="colortable">
					<thead>
						<tr>
							<td>User</td>
							<td>Action</td>
						</tr>
					</thead>
					<tbody>
						<xsl:for-each select="users/user">
							<tr>
								<td>
									<xsl:value-of select="username" />
								</td>
								<td>
                                					<xsl:choose>
                                        					<xsl:when test="../../@can-deleteuser = 'true'">
											<a href="manageauth.xsl?id={../../@id}&amp;username={username}&amp;action=delete">Delete</a>
										</xsl:when>
										<xsl:otherwise>
											<xsl:text disable-output-escaping="yes">&amp;nbsp;</xsl:text>
										</xsl:otherwise>
									</xsl:choose>
								</td>
							</tr>
						</xsl:for-each>
					</tbody>
				</table>
			</xsl:if>
			<xsl:if test="@can-adduser = 'true'">
				<form class="alignedform" method="get" action="/admin/manageauth.xsl">
					<fieldset>
						<legend>Add new user</legend>
						<p>
							<label for="username">Username:</label>
							<input type="text" id="username" name="username"/>
						</p>
						<p>
							<label for="password">Password:</label>
							<input type="password" id="password" name="password"/>
						</p>
						<input type="hidden" name="id" value="{@id}"/>
						<input type="hidden" name="action" value="add"/>
						<input type="Submit" value="Add"/>
					</fieldset>
				</form>
			</xsl:if>
		</div>
	</xsl:for-each>
	<div id="footer">
		Support icecast development at <a href="http://www.icecast.org">www.icecast.org</a>
	</div>
</body>
</html>
</xsl:template>
</xsl:stylesheet>
