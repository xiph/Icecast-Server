#!/bin/bash -xe

SCRIPT_DIR=`dirname $0`
SCRIPT_DIR=`realpath $SCRIPT_DIR`
CONFIG=${1:-nightly}

. $SCRIPT_DIR/nightly-config.sh

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

$OSC_CMD checkout $OBS_BASE $ICECAST_PROJECT 

pushd $OBS_BASE/$ICECAST_PROJECT

# disabled for now
#find . -mindepth 1 -name '*' | grep -v ".osc" | xargs -r $OSC_CMD rm 

# copy dist archive
cp $SOURCE/icecast-$ICECAST_VERSION.tar.gz icecast2_$ICECAST_VERSION.orig.tar.gz 

ls -la

GZIP=-n tar -C $SCRIPT_DIR/$ICECAST_PROJECT -cvzf icecast2_$ICECAST_VERSION-1.debian.tar.gz debian/

# these files will be copied back - adjust as needed
cp -a $SCRIPT_DIR/$ICECAST_PROJECT/icecast* .

$SCRIPT_DIR/../fix-dsc.sh

$OSC_CMD add *
$OSC_CMD diff
$OSC_CMD commit -m "Commit via $CI_PIPELINE_URL"

popd

$OSC_CMD checkout $OBS_BASE $W32_ICECAST_INSTALLER_PROJECT

pushd $OBS_BASE/$W32_ICECAST_INSTALLER_PROJECT

#disabled for now
#find . -mindepth 1 -name '*' | grep -v ".osc" | xargs -r $OSC_CMD rm 

# we don't copy a dist file because we don't need this for the installer - it gets the sources from the installed version

# these files will be copied back - adjust as needed
cp -a $SCRIPT_DIR/$W32_ICECAST_INSTALLER_PROJECT/*.spec .

$OSC_CMD add *
$OSC_CMD diff
$OSC_CMD commit -m "Commit via $CI_PIPELINE_URL"

popd


$OSC_CMD checkout $OBS_BASE $W32_ICECAST_PROJECT

pushd $OBS_BASE/$W32_ICECAST_PROJECT

# disabled for now
#find . -mindepth 1 -name '*' | grep -v ".osc" |  xargs -r $OSC_CMD rm 

# copy dist archive
cp $SOURCE/icecast-$ICECAST_VERSION.tar.gz icecast2_$ICECAST_VERSION.orig.tar.gz 

# these files will be copied back - adjust as needed
cp -a $SCRIPT_DIR/$W32_ICECAST_PROJECT/*.spec .

$OSC_CMD add *
$OSC_CMD diff
$OSC_CMD commit -m "Commit via $CI_PIPELINE_URL"

popd

ls -la

if [ "$NOCLEANUP" != "1" ]; then
  shred -vzf $OSC_RC .osc_cookiejar
  cd ..
  echo > $OSC_RC
  rm -rf "osc_tmp"
fi

