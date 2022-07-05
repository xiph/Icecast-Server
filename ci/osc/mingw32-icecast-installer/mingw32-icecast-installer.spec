#
# spec file for package icecast
#
# Parts of this file taken from original SUSE and Fedora packaging
#

Summary:        Installer for MinGW Windows port of Icecast  streaming media server
Name:           mingw32-icecast-installer
Version:        2.4.99.3
Release:        2%{?dist}
Group:          Applications/Multimedia
License:        GPL-2.0
URL:            http://www.icecast.org/
BuildRequires:  mingw32-icecast >= %{version}
BuildRequires:  mingw32-cross-nsis
BuildRequires:  mingw32-cross-nsis-plugin-uac
BuildRequires:  mingw32-cross-nsis-plugin-zipdll
BuildRequires:  mingw32-cross-nsis-plugin-nsprocess
BuildRequires:  shared-mime-info
BuildRequires:  mingw32-cross-binutils
BuildRequires:  mingw32-filesystem >= 35
BuildRequires:  timezone
BuildRequires:  strace
BuildRequires:  gdb
BuildRequires:  glibc-debuginfo
#BuildRequires:  glibc-locale-debuginfo
BuildRequires:  libgcc_s1-debuginfo
BuildRequires:  libstdc++6-debuginfo
BuildRequires:  libz1-debuginfo
#BuildRequires:  mingw32-cross-nsis-debuginfo
BuildArch:      noarch

BuildRoot:      %{_tmppath}/%{name}-%{version}


%description
This is an official icecast.org package of Icecast for MinGW Windows.
Icecast is a streaming media server which currently supports Ogg Vorbis
and Opus audio streams, with MP3 known to work. It can be used to create
an Internet radio station or a privately running jukebox and many things
in between.  It is very versatile in that new formats can be added
relatively easily and supports open standards for commuincation
and interaction.

%prep
pwd
mkdir -p installer/bin
mkdir -p installer/log
cp    %{_mingw32_bindir}/icecast.exe    installer/bin
cd    installer/bin
%{_mingw32_datadir}/icecast/win32/dllbundler.sh -h i686-w64-mingw32 icecast.exe
ls -la
cd    ../..
ls -la
cp    %{_mingw32_datadir}/icecast/win32/icecast.ico       installer/bin
cp    %{_mingw32_datadir}/icecast/win32/icecast2logo3.bmp installer
cp    %{_mingw32_datadir}/icecast/win32/icecast2logo2.bmp installer
cp    %{_mingw32_datadir}/icecast/win32/icecast.nsis      installer
cp    %{_mingw32_datadir}/icecast/win32/icecast.bat       installer
cp    %{_mingw32_datadir}/icecast/win32/icecast.xml       installer
cp -a %{_mingw32_datadir}/icecast/*     installer/
cp -a %{_mingw32_datadir}/doc/icecast/* installer/doc/
cp    /etc/mime.types   installer/
find installer/
%build
cd installer
export -n MALLOC_CHECK_
export -n MALLOC_PERTURB_
makensis icecast.nsis -V4

%install
mkdir -p "%{buildroot}/%{_mingw32_bindir}"
cp %_builddir/installer/icecast_win32_2.5-beta3.exe "%{buildroot}/%{_mingw32_bindir}"

%clean

%files
%defattr(-,root,root)
%{_mingw32_bindir}/icecast_win32_2.5-beta3.exe

%changelog
* Sun Mar 13 2022 Philipp Schafft <lion@lion.leolix.org> - 2.4.99.3-1
- Preparing for 2.5 beta3 aka 2.4.99.3


* Sun Mar 06 2022 Stephan Jauernick <info@stephan-jauernick.de> - 2.4.99.2

Rework OBS CI/CD

* Wed Oct 31 2018 Thomas B. Ruecker <thomas@ruecker.fi> - 2.4.4-1
- Security fix: Fixed buffer overflows in URL auth code CVE-2018-18820
- For more info see ChangeLog

* Sun Jul 08 2018 Thomas B. Ruecker <thomas@ruecker.fi> - 2.4.4-1

Test installer builds for 2.4.4 release
