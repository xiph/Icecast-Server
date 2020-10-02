<xsl:stylesheet xmlns:xsl = "http://www.w3.org/1999/XSL/Transform" version = "1.0">
	<xsl:output method="html" doctype-system="about:legacy-compat" encoding="UTF-8" indent="yes" />
	<!-- Import include files -->
	<xsl:include href="includes/page.xsl"/>
	<xsl:include href="includes/mountnav.xsl"/>

	<xsl:variable name="title">Update Metadata</xsl:variable>

	<xsl:template name="content">
				<div class="section">
					<h2><xsl:value-of select="$title" /></h2>

					<xsl:for-each select="source">
						<section class="box">
							<h3 class="box_title">Mountpoint <code><xsl:value-of select="@mount" /></code></h3>
							<!-- Mount nav -->
							<xsl:call-template name="mountnav" />
							<h4>Update Metadata</h4>
							<xsl:if test="content-type and not((content-type = 'application/mpeg') or (content-type = 'audio/aac') or (content-type = 'audio/aacp'))">
								<aside class="warning">
									<strong>Warning</strong>
									This is only supported for legacy codecs using ICY as transport such as MP3 and AAC.
								</aside>
							</xsl:if>
							<form method="post" action="/admin/metadata.xsl">
								<input type="hidden" name="mount" value="{@mount}" />
								<input type="hidden" name="mode" value="updinfo" />
								<input type="hidden" name="charset" value="UTF-8" />

								<label for="metadata" class="hidden">Metadata:</label>
								<input type="text" id="metadata" name="song" value="" placeholder="Click to edit" required="required" />
								&#160;
								<input type="submit" value="Update Metadata" />
							</form>
						</section>
					</xsl:for-each>

				</div>
	</xsl:template>
</xsl:stylesheet>
