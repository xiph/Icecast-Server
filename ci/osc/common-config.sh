# Mark the git dir safe, as it may have wider permissions
git config --global --add safe.directory "$PWD"

GIT_BRANCH=${CI_COMMIT_BRANCH}
GIT_COMMIT=`git rev-parse HEAD`

# OBS settings, override using OBS_BASE_PROJECT
# By default: use multimedia:xiph (or OBS_BASE_PROJECT) for releases
# multimedia:xiph:nightly-master for master branch nightlies
# multimedia:xiph:nightly-devel for devel branch nightlies
OBS_WEB_PREFIX=https://build.opensuse.org/project/show/
OBS_BASE_PROJECT=${OBS_BASE_PROJECT:-multimedia:xiph}
OBS_BASE_NIGHTLY_MASTER="${OBS_BASE_PROJECT}:nightly-master"
OBS_BASE_NIGHTLY_DEVEL="${OBS_BASE_PROJECT}:nightly-devel"
OBS_BASE_NIGHTLY_DEVEL_PHSCHAFFT="${OBS_BASE_PROJECT}:nightly-devel-phschafft"
OBS_BASE_BETA="${OBS_BASE_PROJECT}:beta"
OBS_BASE_RELEASE="${OBS_BASE_PROJECT}"

export ICECAST_PROJECT=${ICECAST_PROJECT:-icecast}
export W32_ICECAST_PROJECT=${W32_ICECAST_PROJECT:-mingw32-icecast}
export W64_ICECAST_PROJECT=${W64_ICECAST_PROJECT:-mingw64-icecast}
export W32_ICECAST_INSTALLER_PROJECT=${W32_ICECAST_INSTALLER_PROJECT:-mingw32-icecast-installer}
export W64_ICECAST_INSTALLER_PROJECT=${W64_ICECAST_INSTALLER_PROJECT:-mingw64-icecast-installer}
export ICECAST_VERSION=2.4.999.2
