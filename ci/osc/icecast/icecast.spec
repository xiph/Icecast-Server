#
# spec file for package icecast
#
# Parts of this file taken from original SUSE and Fedora packaging
#
%define version_archive _VERSION_ARCHIVE_
Summary: Streaming media server
Name: icecast
Version: 2.5.0-rc1
Release: 1%{?dist}
Group: Applications/Multimedia
#because one way to say this is not enough...
%if 0%{?suse_version} > 1
License: GPL-2.0
%else
License: GPLv2
%endif
URL: http://www.icecast.org/
#Source0: http://downloads.xiph.org/releases/icecast/icecast-%{version}.tar.gz
Source0: icecast2_%{version}.orig.tar.gz
Source1: icecast2_%{version}-1.debian.tar.gz 
Source2: icecast.init
Source3: icecast.logrotate
Source4: icecast.xml
Source5: icecast.init.suse

%if 0%{?suse_version} > 1
Suggests:	logrotate

PreReq:         %fillup_prereq
PreReq:         %insserv_prereq
PreReq:         /usr/sbin/groupadd
PreReq:         /usr/sbin/useradd
%endif

BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

Provides: streaming-server

BuildRequires: automake, pkgconfig
BuildRequires: libvorbis-devel >= 1.0, libogg-devel >= 1.0, curl-devel >= 7.10.0
BuildRequires: libxml2-devel, libxslt-devel, speex-devel
# To be enabled as soon as Fedora's libtheora supports ogg_stream_init
BuildRequires: libtheora-devel >= 1.0, openssl-devel >= 1.1, rhash-devel, libigloo-devel >= 0.9.2
# From suse packaging
BuildRequires: libtool,

Requires(pre): /usr/sbin/useradd
Requires(post): /sbin/chkconfig
Requires(preun): /sbin/chkconfig
Requires(preun): /sbin/service


%description
This is an official icecast.org package of Icecast.
Icecast is a streaming media server which currently supports Ogg Vorbis
and Opus audio streams, with MP3 known to work. It can be used to create
an Internet radio station or a privately running jukebox and many things
in between.  It is very versatile in that new formats can be added
relatively easily and supports open standards for commuincation
and interaction.


%prep
%setup -q -n icecast-%{version_archive}
find -name "*.html" -or -name "*.jpg" -or -name "*.png" -or -name "*.css" | xargs chmod 644
tar -xzf %{SOURCE1}
%{__sed} -i -e 's/icecast2/icecast/g' debian/icecast2.1

%build
# quick and dirty fix for update-alternatives being broken in some OBS targets
if [ ! -e /usr/bin/ld ] 
 then
 if [ -e /usr/bin/ld.gold ]
  then
    export PATH=$PATH:$HOME/bin
    mkdir $HOME/bin
    ln -s /usr/bin/ld.gold $HOME/bin/ld
  fi
fi
# LD=/usr/bin/ld.gold
%if 0%{?suse_version} > 1
autoreconf -fiv
%endif
%configure
%{__make} %{?_smp_mflags}


%install
rm -rf %{buildroot}
make install DESTDIR=%{buildroot}
%if 0%{?suse_version} > 1
rm -rf %{buildroot}%{_datadir}/doc/%{name}
# init script
install -d -m 0755 %{buildroot}%{_sbindir}
install -D -m 0755 %{SOURCE5} %{buildroot}%{_sysconfdir}/init.d/%{name}
ln -s -f %{_sysconfdir}/init.d/%{name} %{buildroot}%{_sbindir}/rc%{name}
# create missing dirs
install -d -m 0755 %{buildroot}%{_localstatedir}/{lib,log}/%{name}
%else
rm -rf %{buildroot}%{_datadir}/icecast/doc
rm -rf %{buildroot}%{_docdir}/icecast
install -D -m 755 %{SOURCE2} %{buildroot}%{_initrddir}/icecast
install -D -m 640 %{SOURCE4} %{buildroot}%{_sysconfdir}/icecast.xml
%endif
install -D -m 644 %{SOURCE3} %{buildroot}%{_sysconfdir}/logrotate.d/icecast
install -D -m 644 debian/icecast2.1 %{buildroot}%{_mandir}/man1/icecast.1
mkdir -p %{buildroot}%{_localstatedir}/log/icecast
%if 0%{?suse_version} > 1
%else
mkdir -p %{buildroot}%{_localstatedir}/run/icecast
%endif
find doc -iname "Makefile*" | xargs rm -f

%clean
%if 0%{?suse_version} > 1
[ %{buildroot} != "/" -a -d %{buildroot} ] && rm -rf %{buildroot}
%else
rm -rf %{buildroot}
%endif

%pre
%if 0%{?suse_version} > 1
/usr/sbin/groupadd   -r %{name} &>/dev/null || :
/usr/sbin/useradd -g %{name} -s /bin/false -r -c "Icecast streaming server" -d %{_localstatedir}/lib/%{name} %{name} &>/dev/null || :
%else
/usr/sbin/useradd -M -r -d /usr/share/icecast -s /sbin/nologin \
	-c "icecast streaming server" icecast > /dev/null 2>&1 || :
%endif

%post
%if 0%{?suse_version} > 1
%fillup_and_insserv %{name}
%else
/sbin/chkconfig --add icecast
%endif

%preun
%if 0%{?suse_version} > 1
%stop_on_removal %{name}
%else
if [ $1 = 0 ]; then
        /sbin/service icecast stop >/dev/null 2>&1
        /sbin/chkconfig --del icecast
fi
%endif

