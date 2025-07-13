#!/bin/sh -xe
# Script used to make sure we are ready to use osc to upload to OBS
# Installing osc could (should) be done in the image
# Second part handle a OSC_RC set to a file variable from gitlab,
# in the proper path, with the proper permissions
# actual config path can be overridden using $OSC_RC_FILE

# Debug info for the pipeline
cat /etc/os*

# We are alone using this container. Let's break stuff (someday use a venv).
pip install osc --break-system-packages

OSC_RC_FILE=${OSC_RC_FILE:-$HOME/.config/osc/oscrc}
echo "Using $OSC_RC_FILE as osc config (override with \$OSC_RC_FILE)"

# if $OSC_RC is set, and the file exists, copy it as osc's config

if [ -n "$OSC_RC" ] && [ -f "$OSC_RC" ]; then
  echo "Found config at $OSC_RC, copying to $OSC_RC_FILE"
  install -m 600 "$OSC_RC" -D "$OSC_RC_FILE"
else
  echo "\$OSC_RC not set or file does not exist"
  exit 1
fi
