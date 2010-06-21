#!/bin/sh
for f in eldream.264 eledream.264 foreman.264 test.264 tractor-mod2.264; do
	fn=$(basename $f .264)
	for i in 1 2 3; do
		sudo -E sh test4.sh -m 01.00 -M 02.00 -t 4 -a 0-3 -f 2 -p co -x -d+dummy_control -c ENC$fn$i -v ../../video-x264/$f
	done
done
