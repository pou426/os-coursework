#!/bin/sh

BASE_DIR=`pwd`

echo "Building infos..."

make -C $BASE_DIR/infos

echo "Building infos user-space..."

make -C $BASE_DIR/infos-user

echo "Creating infos user-space hard-disk image..."

make -C $BASE_DIR/infos-user fs
