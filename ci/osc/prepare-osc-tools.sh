#!/bin/sh -xe
cat /etc/os*
apk update
apk add python3 py3-pip python3-dev openssl-dev g++ make swig coreutils bash ca-certificates
pip install osc
which osc-wrapper.py
osc-wrapper.py --version
