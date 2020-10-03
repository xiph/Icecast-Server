<xsl:stylesheet xmlns:xsl = "http://www.w3.org/1999/XSL/Transform" version = "1.0">
	<xsl:output method="html" doctype-system="about:legacy-compat" encoding="UTF-8" />
	<!-- Import include files -->
	<xsl:include href="includes/page.xsl"/>
	<xsl:include href="includes/mountnav.xsl"/>

	<xsl:variable name="title">Set fallback</xsl:variable>

	<xsl:template name="content">
				<div class="section">
					<h2><xsl:value-of select="$title" /></h2>
					<section class="box">
						<h3 class="box_title">Mountpoint <code><xsl:value-of select="@mount" /></code></h3>
						<!-- Mount nav -->
						<xsl:call-template name="mountnav">
							<xsl:with-param name="mount" select="current_source"/>
						</xsl:call-template>
						<xsl:choose>
							<xsl:when test="source">
								<p>Choose the mountpoint to which you want listeners to fallback to:</p>
								<form method="post" action="/admin/fallbacks.xsl">
									<label for="fallback" class="hidden">
										Set fallback for <code><xsl:value-of select="current_source" /></code> to
									</label>
									<select name="fallback" id="fallback">
										<xsl:for-each select="source">
											<option value="{@mount}">
												<xsl:value-of select="@mount" />
											</option>
										</xsl:for-each>
									</select>
									<input type="hidden" name="mount" value="{current_source}" />
									<input type="hidden" name="omode" value="strict" />
									&#160;
									<input type="submit" value="Set fallback" />
								</form>
							</xsl:when>
							<xsl:otherwise>
								<aside class="warning">
									<strong>No mounts!</strong> 
									There are no other mountpoints you could set as fallback.
								</aside>
							</xsl:otherwise>
						</xsl:choose>
					</section>
				</div>
	</xsl:template>
</xsl:stylesheet>
