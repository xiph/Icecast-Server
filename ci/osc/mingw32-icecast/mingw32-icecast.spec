#
# spec file for package icecast
#
# Parts of this file taken from original SUSE and Fedora packaging
#

%define version_archive _VERSION_ARCHIVE_
Summary:        MinGW Windows port of Icecast streaming media server 
Name:           mingw32-icecast
Version:        2.4.99.2
Release:        2%{?dist}
Group:          Applications/Multimedia
License:        GPL-2.0
URL:            http://www.icecast.org/
#Source0:       http://downloads.xiph.org/releases/icecast/icecast-%{version}.tar.gz
Source0:        icecast2_%{version}.orig.tar.gz
#Source1:        icecast.bat
BuildRequires:  mingw32-cross-binutils
BuildRequires:  mingw32-cross-gcc
BuildRequires:  mingw32-cross-pkg-config
BuildRequires:  mingw32-filesystem >= 35
BuildRequires:  automake
BuildRequires:  libtool
BuildRequires:  mingw32-libvorbis-devel >= 1.0
BuildRequires:  mingw32-libogg-devel >= 1.0
BuildRequires:  mingw32-libcurl-devel
BuildRequires:  mingw32-libxml2-devel
BuildRequires:  mingw32-libxslt-devel
BuildRequires:  mingw32-libspeex-devel
BuildRequires:  mingw32-libtheora-devel >= 1.0
BuildRequires:  mingw32-libopenssl-devel
%_mingw32_package_header_debug
BuildArch:      noarch

BuildRoot:      %{_tmppath}/%{name}-%{version}-build

Provides: streaming-server


%description
This is an official icecast.org package of Icecast for MinGW Windows.
Icecast is a streaming media server which currently supports Ogg Vorbis
and Opus audio streams, with MP3 known to work. It can be used to create
an Internet radio station or a privately running jukebox and many things
in between.  It is very versatile in that new formats can be added
relatively easily and supports open standards for commuincation
and interaction.

%_mingw32_debug_package

%prep
%setup -q -n icecast-%{version_archive}
find -name "*.html" -or -name "*.jpg" -or -name "*.png" -or -name "*.css" | xargs chmod 644

%build
echo "lt_cv_deplibs_check_method='pass_all'" >>%{_mingw32_cache}
#PATH="%{_mingw32_bindir}:$PATH" ./autogen.sh
MINGW32_CFLAGS="%{_mingw32_cflags}" \
PATH="%{_mingw32_bindir}:$PATH" \
%{_mingw32_configure} \
  --with-curl=%{_mingw32_prefix} \
  --disable-static --enable-shared #\
%{__make} %{?_smp_mflags}


%install
make DESTDIR=%{buildroot} install %{?_smp_mflags}
rm %{buildroot}%{_mingw32_sysconfdir}/icecast.xml
cp -rfvp win32 %{buildroot}%{_mingw32_datadir}/icecast/win32

%clean
#probably redundant?
rm -rf %{buildroot}

%files
%defattr(-,root,root)
%{_mingw32_bindir}/icecast.exe
%{_mingw32_datadir}/icecast
%{_mingw32_datadir}/doc/icecast
#%%{_mingw32_sysconfdir}/icecast.xml

%changelog
* Sun Mar 06 2022 Stephan Jauernick <info@stephan-jauernick.de> - 2.4.99.2

Work in Progress rebuilding Icecast OBS CI

* Sun Dec 21 2014 Thomas B. Ruecker <thomas@ruecker.fi> - 2.4.1-3

Initial MinGW packaging
Includes fixes contributed by Erik van Pienbroek.
