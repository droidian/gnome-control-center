#!/bin/bash

FILES="panels/usb panels/power panels/nfc panels/fingerprint panels/waydroid"

usage() {
	echo "usage: build-aux/msg_droidian.sh language"
	exit 0
}

(( $# != 1 )) && usage

pushd . >/dev/null

cd "$(git rev-parse --show-toplevel)"

lang=po/$1.po
filename=$(basename $lang)

trap "rm -f /tmp/gcc$$.pot" EXIT

>/tmp/gcc$$.pot

git ls-files panels/usb panels/power panels/nfc panels/fingerprint  | grep -E '(.*\.[ch]$|.*\.ui$)' | while read file
do
	xgettext --from-code=UTF-8 -j $file -o /tmp/gcc$$.pot
done

mkdir -p droidian_po
if [ -e droidian_po/$filename ]
then
	msgcat --use-first $lang droidian_po/$filename > droidian_po/new_$filename
else
	cat $lang > droidian_po/new_$filename
fi
msgmerge -N droidian_po/new_$filename /tmp/gcc$$.pot > droidian_po/$filename

rm -f droidian_po/new_$filename

popd >/dev/null
