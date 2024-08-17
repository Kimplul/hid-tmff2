#!/bin/sh

# script assumes it is being run from root, i.e.
# sudo ./dkms/dkms-install.sh

if ! [ $(id -u) = 0 ]; then
	echo "You must run this script as root!"
	exit 1
fi

TM_NAME=hid-tmff2
TM_VER=0.82
DST="/usr/src/${TM_NAME}-${TM_VER}"

mkdir -p "${DST}"
cp -r * "${DST}"

# copy dkms.conf specifically to be in root dir or build area
cp dkms/dkms.conf "${DST}"

cd dkms

dkms add -m "${TM_NAME}" -v "${TM_VER}"
dkms build -m "${TM_NAME}" -v "${TM_VER}"
dkms install -m "${TM_NAME}" -v "${TM_VER}"

exit $?
