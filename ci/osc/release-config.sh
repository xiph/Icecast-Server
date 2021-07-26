if [ "z$OBS_BASE" = "z" ]; then
  if [ "z$CI_COMMIT_TAG" != "z" ]; then
    export OBS_BASE=multimedia:xiph:beta
  else
    echo "tag variable CI_COMMIT_TAG not defined, please export OBS_BASE accordingly";
    exit 1
  fi
fi

export ICECAST_PROJECT=icecast
export W32_ICECAST_PROJECT=mingw32-icecast
export W32_ICECAST_INSTALLER_PROJECT=mingw32-icecast-installer
export ICECAST_BETA_VERSION=2
export ICECAST_VERSION=2.4.99.2
export ICECAST_CI_VERSION=$ICECAST_VERSION
export DISABLE_CHANGELOG=1
export RELEASE_AUTHOR="Thomas B. Ruecker <thomas@ruecker.fi>"
export RELEASE_DATETIME=2017-11-17T09:04:42
