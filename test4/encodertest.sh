#!/bin/sh
for i in 4 5 6 7; do
	for f in eldream.264 eledream.264 foreman.264 test.264 tractor-mod2.264; do
	fn=$(basename $f .264)
		sudo -E bash testenc.sh -m 01.00 -M 02.00 -t 4 -a 0-3 -f 2 -p co -x -d+dummy_control -c ENC$fn$i -v ../../video-x264/$f
	done
done
