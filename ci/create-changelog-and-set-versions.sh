#!/bin/bash -xe

# Script to handle version handling for releases manually or through the CI
# SHORT_VERSION (first parameter) will be used for most places
# It should be MAJOR.minor.patch to prepare an actual release
# or MAJOR.minor.patch.extra to prepare for the next minor release
# extra will then be the number of the beta release
# - in configure.ac, for the actual version used for building
# - to build the other version numbers such as :
#   - Documentation link for the current version
#   - Windows installer "displayed version" and others
SHORT_VERSION=${1:?Missing Short Version, Use 2.5.0 or 2.5.0.1}; shift
# ARCHIVE_VERSION is used as an info to differentiate releases from CI builds
# For releases, it should be set to _VERSION_ARCHIVE_ 
# For CI, something resembling the SHORT_VERSION
ARCHIVE_VERSION=${1:?Missing Archive Version, Use '2.5.0' for ci or _VERSION_ARCHIVE_ for release}; shift
# CI_VERSION is used mostly in changelogs and comments about the release
# should be set to something like SHORT_VERSION
CI_VERSION=${1:?Missing CI Version - Use 2.4.99.2+bla without any -}; shift
# Date used throughout for changelogs, and tags where we need a timestamp
DATE=$(LC_TIME=C date --utc --iso-8601=seconds --date="${1:?ISO DATE or now}"); shift
# Author (Email, or Name <email>) in changelogs and comments
AUTHOR=${1:?Mail Address}; shift
# Short description to be added to changelogs
TEXT=${1:?Release Text}; shift

if [[ "$SHORT_VERSION" =~ ^([0-9]+)\.([0-9]+)\.([0-9]+)(\.([0-9]+))?$ ]]; then
  MAJOR=${BASH_REMATCH[1]}
  MINOR=${BASH_REMATCH[2]}
  PATCH=${BASH_REMATCH[3]}
  EXTRA=${BASH_REMATCH[5]}


  LABEL=${EXTRA:+$SUFFIX}
  if [[ -n "$EXTRA" ]]; then
    ((MINOR++))
  fi
else
  echo "Sorry, that version does not comply to our ^([0-9]+)\.([0-9]+)\.([0-9]+)(\.([0-9]+))?$ regex, use something like 2.5.0 or 2.5.0.1"
  exit 3
fi

# M.m.p or M.m.p -LABEL
STRANGE_VERSION="${MAJOR}.${MINOR}.${PATCH}${EXTRA:+ $LABEL$EXTRA}"
# Mmp, or Mmp-beta-1
HTML_VERSION="${MAJOR}${MINOR}${PATCH}${EXTRA:+-$LABEL-$EXTRA}"
# M.m.p or M.m.p-LABEL
WIN32_VERSION="${MAJOR}.${MINOR}.${PATCH}${EXTRA:+-$LABEL$EXTRA}"

# Name of the package names in OBS for each part. Can be overridden.
ICECAST_PROJECT=${ICECAST_PROJECT:-icecast}
W32_ICECAST_PROJECT=${W32_ICECAST_PROJECT:-mingw32-icecast}
W64_ICECAST_PROJECT=${W64_ICECAST_PROJECT:-mingw64-icecast}
W32_ICECAST_INSTALLER_PROJECT=${W32_ICECAST_INSTALLER_PROJECT:-mingw32-icecast-installer}
W64_ICECAST_INSTALLER_PROJECT=${W64_ICECAST_INSTALLER_PROJECT:-mingw64-icecast-installer}

OSC_BASE_DIR="${OSC_TMP:-osc_tmp}"

# upon release we modify the templates - in ci we modiy temporary files
if [ "$ARCHIVE_VERSION" = "_VERSION_ARCHIVE_" ]; then
  OSC_BASE_DIR=ci/osc
fi

pushd $OSC_BASE_DIR

# Set changelog for Debian, required. Version will be set to $CI_VERSION-1
sed -i "1s#^#icecast2 ($CI_VERSION-1) UNRELEASED; urgency=medium\n\n  * $TEXT\n\n --  $AUTHOR  $(LC_TIME=C date --date="$DATE" +"%a, %d %b %Y %H:%M:%S %z")\n\n#"  "$ICECAST_PROJECT/debian/changelog"

# Set .spec to build on OBS, common changelog, version set to $ARCHIVE_VERSION
for i in "$ICECAST_PROJECT/$ICECAST_PROJECT.spec" "$W32_ICECAST_INSTALLER_PROJECT/$W32_ICECAST_INSTALLER_PROJECT.spec" "$W32_ICECAST_PROJECT/$W32_ICECAST_PROJECT.spec" "$W64_ICECAST_INSTALLER_PROJECT/$W64_ICECAST_INSTALLER_PROJECT.spec" "$W64_ICECAST_PROJECT/$W64_ICECAST_PROJECT.spec"; do
  sed -i "s/_VERSION_ARCHIVE_/$ARCHIVE_VERSION/; s/^Version:\(\s*\)[^\s]*$/Version:\1$CI_VERSION/; s#^%changelog.*\$#\0\n* $(LC_TIME=C date --date="$DATE" +"%a %b %d %Y") $AUTHOR - $CI_VERSION-1\n- $TEXT\n\n#" "$i";
done

popd

# we only update the changelog for releases - until i figure out if we want to run the magic script pre CI
if [ "$ARCHIVE_VERSION" = "_VERSION_ARCHIVE_" ]; then
  sed -i "1s#^#$(LC_TIME=C date --date="$DATE" +"%Y-%m-%d %H:%M:%S")  $AUTHOR\n\n        * $TEXT\n\n#" ChangeLog
fi
# actual version change for the build
sed -i "1s#\[[.0-9]\+\]#[$SHORT_VERSION]#" configure.ac

sed -i "s/Icecast .* Documentation/Icecast $STRANGE_VERSION Documentation/; s/icecast-.*-documentation/icecast-$HTML_VERSION-documentation/" doc/index.html

sed -i "s/\(\"DisplayVersion\" \"\).*\(\"\)$/\1$STRANGE_VERSION\2/" win32/icecast.nsis
# Update version in all config.sh script used for releases and nightly builds
sed -i "s/^\(export ICECAST_VERSION=\).*$/\1$SHORT_VERSION/" ci/osc/*-config.sh

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
