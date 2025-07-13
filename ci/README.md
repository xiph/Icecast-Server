# Icecast CI for automated OBS Uploads

This directory contains scripting triggered by .gitlab-ci.yml to automatically upload releases and nightlies to build.opensuse.org.
The Pipeline triggers are set so that releases are pushed only on tags and nightlies on master/devel commits into respective OBS projects.
These are defined in the ci/osc/ for releases and nightlies in their respective config files.

TODO: add info regarding forks and expected repo layout in gitlab/*blub* - ask stephan48 in the meantime

# How to release

To make a new release please call the magic version changer as following from the repo root:

```
ci/create-changelog-and-set-versions.sh "2.4.99.4" "_VERSION_ARCHIVE_" "2.4.99.4" "now" "Stephan Jauernick <info@stephan-jauernick.de>" "Preparing for 2.5.0"
ci/create-changelog-and-set-versions.sh "2.5.0" "_VERSION_ARCHIVE_" "2.5.0" "now" "Stephan Jauernick <info@stephan-jauernick.de>" "2.5 is great"
```

Please adapt the Author, the Date(now; please enter a valid ISO8601 Date if needed) and the Message as needed.

This script/mechanism will update all the version references and then show you a git status/diff. Please check all changes and commit them as needed.

For now, a X.Y.Z.B release will set the versions for a X.Y+1 beta B release, this could be changed to allow patch pre-release.

After tagging and uploading the release will be picked up by gitlabs CI and a release will be pushed to OBS.

# Nightlies

A nightly will be build on each change to master/devel - these will be marked with a git version + build date in the version and a "correct" changelog entry where relevant.

# Required Repo Configuration in Gitlab

- master and devel need to be set to protected with Maintainer only push/merge permissions
- all tags need to be set to protected with Maintainer only permissions
- There needs to be a "File" Variable configured as follows:

Name: OSC_RC
Protected only: yes
Content:
```
[general]
apiurl = https://api.opensuse.org

[https://api.opensuse.org]
user = OBS USER
pass = OBS PASSWORD
```

The referenced user needs to have Maintainer access to the OBS projects referenced in the build configuration files.

# How to improve

Make version update script more robust... currently it will probably explode when we do a final 2.5 release.
Rework OBS spec and debian files to modern standards. 
