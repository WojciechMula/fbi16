#!/bin/sh

if [ -e $1 ]
then
	f=/tmp/`basename $1`.rgb
	if [ $1 -nt $f ]
	then
		convert -colors 16 $1 $f
	fi
	fbi16_2.bin `identify -format "%w" "$1"` `identify -format "%h" "$1"` $f
fi
