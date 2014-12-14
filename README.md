Icecast 2 - README
---------------------------------------------------------------------

Icecast is a streaming media server which currently supports WebM and
Ogg streaming including the Opus, Vorbis and Theora codecs. 
Also Icecast can handle other streams like MP3/AAC/NSV 
in legacy mode, but this is not officially supported.

It can be used to create an Internet radio station or a privately
running jukebox and many things in between. It is very versatile in
that new formats can be added relatively easily and supports open
standards for communication and interaction.

Icecast is distributed under the GNU GPL, version 2. A copy of this
license is included with this software in the COPYING file.

Prerequisites
---------------------------------------------------------------------
Icecast requires the following packages:

-   [libxml2][1]
-   [libxslt][2]
-   [curl][3] (>= version 7.10 required)
-   [ogg/vorbis][4] (>= version 1.0 required)

__NOTE__: Icecast may be compiled without curl, however this will
disable Stream Directory server interaction (YP) and URL based 
authentication.

A note about prerequisite packages
---------------------------------------------------------------------
Most distributions have some sort of package management repository for
pre-built packages (eg rpm, deb etc).  These setups often have a runtime
package, which is usually installed for you by default, and enables you
to run applications that depend on them.  However if you are building
Icecast from source then the runtime system is not enough. You will also
need a development package named something like libxslt-devel

Build/Install
---------------------------------------------------------------------
To build Icecast on a Unix platform, perform the following:

Run

    ./configure
    make
    make install

This is the typical procedure if you download the tar file.  If you retrive
the code from Git or want to rebuild the configure then run the `autogen.sh`
instead of the configure above. Most people do not need to run autogen.sh

A sample config file will be placed in `/usr/local/etc` (on UNIX) or in 
the current working directory (on Win32) and is called `icecast.xml`

Documentation for Icecast is available in the doc directory, by 
viewing `doc/index.html` in a browser.
Online documentation can be found on the [Icecast Website][5].

Please email us at icecast@xiph.org or icecast-dev@xiph.org, or come and see
us at irc.freenode.net, channel [#icecast][6], if you have any troubles.

[1]: http://xmlsoft.org/downloads.html
[2]: http://xmlsoft.org/XSLT/downloads.html
[3]: http://curl.haxx.se/download.html
[4]: http://www.vorbis.com/files
[5]: http://icecast.org/docs/
[6]: https://webchat.freenode.net/?channels=#icecast
