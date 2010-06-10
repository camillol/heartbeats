#!/bin/bash
X264_MIN_HEART_RATE=5
X264_MAX_HEART_RATE=10
THREAD_COUNT=1
while getopts 'm:M:t:a:f:p:d' OPT; do
	case $OPT in
		m)
			X264_MIN_HEART_RATE=$OPTARG;;
		M)
			X264_MAX_HEART_RATE=$OPTARG;;
		t)
			THREAD_COUNT=$OPTARG;;
		a)
			AFFINITY=$OPTARG;;
		f)
			FREQ=$OPTARG;;
		p)
			PROG=$OPTARG;;
		d)
			DEBUG=1;;
	esac
	echo $OPT $OPTARG
done
export X264_MIN_HEART_RATE
export X264_MAX_HEART_RATE

case $PROG in
	fs|frequencyscaler)
		PROGRAM=../bin/frequencyscaler1
		PROGSYM=fs;;
        fs2|frequencyscaler2)
		PROGRAM=../bin/frequencyscaler2
		PROGSYM=fs2;;
	ca|coreallocator)
		PROGRAM=../bin/core-allocator
		PROGSYM=ca;;
	co|combo|combined)
		PROGRAM=../bin/combined
		PROGSYM=co;;
esac

if [ -z $PROGRAM ]; then
	echo a service must be specified
	exit 1
fi

if [ ! -z $FREQ ]; then
	for i in 0 1 2 3; do cpufreq-set -c $i -f ${FREQ}GHz; done
fi

../x264-heartbeat-shared/x264 -B 400 --threads $THREAD_COUNT -o out.264 pipe.y4m >/dev/null & mplayer -nolirc ../../video-x264/tractor.mkv -vo yuv4mpeg:file=pipe.y4m -nosound > /dev/null &

if [ ! -z $AFFINITY ]; then
	taskset -pc $AFFINITY $(ls ../HB)
fi

LOGNAME=${PROGSYM}_hr${X264_MIN_HEART_RATE}-${X264_MAX_HEART_RATE}_t${THREAD_COUNT}_a${AFFINITY}_f${FREQ}.txt
echo Sending output to $LOGNAME
if [ ! -z $DEBUG ]; then
	gdb --args $PROGRAM 240 $AFFINITY
else
	$PROGRAM 240 $AFFINITY| tee $LOGNAME
fi
