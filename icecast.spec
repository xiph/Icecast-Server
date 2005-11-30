Name:		icecast
Version:	2.3.1
Release:	0
Summary:	Xiph Streaming media server that supports multiple audio formats.
Group:		Applications/Multimedia
License:	GPL
URL:		http://www.icecast.org/
Vendor:		Xiph.org Foundation <team@icecast.org>
Source:     	http://downloads.us.xiph.org/releases/icecast/%{name}-%{version}.tar.gz
Prefix:		%{_prefix}
BuildRoot:	%{_tmppath}/%{name}-root

Requires:       libvorbis >= 1.0
BuildRequires:	libvorbis-devel >= 1.0
Requires:       libogg >= 1.0
BuildRequires:	libogg-devel >= 1.0
Requires:       curl >= 7.10.0
BuildRequires:	curl-devel >= 7.10.0
Requires:       libxml2
BuildRequires:	libxml2-devel
Requires:       libxslt
BuildRequires:	libxslt-devel
Requires:       libtheora
BuildRequires:	libtheora-devel
Requires:       speex
BuildRequires:	speex-devel

%description
Icecast is a streaming media server which currently supports Ogg Vorbis 
and MP3 audio streams. It can be used to create an Internet radio 
station or a privately running jukebox and many things in between. 
It is very versatile in that new formats can be added relatively 
easily and supports open standards for commuincation and interaction.

%prep
%setup -q -n %{name}-%{version}

%build
CFLAGS="$RPM_OPT_FLAGS" ./configure --prefix=%{_prefix} --mandir=%{_mandir} --sysconfdir=/etc
make

%install
[ "$RPM_BUILD_ROOT" != "/" ] && rm -rf $RPM_BUILD_ROOT

make DESTDIR=$RPM_BUILD_ROOT install
rm -rf $RPM_BUILD_ROOT%{_datadir}/doc/%{name}

%clean 
[ "$RPM_BUILD_ROOT" != "/" ] && rm -rf $RPM_BUILD_ROOT

%files
%defattr(-,root,root)
%doc README AUTHORS COPYING NEWS TODO ChangeLog
%doc doc/*.html
%doc doc/*.jpg
%doc doc/*.css
%config(noreplace) /etc/%{name}.xml
%{_bindir}/icecast
%{_prefix}/share/icecast/*

%changelog
