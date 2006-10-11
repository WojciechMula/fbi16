#!/bin/sh

if [ -e $1 ]
then
	f=/tmp/`basename $1`.pgm
	if [ $1 -nt $f ]
	then
		convert $1 /tmp/fbi16.pgm
		pnmquant -fs 2 /tmp/fbi16.pgm > $f
		rm /tmp/fbi16.pgm
	fi
	fbi16.bin $f
fi
