#!/bin/bash -xe
# Script used by Gitlab's CI to push built packgages to OBS for packaging.
# It's configured using common-config.sh and nightly-config.sh for master/devel
# and release-config.sh through variables
# We'll need OBS_BASES to be set as a list of strings indicating OBS projects
# we want to upload our packages to.

# Set SCRIPT_DIR to current directory of the script
SCRIPT_DIR=$(realpath "$(dirname "$0")")
# Set OSC_RC_FILE to define which file has osc config (and credentials)
# This can be overriden through CI or when manually called
OSC_RC_FILE=${OSC_RC_FILE:-$HOME/.config/osc/oscrc}

# Gitlab CI calls the script without parameters for master/devel
# and adds "release" as a param for releases, so we use that to load variables
CONFIG=${1:-nightly}

. "$SCRIPT_DIR/common-config.sh"
. "$SCRIPT_DIR/$CONFIG-config.sh"

# This is set by Gitlab, but should be set if run manually
: "${CI_PIPELINE_URL:?Variable CI_PIPELINE_URL not set or empty}"

# Print current dir for debug
pwd
# Print content
ls -la

# We use $HOME/.local/bin/osc --config=$ORC_RC_FILE as our osc command
# This can be overriden, and would be changed if osc was properly installed
export OSC_CMD="${OSC_CMD:-$HOME/.local/bin/osc} --config=$OSC_RC_FILE"
echo "Using ${OSC_CMD} for \$OSC_CMD, as $(id)"

# Remember where we are
export SOURCE=$PWD

# For each project on OBS
for OBS_BASE in $OBS_BASES; do
  printf '\n\nUploading to %s%s\n\n' "$OBS_WEB_PREFIX" "$OBS_BASE"
  # Create a temp dir for our upload, clean it up
  export OSC_TMP=osc_tmp-$OBS_BASE
  rm -rf "$OSC_TMP"
  mkdir -p "$OSC_TMP"
  cd "$OSC_TMP"
  # init dir for osc for the repository
  $OSC_CMD init "$OBS_BASE"

  # checkout into a dkirectory named like the project - avoiding having it in a subdir called OBS_BASE
  for i in "$ICECAST_PROJECT" "$W32_ICECAST_INSTALLER_PROJECT" "$W32_ICECAST_PROJECT" "$W64_ICECAST_INSTALLER_PROJECT" "$W64_ICECAST_PROJECT"; do
    $OSC_CMD checkout -o "$i" "$OBS_BASE" "$i" || $OSC_CMD mkpac "$i"
    rm -vrf "${i:?}"/*
  done

  # Place the built archive in the source for OBS
  for i in "$ICECAST_PROJECT" "$W32_ICECAST_PROJECT" "$W64_ICECAST_INSTALLER_PROJECT" "$W64_ICECAST_PROJECT"; do
    cp "$SOURCE/icecast-$ICECAST_VERSION.tar.gz" "$i/icecast2_$ICECAST_CI_VERSION.orig.tar.gz"
  done

  # we copy the spec for these projects - for the icecast project the spec is globeed
  for i in "$W32_ICECAST_INSTALLER_PROJECT" "$W32_ICECAST_PROJECT" "$W64_ICECAST_INSTALLER_PROJECT" "$W64_ICECAST_PROJECT"; do
    cp -a "$SCRIPT_DIR/$i/$i.spec" "$i/"
  done

  # this is more complex because we have more files.
  cp -a "$SCRIPT_DIR/$ICECAST_PROJECT"/icecast* "$ICECAST_PROJECT/"
  cp -a "$SCRIPT_DIR/$ICECAST_PROJECT"/debian "$ICECAST_PROJECT/"

  # If we need to add automated changelog, call the versionning script
  # Otherwise, assume it's a release, and we clean up the .spec files to be updated later by the script
  if [ "$DISABLE_CHANGELOG" == "0" ]; then
    pushd "$SOURCE"
      "$SCRIPT_DIR/../create-changelog-and-set-versions.sh" "$ICECAST_VERSION" "$ICECAST_VERSION" "$ICECAST_CI_VERSION" "$RELEASE_DATETIME" "$RELEASE_AUTHOR" "CI Build - $CI_PIPELINE_URL" "$ICECAST_PROJECT"
    popd
  else
    for i in "$ICECAST_PROJECT/$ICECAST_PROJECT.spec" "$W32_ICECAST_INSTALLER_PROJECT/$W32_ICECAST_INSTALLER_PROJECT.spec" "$W32_ICECAST_PROJECT/$W32_ICECAST_PROJECT.spec" "$W64_ICECAST_INSTALLER_PROJECT/$W64_ICECAST_INSTALLER_PROJECT.spec" "$W64_ICECAST_PROJECT/$W64_ICECAST_PROJECT.spec"; do
      sed -i "s/_VERSION_ARCHIVE_/$ICECAST_VERSION/;" "$i";
    done
  fi

  tar -C "$ICECAST_PROJECT" -cvzf "$ICECAST_PROJECT/icecast2_$ICECAST_CI_VERSION-1.debian.tar.gz" debian/

  # remove debian/ so it does not end up in the archive
  rm -rf "$ICECAST_PROJECT/debian"

  # we fix the dsc to have the correct hashsums so dpkg-buildpackage and co do not complain

  pushd "$ICECAST_PROJECT"

  "$SCRIPT_DIR/../fix-dsc.sh"

  popd

  # we use addremove to detect changes and commit them to the server
  for i in "$ICECAST_PROJECT" "$W32_ICECAST_INSTALLER_PROJECT" "$W32_ICECAST_PROJECT" "$W64_ICECAST_INSTALLER_PROJECT" "$W64_ICECAST_PROJECT"; do
    pushd "$i"

    $OSC_CMD addremove
    $OSC_CMD diff
    $OSC_CMD commit -m "Commit via $CI_PIPELINE_URL - Tag: ${CI_COMMIT_TAG:-N/A} - Branch: ${GIT_BRANCH:-N/A} - Commit: $GIT_COMMIT - Release Author: ${RELEASE_AUTHOR} - Commit Author: ${CI_COMMIT_AUTHOR}"

    popd
  done
done

 # we cleanup because the OSC_RC should not remain on disk
if [ "$NOCLEANUP" != "1" ]; then
  for file in "$OSC_RC" "$OSC_RC_FILE" .osc_cookiejar; do
    [ -n "$file" ] && [ -f "$file" ] && shred -vzf "$file"
    echo > "$file"
  done
  rm -rf osc_tmp*
fi
