#
# spec file for package icecast
#
# Parts of this file taken from original SUSE and Fedora packaging
#

Summary:        Installer for MinGW Windows port of Icecast  streaming media server
Name:           mingw64-icecast-installer
Version:        2.5.0-rc1
Release:        2%{?dist}
Group:          Applications/Multimedia
License:        GPL-2.0
URL:            http://www.icecast.org/
BuildRequires:  mingw64-icecast >= %{version}
BuildRequires:  mingw64-cross-nsis
BuildRequires:  mingw64-cross-nsis-plugin-uac
BuildRequires:  mingw64-cross-nsis-plugin-zipdll
BuildRequires:  mingw64-cross-nsis-plugin-nsprocess
BuildRequires:  shared-mime-info
BuildRequires:  mingw64-cross-binutils
BuildRequires:  mingw64-filesystem >= 35
BuildRequires:  timezone
BuildRequires:  zip
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
cp    %{_mingw64_bindir}/icecast.exe    installer/bin
cd    installer/bin
%{_mingw64_datadir}/icecast/win32/dllbundler.sh -h x86_64-w64-mingw32 icecast.exe
ls -la
cd    ../..
ls -la
cp    %{_mingw64_datadir}/icecast/win32/icecast.ico       installer/bin
cp    %{_mingw64_datadir}/icecast/win32/icecast2logo3.bmp installer
cp    %{_mingw64_datadir}/icecast/win32/icecast2logo2.bmp installer
cp    %{_mingw64_datadir}/icecast/win32/icecast.nsis      installer
cp    %{_mingw64_datadir}/icecast/win32/icecast.bat       installer
cp    %{_mingw64_datadir}/icecast/win32/icecast.xml       installer
cp -a %{_mingw64_datadir}/icecast/*     installer/
cp -a %{_mingw64_datadir}/doc/icecast/* installer/doc/
touch installer/log/.keep
cp    /etc/mime.types   installer/
find installer/
%build
cd installer
cp icecast.nsis icecast.nsis.orig
sed -i "s/win32/win64/g; s/Win32/Win64/g" icecast.nsis;
sed -i "s/\(\"DisplayVersion\" \"\).*\(\"\)$/\1%{version}\2/" icecast.nsis
sed -i 's/\(OutFile "icecast_win64_\).*\(.exe"\)$/\1%{version}\2/' icecast.nsis
sed -i 's/PROGRAMFILES32/PROGRAMFILES64/' icecast.nsis
diff -u  icecast.nsis.orig icecast.nsis || true
export -n MALLOC_CHECK_
export -n MALLOC_PERTURB_
makensis icecast.nsis -V4

cd ..
ln -s installer icecast_win64_%{version}
zip -r installer/icecast_win64_%{version}.zip icecast_win64_%{version}/{bin,doc,log,admin,web,icecast.xml,icecast.bat,mime.types}

%install
mkdir -p "%{buildroot}/%{_mingw64_bindir}"
find %_builddir/installer/
cp %_builddir/installer/icecast_win64_%{version}.exe "%{buildroot}/%{_mingw64_bindir}"
cp %_builddir/installer/icecast_win64_%{version}.zip "%{buildroot}/%{_mingw64_bindir}"

%clean

%files
%defattr(-,root,root)
%{_mingw64_bindir}/icecast_win64_%{version}.exe
%{_mingw64_bindir}/icecast_win64_%{version}.zip

%changelog
* Tue Aug 26 2025 Philipp Schafft <lion@lion.leolix.org> - 2.5.0-rc1-1
- Preparing for 2.5.0-rc1


* Wed Jan 29 2025 Stephan Jauernick <info@stephan-jauernick.de> - 2.4.99.3+2025012921+206f-1
- CI Build - https://gitlab.xiph.org/stephan48/icecast-server/-/pipelines/5625


* Sun Mar 13 2022 Philipp Schafft <lion@lion.leolix.org> - 2.4.99.3-1
- Preparing for 2.5 beta3 aka 2.4.99.3


* Sun Mar 06 2022 Stephan Jauernick <info@stephan-jauernick.de> - 2.4.99.2

Rework OBS CI/CD

* Wed Oct 31 2018 Thomas B. Ruecker <thomas@ruecker.fi> - 2.4.4-1
- Security fix: Fixed buffer overflows in URL auth code CVE-2018-18820
- For more info see ChangeLog

* Sun Jul 08 2018 Thomas B. Ruecker <thomas@ruecker.fi> - 2.4.4-1

Test installer builds for 2.4.4 release
