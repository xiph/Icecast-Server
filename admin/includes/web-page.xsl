<xsl:stylesheet xmlns:xsl = "http://www.w3.org/1999/XSL/Transform" version = "1.0" xmlns="http://www.w3.org/1999/xhtml">
    <xsl:output omit-xml-declaration="no" method="xml" doctype-public="-//W3C//DTD XHTML 1.0 Strict//EN" doctype-system="http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd" indent="yes" encoding="UTF-8" />
    <!-- Import include files -->

    <xsl:template match="/node()">
        <html>
            <head>
                <meta charset="utf-8" />
                <title><xsl:value-of select="$title"/> — Icecast Server</title>

                <link rel="stylesheet" type="text/css" href="/assets/css/style.css" media="screen, print" />

                <meta name="theme-color" content="#001826" />
                <meta name="viewport" content="width=device-width, initial-scale=1.0, user-scalable=yes" />
                <meta name="description" content="Icecast is free server software for streaming multimedia." />
            </head>
            <body>
                <!-- Header and menu -->
                <header>
                    <nav id="main-nav" role="primary">
                        <a href="/" id="branding">
                            <img src="/assets/img/icecast.png" alt="Logo" />
                            <h1>Icecast Server</h1>
                        </a>
                        <ul>
                            <li><a href="/status.xsl">Status</a></li>
                            <li><a href="/server_version.xsl">Version</a></li>
                            <li class="right adminlink"><a href="/admin/">Administration</a></li>
                        </ul>
                    </nav>
                </header>

                <!--<h1 id="header">Icecast <xsl:value-of select="$title"/></h1>-->

		<!-- Content -->
		<main role="main">
			<xsl:call-template name="content" namespace="http://www.w3.org/1999/xhtml" />
		</main>

		<!-- Footer -->
		<footer>
			<p>Support icecast development at <a href="http://icecast.org">icecast.org</a></p>
		</footer>
            </body>
        </html>
    </xsl:template>
</xsl:stylesheet>
