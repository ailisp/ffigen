#!/bin/sh
FFIGEN_DIR=`dirname $0`/../ffigen
CFLAGS="-isystem ${FFIGEN_DIR}/include -isystem /usr/include/mingw -isystem /usr/include/w32api -isystem /usr/local/include -quiet -fffigen ${CFLAGS}"
GEN=${FFIGEN_DIR}/bin/ffigen


