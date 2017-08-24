#!/bin/bash

testdir=$(dirname "$0")
test -n "$testdir" && cd "$testdir" || exit

ICECAST_BASE_URL="http://localhost:8000/"

RETURN=0
MOUNT_LISTENER_AUTH="foo.ogg"
MOUNT_SOURCE_AUTH="test.ogg"

AUTH_MOUNT="foobar:hackmemore"
AUTH_SOURCE="source:hackme"
AUTH_ADMIN="admin:hackme"

L_USER="foo"
L_PASS="bar"
L_AUTH="$L_USER:$L_PASS"

echo "Starting Icecast"
../src/icecast -c ./icecast.xml 2> /dev/null &
ICECAST_PID=$!
sleep 5

echo "Starting Source client on /$MOUNT_LISTENER_AUTH"
ffmpeg -loglevel panic -re -f lavfi -i "sine=frequency=1000" -content_type application/ogg "icecast://$AUTH_SOURCE@127.0.0.1:8000/$MOUNT_LISTENER_AUTH" &
SOURCE1_PID=$!

echo "Starting Source client on /$MOUNT_SOURCE_AUTH"
ffmpeg -loglevel panic -re -f lavfi -i "sine=frequency=1000" -content_type application/ogg "icecast://$AUTH_MOUNT@127.0.0.1:8000/$MOUNT_SOURCE_AUTH" &
SOURCE2_PID=$!

function test_endpoint {
    echo " CURL $1"
    if test "x$3" == "x"; then
        res=$(curl -m 5 -o /dev/null -D - "$ICECAST_BASE_URL$1" 2>/dev/null | head -n 1 | cut -d$' ' -f2- | tr -d '\r\n')
    else
        res=$(curl -m 5 -u "$3" -o /dev/null -D - "$ICECAST_BASE_URL$1" 2>/dev/null | head -n 1 | cut -d$' ' -f2- | tr -d '\r\n')
    fi
    if test "$(echo "$res" | cut -d$' ' -f1)" -eq "$2"; then
        printf "   \e[92mOK\e[39m"
        echo " [$res]"
    else
        printf "   \e[91mFAIL\e[39m"
        echo " [$res] Expected: $2"
        RETURN=1
    fi
}

echo
echo "Testing admin/manageauth endpoint"
test_endpoint "admin/manageauth?id=4&username=$L_USER&password=$L_PASS&action=add"                              401
test_endpoint "admin/manageauth?id=99&username=$L_USER&password=$L_PASS&action=add"                             401
test_endpoint "admin/manageauth?id=99&username=$L_USER&password=$L_PASS&action=add"                             404 "$AUTH_ADMIN"
test_endpoint "admin/manageauth?id=4&username=$L_USER&password=$L_PASS&action=add"                              200 "$AUTH_ADMIN"

echo
echo "Testing admin/buildm3u endpoint"
test_endpoint "admin/buildm3u?username=$L_USER&password=$L_PASS&mount=%2F$MOUNT_LISTENER_AUTH"                  200
test_endpoint "admin/buildm3u?username=something&password=foo&mount=%2F$MOUNT_LISTENER_AUTH"                    200

echo
echo "Testing admin/stats endpoint"
test_endpoint "admin/stats"                                                                                     401
test_endpoint "admin/stats"                                                                                     401 "$AUTH_SOURCE"
test_endpoint "admin/stats"                                                                                     401 "$L_AUTH"
test_endpoint "admin/stats"                                                                                     200 "$AUTH_ADMIN"

echo
echo "Testing admin/listclients endpoint"
test_endpoint "admin/listclients"                                                                               401
test_endpoint "admin/listclients"                                                                               401 "$AUTH_SOURCE"
test_endpoint "admin/listclients"                                                                               401 "$L_AUTH"
test_endpoint "admin/listclients"                                                                               400 "$AUTH_ADMIN"
test_endpoint "admin/listclients?mount=%2F$MOUNT_LISTENER_AUTH"                                                 200 "$AUTH_ADMIN"

echo
echo "Testing admin/moveclients endpoint"
test_endpoint "admin/moveclients"                                                                               401
test_endpoint "admin/moveclients"                                                                               401 "$AUTH_SOURCE"
test_endpoint "admin/moveclients"                                                                               401 "$L_AUTH"
test_endpoint "admin/moveclients"                                                                               400 "$AUTH_ADMIN"
test_endpoint "admin/moveclients?mount=%2F$MOUNT_LISTENER_AUTH"                                                 200 "$AUTH_ADMIN"

