#!/bin/sh
FFIGEN_DIR=`dirname $0`/../ffigen
CFLAGS="-D__APPLE_CC__ -isystem ${FFIGEN_DIR}/include -isystem /usr/include -isystem /usr/local/include -quiet -fffigen ${CFLAGS}"
GEN=${FFIGEN_DIR}/bin/ffigen


