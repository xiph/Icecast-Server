GIT_BRANCH=`git rev-parse --abbrev-ref HEAD`

if [ "z$OBS_BASE" = "z" ]; then
  if [ "$GIT_BRANCH" = "master" ]; then
    export OBS_BASE=multimedia:xiph:nightly
  elif [ "$GIT_BRANCH" = "devel" -o "$GIT_BRANCH" = "dev-stjauernick-osc-gitlab-ci" ];
    export OBS_BASE=home:stephan48:branches:multimedia:xiph:beta-devel
  else
    echo "branch '$GIT_BRANCH' is not master, please export OBS_BASE accordingly";
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
