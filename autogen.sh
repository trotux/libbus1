#!/bin/bash

set -e

oldpwd=$(pwd)
topdir=$(dirname $0)
cd $topdir

autoreconf --force --install --symlink

if [[ -f "$topdir/.config.args" ]]; then
        args="$args $(cat $topdir/.config.args)"
fi

# https://wiki.debian.org/Multiarch/Tuples
if [[ "$HOSTTYPE" == "x86_64" ]]; then
  ARCHITECTURE_TUPLE=x86_64-linux-gnu
elif [[ "$HOSTTYPE" == "arm" ]]; then
  ARCHITECTURE_TUPLE=arm-linux-gnueabihf
else
  echo "Unknown HOSTTYPE"
  exit 1
fi

cd $oldpwd

if [[ "$1" == "b" ]]; then
        $topdir/configure --enable-debug --prefix=/usr --sysconfdir=/etc --localstatedir=/var --libdir=/usr/lib/$ARCHITECTURE_TUPLE
        make clean
elif [[ "$1" == "c" ]]; then
        $topdir/configure --enable-debug $args
        make clean
elif [[ "$1" == "l" ]]; then
        $topdir/configure CC=clang $args
        make clean
else
        echo
        echo "----------------------------------------------------------------"
        echo "Initialized build system. For a common configuration please run:"
        echo "----------------------------------------------------------------"
        echo
        echo "$topdir/configure $args"
        echo
fi
