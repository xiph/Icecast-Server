#!/bin/bash -xe

BETA_VERSION=${1:?Missing Beta Version, Use 2}; shift
SHORT_VERSION=${1:?Missing Short Version, Use 2.4.99.2}; shift
STRANGE_VERSION=${1:?Missing Strange Version, Use '2.5 beta2'}; shift
HTML_VERSION=${1:?Missing HTML Version, Use '25-beta-2'}; shift
WIN32_VERSION=${1:?Missing Win32 Version, Use '2.5-beta2'}; shift
ARCHIVE_VERSION=${1:?Missing Archive Version, Use '2.4.99.2' for ci or _VERSION_ARCHIVE_ for release}; shift
CI_VERSION=${1:?Missing CI Version - Use 2.4.99.2+bla}; shift
DATE=`date --date=${1:?ISO DATE or now} --iso-8601=seconds`; shift
AUTHOR=${1:?Mail Address}; shift
TEXT=${1:?Release Text}; shift
ICECAST_PROJECT=${1:?Icecast OSC Project Name}; shift
W32_ICECAST_PROJECT=${1:?Icecast W32 OSC Project Name}; shift
W32_ICECAST_INSTALLER_PROJECT=${1:?Icecast W32 Installer OSC Project Name}; shift

OSC_BASE_DIR=osc_tmp

# upon release we modify the templates - in ci we modiy temporary files
if [ "$ARCHIVE_VERSION" = "_VERSION_ARCHIVE_" ]; then
  OSC_BASE_DIR=ci/osc
fi

pushd $OSC_BASE_DIR

sed -i "1s#^#icecast2 ($CI_VERSION-1) UNRELEASED; urgency=medium\n\n  * $TEXT\n\n --  $AUTHOR  `date --date=$DATE +"%a, %d %b %Y %H:%M:%S %z"`\n\n#"  $ICECAST_PROJECT/debian/changelog

for i in "$ICECAST_PROJECT/$ICECAST_PROJECT.spec" "$W32_ICECAST_INSTALLER_PROJECT/$W32_ICECAST_INSTALLER_PROJECT.spec" "$W32_ICECAST_PROJECT/$W32_ICECAST_PROJECT.spec"; do
  sed -i "s/_VERSION_ARCHIVE_/$ARCHIVE_VERSION/; s/^Version:\(\s*\)[^\s]*$/Version:\1$CI_VERSION/; s#^%changelog.*\$#\0\n* `date --date=$DATE +"%a %b %d %Y"` $AUTHOR - $CI_VERSION-1\n- $TEXT\n\n#" "$i";
done

sed -i "s/\(icecast_win32_\).*\(.exe\)/\1$WIN32_VERSION\2/" $W32_ICECAST_INSTALLER_PROJECT/$W32_ICECAST_INSTALLER_PROJECT.spec

popd

# we only update the changelog for releases - until i figure out if we want to run the magic script pre CI
if [ "$ARCHIVE_VERSION" = "_VERSION_ARCHIVE_" ]; then
  sed -i "1s#^#`date --date=$DATE +"%Y-%m-%d %H:%M:%S"`  $AUTHOR\n\n        * $TEXT\n\n#" ChangeLog
fi

sed -i "1s#\[[.0-9]*\]#[$SHORT_VERSION]#" configure.ac

sed -i "s/Icecast .* Documentation/Icecast $STRANGE_VERSION Documentation/; s/icecast-.*-documentation/icecast-$HTML_VERSION-documentation/" doc/index.html

sed -i "s/\(\"DisplayVersion\" \"\).*\(\"\)$/\1$STRANGE_VERSION\2/" win32/icecast.nsis
sed -i "s/\(OutFile \"icecast_win32_\).*\(.exe\"\)$/\1$WIN32_VERSION\2/" win32/icecast.nsis

sed -i "s/^\(export ICECAST_VERSION=\).*$/\1$SHORT_VERSION/; s/\(export ICECAST_BETA_VERSION=\).*$/\1$BETA_VERSION/" ci/osc/*-config.sh

# we only do this for release builds
if [ "$ARCHIVE_VERSION" = "_VERSION_ARCHIVE_" ]; then
  sed -i "s/^\(export RELEASE_AUTHOR=\"\).*\(\"\)$/\1$AUTHOR\2/; s/\(export RELEASE_DATETIME=\).*$/\1$DATE/" ci/osc/release-config.sh
fi

if [ "$ARCHIVE_VERSION" != "_VERSION_ARCHIVE_" ]; then
  if ! git diff --quiet; then
    echo "git detected differences after ci driven create changelog run, this should not happen - please check";
    git status
    git --no-pager diff
    exit 1;
  else
    echo "no repo diffs detected, this is good as CI should not change the repo(only temp files)!"
  fi
else
  echo "applied changes to versions, please verify and commit them for a new release"
  git status
  git --no-pager diff
fi
