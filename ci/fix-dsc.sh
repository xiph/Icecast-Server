#!/bin/bash

BASE=`dirname $0`
TARGET=icecast.dsc
TARBASE=icecast2_2.4.99.2
cat $BASE/$TARGET.templ > $TARGET

function helper {
        HEADER=$1
        FUNCTION=$2

        echo "$HEADER" >> $TARGET
        for FILE in $TARBASE.orig.tar.gz $TARBASE-1.debian.tar.gz; do
                echo -e " `$FUNCTION $FILE | awk '{ print \$1 }'` `stat -c '%s' $FILE` $FILE" >> $TARGET
        done
}


helper "Checksums-Sha1:" "sha1sum"
helper "Checksums-Sha256:" "sha256sum"
helper "Files:" "md5sum"
