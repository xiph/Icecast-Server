#
# spec file for package icecast
#
# Parts of this file taken from original SUSE and Fedora packaging
#

%define version_archive 2.4.99.3
Summary:        MinGW Windows port of Icecast streaming media server 
Name:           mingw64-icecast
Version:        2.4.99.3+2025012921+206f
Release:        2%{?dist}
Group:          Applications/Multimedia
License:        GPL-2.0
URL:            http://www.icecast.org/
Source0:        icecast2_%{version}.orig.tar.gz
BuildRequires:  mingw64-cross-binutils
BuildRequires:  mingw64-cross-gcc
BuildRequires:  mingw64-cross-pkg-config
BuildRequires:  mingw64-filesystem >= 35
BuildRequires:  automake
BuildRequires:  libtool
BuildRequires:  mingw64-libvorbis-devel >= 1.0
BuildRequires:  mingw64-libogg-devel >= 1.0
BuildRequires:  mingw64-libcurl-devel
BuildRequires:  mingw64-libxml2-devel
BuildRequires:  mingw64-libxslt-devel
BuildRequires:  mingw64-libspeex-devel
BuildRequires:  mingw64-libtheora-devel >= 1.0
BuildRequires:  mingw64-libopenssl-1_1-devel
BuildRequires:  mingw64-libigloo-devel >= 0.9.0
BuildRequires:  mingw64-librhash-devel >= 0.9.0
%_mingw64_package_header_debug
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

%_mingw64_debug_package

%prep
%setup -q -n icecast-%{version_archive}
find -name "*.html" -or -name "*.jpg" -or -name "*.png" -or -name "*.css" | xargs chmod 644

%build
echo "lt_cv_deplibs_check_method='pass_all'" >>%{_mingw64_cache}
#PATH="%{_mingw64_bindir}:$PATH" ./autogen.sh
mingw64_CFLAGS="%{_mingw64_cflags}" \
PATH="%{_mingw64_bindir}:$PATH" \
%{_mingw64_configure} \
  --with-curl=%{_mingw64_prefix} \
  --disable-static --enable-shared

%{_mingw64_make} %{?_smp_mflags}

%install
%{_mingw64_makeinstall} %{?_smp_mflags}
find %{buildroot}
rm %{buildroot}%{_mingw64_sysconfdir}/icecast.xml
cp -rfvp win32 %{buildroot}%{_mingw64_datadir}/icecast/win32

%clean
#probably redundant?
rm -rf %{buildroot}

%files
%defattr(-,root,root)
%{_mingw64_bindir}/icecast.exe
%{_mingw64_datadir}/icecast
%{_mingw64_datadir}/doc/icecast
#%%{_mingw64_sysconfdir}/icecast.xml

%changelog
* Wed Jan 29 2025 Stephan Jauernick <info@stephan-jauernick.de> - 2.4.99.3+2025012921+206f-1
- CI Build - https://gitlab.xiph.org/stephan48/icecast-server/-/pipelines/5625


* Sun Mar 13 2022 Philipp Schafft <lion@lion.leolix.org> - 2.4.99.3-1
- Preparing for 2.5 beta3 aka 2.4.99.3


* Sun Mar 06 2022 Stephan Jauernick <info@stephan-jauernick.de> - 2.4.99.2

Work in Progress rebuilding Icecast OBS CI

* Sun Dec 21 2014 Thomas B. Ruecker <thomas@ruecker.fi> - 2.4.1-3

Initial MinGW packaging
Includes fixes contributed by Erik van Pienbroek.