%postun
%if 0%{?suse_version} > 1
%restart_on_update %{name}
%insserv_cleanup
%else
if [ "$1" -ge "1" ]; then
        /sbin/service icecast condrestart >/dev/null 2>&1
fi
if [ $1 = 0 ] ; then
	userdel icecast >/dev/null 2>&1 || :
fi
%endif

%files
%defattr(-,root,root)
%doc README.md AUTHORS COPYING NEWS ChangeLog
%doc doc/
%doc conf/*.dist
%config(noreplace) %attr(-,root,%{name}) %{_sysconfdir}/icecast.xml
%{_sysconfdir}/logrotate.d/icecast
%if 0%{?suse_version} > 1
%{_sysconfdir}/init.d/%{name}
%{_localstatedir}/lib/%{name}
%{_sbindir}/rc%{name}
%config(noreplace) %attr(640,root,root) %{_sysconfdir}/logrotate.d/%{name}
%else
%{_initrddir}/icecast
%dir %attr(-,%{name},%{name}) %{_localstatedir}/run/icecast
%endif
%{_bindir}/icecast
%{_datadir}/icecast
%{_mandir}/man1/icecast.1.gz
%dir %attr(-,%{name},%{name}) %{_localstatedir}/log/icecast

%changelog
* Tue Aug 26 2025 Philipp Schafft <lion@lion.leolix.org> - 2.5.0-rc1-1
- Preparing for 2.5.0-rc1


* Sun Mar 13 2022 Philipp Schafft <lion@lion.leolix.org> - 2.4.99.3-1
- Preparing for 2.5 beta3 aka 2.4.99.3


* Fri May 11 2018 Thomas B. Ruecker <thomas@ruecker.fi> - 2.4.2.99-1
- 2.5 Beta 2

* Wed Apr 08 2015 Thomas B. Ruecker <thomas@ruecker.fi> - 2.4.2-1
- security fix for remote DoS vulnerability

* Sun Dec 14 2014 Thomas B. Ruecker <thomas@ruecker.fi> - 2.4.1-2
- Packaging fix for docdir problem, as patched upstream
- Adjusted %%doc to reflect subdirectories

* Sun Nov 23 2014 Thomas B. Ruecker <thomas@ruecker.fi> - 2.4.1-1
- Initial packaging of 2.4.1
- SECURITY FIX
- See ChangeLog for details

* Sun May 25 2014 Thomas B. Ruecker <thomas@ruecker.fi> - 2.4.0-1
- SECURITY FIX - Override supplementary groups if <changeowner>
- Added <audio> for supported streams. TNX ePirat
- status2.xsl, broken for a decade, now it's gone!
- Updated docs:
  - logging to STDERR; known issues
  - Refactored docs about client authentication
  - Vastly improved page about Icecast statistics
  - Clean up supported windows versions
  - Quick fixup of the basic setup page
  - Minor fixes to the config file documentation
  - Updated YP documentation
  - Reduced win32 documentation to essentials
- Adding stream_start_iso8601, server_start_iso8601
  ISO8601 compliante timestamps for statistics. Should make usage in
  e.g. JSON much easier.
  Added as new variables to avoid breaking backwards compatibility.
- Nicer looking tables for the admin interface.
  ePirat sent updated tables code that should look much nicer.
  This is admin interface only (and a global css change).
- Set content-type to official "application/json"
- Initial JSON status transform.
  Output roughly limited to data also visible through status.xsl.
- Silence direct calls, add partial array support.
- The XSLT will now return empty if called directly.
  This is a security measure to prevent unintended data leakage.
- Adding partial array support to print sources in an array.
  Code lifted from:
  https://code.google.com/p/xml2json-xslt/issues/detail?id=3
- Adding xml2json XSLT, svn r31 upstream trunk.
  https://code.google.com/p/xml2json-xslt/
- RPM specific changes:
  - remove status3.xsl as it was never part of the official source
  - slight change in default config, more changes later

* Sat Mar 01 2014 Thomas B. Ruecker <thomas@ruecker.fi> - 2.3.99.5-1
- Upgrade to 2.4 beta5
- Updated web interface to be fully XHTML compliant.
  Credit to ePirat
- Send charset in headers for everything, excluding file-serv and streams.
- Documentation updates
- reverted patch affecting stats interface

* Thu Jan 23 2014 Thomas B. Ruecker <thomas@ruecker.fi> - 2.3.99.4-1
- Upgrade to 2.4 beta4
- Updated web interface to be more XHTML compliant.
- Fixed a memory leak. Lost headers of stream because of wrong ref
  counter in associated refbuf objects.
- avoid memory leak in _parse_mount() when "type"-attribute is set
- Completed HTTP PUT support, send 100-continue-header,
  if client requests it. We need to adhere to HTTP1.1 here.
- corrected Date-header format to conform the standard (see RFC1123).
  Thanks to cato for reporting.
- Added a favicon to the web-root content
- We now split handling of command line arguments into two parts.
  Only the critical part of getting the config file is done first (and
  -v as it prevents startup). The rest (currently only -b) is deferred.
  It allows us to log error messages to stderr even if the -b argument
  is passed. This is mainly for the case where the logfile or TCP port
  can't be opened.

* Sat Apr 06 2013 Thomas B. Ruecker <thomas@ruecker.fi> - 2.3.99.3-1
- Upgrade to 2.4 beta3
- This release added a default mount feature

* Sun Mar 31 2013 Thomas B. Ruecker <thomas@ruecker.fi> - 2.3.99.2-1
- Experimental packaging of Icecast 2.4 beta2
