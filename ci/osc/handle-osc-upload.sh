#!/bin/bash -xe

SCRIPT_DIR=`dirname $0`
SCRIPT_DIR=`realpath $SCRIPT_DIR`
CONFIG=${1:-nightly}

. $SCRIPT_DIR/$CONFIG-config.sh

: "${OSC_RC:?Variable OSC_RC not set or empty}"
: "${CI_PIPELINE_URL:?Variable CI_PIPELINE_URL not set or empty}"

pwd

ls -la

rm -rf osc_tmp/*
mkdir -p osc_tmp
cd osc_tmp

export HOME=`pwd`
export SOURCE=`pwd`/..
export OSC_CMD="osc-wrapper.py --config=$OSC_RC"

# checkout into a dkirectory named like the project - avoiding having it in a subdir called OBS_BASE
for i in "$ICECAST_PROJECT" "$W32_ICECAST_INSTALLER_PROJECT" "$W32_ICECAST_PROJECT"; do
  $OSC_CMD checkout -o "$i" $OBS_BASE "$i"
  rm -vrf "$i"/*
done

# no comment needed
for i in "$ICECAST_PROJECT" "$W32_ICECAST_PROJECT"; do
  cp $SOURCE/icecast-$ICECAST_VERSION.tar.gz "$i"/icecast2_$ICECAST_CI_VERSION.orig.tar.gz 
done

# we copy the spec for these projects - for the icecast project the spec is globeed
for i in "$W32_ICECAST_INSTALLER_PROJECT" "$W32_ICECAST_PROJECT"; do
  cp -a $SCRIPT_DIR/$i/$i.spec $i/
done

# this is more complex because we have more files.
cp -a $SCRIPT_DIR/$ICECAST_PROJECT/icecast* $ICECAST_PROJECT/
cp -a $SCRIPT_DIR/$ICECAST_PROJECT/debian $ICECAST_PROJECT/

if [ "$DISABLE_CHANGELOG" == "0" ]; then
  pushd $SOURCE
    $HOME/create-changelog-and-set-versions.sh "2.5-beta.2" "2.4.99.2" "2.5 beta2" "25-beta-2" "2.5-beta2" "2.4.99.2" "2.4.99.2" "now" "Stephan Jauernick <info@stephan-jauernick.de>" "CI Build - $CI_PIPELINE_URL" "icecast" "mingw32-icecast" "mingw32-icecast-installer"  
  popd
fi

tar -C $ICECAST_PROJECT -cvzf $ICECAST_PROJECT/icecast2_$ICECAST_CI_VERSION-1.debian.tar.gz debian/

# remove debian/ so it does not end up in the archive
rm -rf $ICECAST_PROJECT/debian

# we fix the dsc to have the correct hashsums so dpkg-buildpackage and co do not complain

pushd $ICECAST_PROJECT

$SCRIPT_DIR/../fix-dsc.sh

popd

# we use addremove to detect changes and commit them to the server
for i in "$ICECAST_PROJECT" "$W32_ICECAST_INSTALLER_PROJECT" "$W32_ICECAST_PROJECT"; do
  pushd $i

  $OSC_CMD addremove
  $OSC_CMD diff
  $OSC_CMD commit -m "Commit via $CI_PIPELINE_URL"

  popd
done

# we cleanup because the OSC_RC should not remain on disk
if [ "$NOCLEANUP" != "1" ]; then
  shred -vzf $OSC_RC .osc_cookiejar
  cd ..
  echo > $OSC_RC
  rm -rf "osc_tmp"
fi

