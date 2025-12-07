if [ "z$OBS_BASES" = "z" ]; then
  if [ "$GIT_BRANCH" = "master" ]; then
    export OBS_BASES=$OBS_BASE_NIGHTLY_MASTER
  elif [ "$GIT_BRANCH" = "devel" ]; then
    export OBS_BASES=$OBS_BASE_NIGHTLY_DEVEL
  elif [ "$GIT_BRANCH" = "devel-phschafft" ]; then
    export OBS_BASES=$OBS_BASE_NIGHTLY_DEVEL_PHSCHAFFT
  else
    echo "branch '$GIT_BRANCH' is not master, devel or devel-phschafft, please export OBS_BASES accordingly";
    exit 1
  fi
fi


# How long should the hash be, overridable
GIT_HASH_LENGTH=${GIT_HASH_LENGTH:-4}
# Get the current commit ID
GIT_HASH=$(git rev-parse --short="$GIT_HASH_LENGTH" HEAD)
# How long should the date be, we want it to be increasing
# Consider "nightly", but this can be overriden
SHORT_DATE_FORMAT=${SHORT_DATE_FORMAT:-%Y%m%d%H}

# Compute date for CI VERSION
SHORT_DATE=$(date --utc +"$SHORT_DATE_FORMAT")

ICECAST_PLUS_VERSION=${ICECAST_VERSION/-/+}

# CI_VERSION will be something like 2.5.0+202411171222+1234
export ICECAST_CI_VERSION=${ICECAST_PLUS_VERSION}+${SHORT_DATE}+${GIT_HASH}
export DISABLE_CHANGELOG=0
export RELEASE_AUTHOR=${CI_COMMIT_AUTHOR:?Please set CI_COMMIT_AUTHOR}
export RELEASE_DATETIME=now
