Icecast 2 - README
---------------------------------------------------------------------

Icecast is a streaming media server which currently supports _WebM_ and
_Ogg_ streaming including the _Opus_, _Vorbis_ and _Theora_ codecs.
Also Icecast can handle other streams like MP3/AAC/NSV
in legacy mode, but this is not officially supported.

It can be used to create an Internet radio station or a privately
running jukebox and many things in between. It is very versatile in
that new formats can be added relatively easily and supports open
standards for communication and interaction.

Icecast is distributed under the GNU GPL, version 2. A copy of this
license is included with this software in the COPYING file.

The name of this software is spelled __"Icecast"__ with a leading capital 'I' but with a lower case 'c'. Any other spelling is _incorrect_.

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
To build Icecast on a Unix platform, perform the following steps:

Run

    ./configure
    make
    make install  # as root

This is the typical procedure if you download the tar file.  If you retrive
the code from Git or want to rebuild the configure then run `./autogen.sh`
instead of configure above. Most people do not need to run autogen.sh

A sample config file will be placed in `/usr/local/etc` (on UNIX, 
also depends on path PREFIX) or in the current working directory 
(on Win32) and is called `icecast.xml`

Documentation for Icecast is available in the doc directory, by 
viewing `doc/index.html` in a browser. It's also installed to 
`$PREFIX/share/doc/icecast/`. Online documentation can be found 
on the [Icecast Website][5].

If you have problems with setting up Icecast, please join the 
[Icecast mailing list][6] and then email icecast@xiph.org.
In case you have patches or want to discuss development issues,
please join the [Icecast developer mailing list][7] and then
email icecast-dev@xiph.org.
Or come and see us on irc.freenode.net, channel [#icecast][8]
(please be patient, people are not always at their computers).

[1]: http://xmlsoft.org/downloads.html
[2]: http://xmlsoft.org/XSLT/downloads.html
[3]: http://curl.haxx.se/download.html
[4]: http://www.vorbis.com/files
[5]: http://icecast.org/docs/
[6]: http://lists.xiph.org/mailman/listinfo/icecast
[7]: http://lists.xiph.org/mailman/listinfo/icecast-dev
[8]: https://webchat.freenode.net/?channels=#icecast