echo
echo "Testing admin/updatemetadata endpoint"
test_endpoint "admin/updatemetadata"                                                                            401
test_endpoint "admin/updatemetadata"                                                                            401 "$AUTH_SOURCE"
test_endpoint "admin/updatemetadata"                                                                            401 "$L_AUTH"
test_endpoint "admin/updatemetadata"                                                                            400 "$AUTH_ADMIN"
test_endpoint "admin/updatemetadata?mount=%2F$MOUNT_LISTENER_AUTH"                                              200 "$AUTH_ADMIN"

echo
echo "Testing admin/metadata endpoint"
test_endpoint "admin/metadata"                                                                                  401
test_endpoint "admin/metadata"                                                                                  401 "$AUTH_SOURCE"
test_endpoint "admin/metadata"                                                                                  401 "$L_AUTH"
test_endpoint "admin/metadata"                                                                                  400 "$AUTH_ADMIN"
test_endpoint "admin/metadata?mount=%2F$MOUNT_LISTENER_AUTH&mode=updinfo&charset=UTF-8&song=Test1"              200 "$AUTH_SOURCE"
test_endpoint "admin/metadata?mount=%2F$MOUNT_LISTENER_AUTH&mode=updinfo&charset=UTF-8&song=Test2"              200 "$AUTH_ADMIN"
test_endpoint "admin/metadata?mount=%2F$MOUNT_SOURCE_AUTH&mode=updinfo&charset=UTF-8&song=Test1"                401 "$AUTH_SOURCE"
test_endpoint "admin/metadata?mount=%2F$MOUNT_SOURCE_AUTH&mode=updinfo&charset=UTF-8&song=Test2"                200 "$AUTH_ADMIN"
test_endpoint "admin/metadata?mount=%2F$MOUNT_SOURCE_AUTH&mode=updinfo&charset=UTF-8&song=Test3"                200 "$AUTH_MOUNT"

echo
echo "Testing admin/listmounts endpoint"
test_endpoint "admin/listmounts"                                                                                  401
test_endpoint "admin/listmounts"                                                                                  401 "$AUTH_SOURCE"
test_endpoint "admin/listmounts"                                                                                  401 "$L_AUTH"
test_endpoint "admin/listmounts"                                                                                  200 "$AUTH_ADMIN"
test_endpoint "admin/listmounts?mount=%2F$MOUNT_LISTENER_AUTH"                                                    400 "$AUTH_ADMIN"

echo
echo "Testing mountpoint which requires auth"
test_endpoint "$MOUNT_LISTENER_AUTH"                                                                              401
test_endpoint "$MOUNT_LISTENER_AUTH"                                                                              401 "$AUTH_SOURCE"
test_endpoint "$MOUNT_LISTENER_AUTH"                                                                              200 "$L_AUTH"
test_endpoint "$MOUNT_LISTENER_AUTH"                                                                              401 "$AUTH_ADMIN"

echo
echo "Testing mountpoint which doesn't require auth"
test_endpoint "$MOUNT_SOURCE_AUTH"                                                                                200
test_endpoint "$MOUNT_SOURCE_AUTH"                                                                                200 "$AUTH_SOURCE"
test_endpoint "$MOUNT_SOURCE_AUTH"                                                                                200 "$L_AUTH"
test_endpoint "$MOUNT_SOURCE_AUTH"                                                                                200 "$AUTH_ADMIN"

echo
echo "Testing admin/killsource endpoint"
test_endpoint "admin/killsource"                                                                                  401
test_endpoint "admin/killsource"                                                                                  401 "$AUTH_SOURCE"
test_endpoint "admin/killsource"                                                                                  401 "$L_AUTH"
test_endpoint "admin/killsource"                                                                                  400 "$AUTH_ADMIN"
test_endpoint "admin/killsource?mount=%2F$MOUNT_LISTENER_AUTH"                                                    200 "$AUTH_ADMIN"

echo
echo "All tests done"
echo

if kill $SOURCE1_PID; then
    echo "Terminated SOURCE1"
fi
if kill $SOURCE2_PID; then
    echo "Terminated SOURCE2"
fi

if kill $ICECAST_PID; then
    echo "Terminated Icecast"
fi

exit $RETURN
