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
export ICECAST_BETA_VERSION=3
export ICECAST_VERSION=2.4.99.3
export ICECAST_CI_VERSION=$ICECAST_VERSION
export DISABLE_CHANGELOG=1
export RELEASE_AUTHOR="Philipp Schafft <lion@lion.leolix.org>"
export RELEASE_DATETIME=2022-03-13T18:25:33+00:00
