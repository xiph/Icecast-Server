# Icecast 2.4.1 Documentation

Icecast is a streaming media server which currently supports Ogg Vorbis and MP3 audio streams.
It can be used to create an Internet radio station or a privately running jukebox and many
things in between. It is very versatile in that new formats can be added relatively easily
and supports open standards for commuincation and interaction.

Icecast is distributed under the GNU GPL, version 2. A copy of this license is included with
this software in the COPYING file.

There are two major parts to most streaming media servers: The component providing the
content (what we call source clients) and the component which is responsible for serving that
content to listeners (this is the function of Icecast).

# Prerequisites

Icecast requires the following packages:

* [libxml2](http://xmlsoft.org/downloads.html)
* [libxslt](http://xmlsoft.org/XSLT/downloads.html)
* [curl](http://curl.haxx.se/download.html) Version >= 7.10
* [libogg/libvorbis](http://www.vorbis.com/files) Version >= 1.0
* [OpenSSL](https://www.openssl.org/source/) (Optional, enable if SSL support is desired)

# What platforms are supported?

Currently the following Unix platforms are supported:

-	Linux (Most flavors including Redhat and Debian)
-	FreeBSD
-	OpenBSD
-	Solaris

Currently the following Windows platforms are supported:

-	Windows Vista
-	Windows 7
-	Windows 8
-	Windows Server 2003
-	Windows Server 2008
-	Windows Server 2012

# Build/Install

To build Icecast on a Unix platform, perform the following:

Run

    ./configure
    make
    make install

to build and install this release.

A sample config file will be placed in `/usr/local/etc` (on UNIX) or in the current working
directory (on Win32) and is called `icecast.xml`.

Documentation for Icecast is available in the doc directory, by viewing `doc/index.html` in a
browser.

# Where do I go for questions?

There are many ways to contact the icecast development team, best ways are:

-  The [Icecast Mailing list](http://lists.xiph.org/mailman/listinfo/icecast)
-  The [Icecast Developer Mailing list](http://lists.xiph.org/mailman/listinfo/icecast-dev), for more development-related questions.
-  Icecast IRC Chat at the [#icecast](irc://irc.freenode.net:6667/#icecast) channel on irc.freenode.net