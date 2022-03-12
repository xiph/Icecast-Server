#!/bin/bash -xe

LONG_VERSION=${1:?Missing Long Version, Use 2.5-beta.2}; shift
SHORT_VERSION=${1:?Missing Short Version, Use 2.4.99.2}; shift
STRANGE_VERSION=${1:?Missing Strange Version, Use '2.5 beta 2'}; shift
HTML_VERSION=${1:?Missing HTML Version, Use '25-beta-2'}; shift
WIN32_VERSION=${1:?Missing Win32 Version, Use '2.5-beta2'}; shift
ARCHIVE_VERSION=${1:?Missing Archive Version, Use '2.4.99.2' for ci or _VERSION_ARCHIVE_ for release}; shift
CI_VERSION=${1:?Missing CI Version - Use 2.4.99.2+bla}; shift
DATE=${1:?ISO DATE or now}; shift
AUTHOR=${1:?Mail Address}; shift
TEXT=${1:?Release Text}; shift
ICECAST_PROJECT=${1:?Icecast OSC Project Name}; shift
W32_ICECAST_PROJECT=${1:?Icecast W32 OSC Project Name}; shift
W32_ICECAST_INSTALLER_PROJECT=${1:?Icecast W32 Installer OSC Project Name}; shift

pushd ci/osc/

sed -i "1s#^#icecast2 ($CI_VERSION-1) UNRELEASED; urgency=medium\n\n  * $TEXT\n\n --  $AUTHOR  `date --date=$DATE +"%a, %d %b %Y %H:%M:%S %z"`\n\n#"  $ICECAST_PROJECT/debian/changelog

for i in "$ICECAST_PROJECT/$ICECAST_PROJECT.spec" "$W32_ICECAST_INSTALLER_PROJECT/$W32_ICECAST_INSTALLER_PROJECT.spec" "$W32_ICECAST_PROJECT/$W32_ICECAST_PROJECT.spec"; do
  sed -i "s/_VERSION_ARCHIVE_/$ARCHIVE_VERSION/; s/^Version:.*$/Version: $CI_VERSION/; s#^%changelog.*\$#\0\n* `date --date=$DATE +"%a %b %d %Y"` $AUTHOR - $CI_VERSION\n- $TEXT\n\n#" "$i";
done

popd

sed -i "1s#^#`date --date=$DATE +"%Y-%m-%d %H:%M:%S"`  $AUTHOR\n\n        * $TEXT\n\n#" ChangeLog

sed -i "1s#\[[.0-9]*\]#[$SHORT_VERSION]#" configure.ac

sed -i "s/Icecast .* Documentation/Icecast $STRANGE_VERSION Documentation/; s/icecast-.*-documentation/icecast-$HTML_VERSION-documentation/" doc/index.html

./win32/icecast.nsis:  OutFile "icecast_win32_2.5-beta2.exe"
./win32/icecast.nsis:  WriteRegStr   HKLM $RegistryPathForUninstall "DisplayVersion" "2.5 beta2"

sed -i "s/(\"DisplayVersion\" \").*(\")$/\1$STRANGE_VERSION\2/" win32/icecast.nsis
sed -i "s/(OutFile \").*(\")$/\1$WIN32_VERSION\2/" win32/icecast.nsis
