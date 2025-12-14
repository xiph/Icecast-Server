if [ "z$OBS_BASES" = "z" ]; then
  if [ "z$CI_COMMIT_TAG" != "z" ]; then
    if echo $CI_COMMIT_TAG | grep -q '^v[0-9]\+\.[0-9]\+\.[0-9]\+$'; then
      export OBS_BASES="$OBS_BASE_RELEASE $OBS_BASE_BETA"
    else
      export OBS_BASES="$OBS_BASE_BETA"
    fi
  else
    echo "tag variable CI_COMMIT_TAG not defined, please export OBS_BASES accordingly";
    exit 1
  fi
fi

export ICECAST_VERSION=2.4.999.2
export ICECAST_CI_VERSION=${ICECAST_VERSION/-/+}
export DISABLE_CHANGELOG=1
export RELEASE_AUTHOR="Philipp Schafft <lion@lion.leolix.org>"
export RELEASE_DATETIME=2025-12-14T21:02:47+00:00
