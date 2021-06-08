#!/bin/sh

if ! [ $(id -u) = 0 ]; then
	echo "You must run this script as root!"
	exit 1
fi

TM_NAME=hid-tmff2
TM_VER=0.8
DST="/usr/src/${TM_NAME}-${TM_VER}"

mkdir -p "${DST}"
cp -r ./* "${DST}"

dkms add -m "${TM_NAME}" -v "${TM_VER}"
dkms build -m "${TM_NAME}" -v "${TM_VER}"
dkms install -m "${TM_NAME}" -v "${TM_VER}"

exit $?
