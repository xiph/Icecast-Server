GIT_BRANCH=${CI_COMMIT_BRANCH:?Please define CI_COMMIT_BRANCH}

if [ "z$OBS_BASE" = "z" ]; then
  if [ "$GIT_BRANCH" = "master" ]; then
    export OBS_BASE=multimedia:xiph:nightly-master
  elif [ "$GIT_BRANCH" = "devel" ]; then
    export OBS_BASE=multimedia:xiph:nightly-devel
  else
    echo "branch '$GIT_BRANCH' is not master or devel, please export OBS_BASE accordingly";
    exit 1
  fi
fi

export ICECAST_PROJECT=icecast
export W32_ICECAST_PROJECT=mingw32-icecast
export W32_ICECAST_INSTALLER_PROJECT=mingw32-icecast-installer
export ICECAST_BETA_VERSION=2
export ICECAST_VERSION=2.4.99.2
export ICECAST_CI_VERSION=$ICECAST_VERSION+`date +%Y%m%d%H%M%S`+`git rev-parse HEAD`
export DISABLE_CHANGELOG=0
export RELEASE_AUTHOR=${CI_COMMIT_AUTHOR:?Please set CI_COMMIT_AUTHOR}
export RELEASE_DATETIME=now
