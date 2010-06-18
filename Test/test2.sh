#!/bin/bash
X264_MIN_HEART_RATE=5
X264_MAX_HEART_RATE=10
THREAD_COUNT=1
while getopts 'm:M:t:a:f:p:dc:x:' OPT; do
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
		c)
			if [ ! -z $OPTARG ]; then
				COUNTER=_c$OPTARG
			fi;;
		x)
			EXTRA_ARG=$OPTARG;;
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
		if [ -z $EXTRA_ARG ]; then
			PROGSYM=co
		else
			EXTRA_ARGS="-d $EXTRA_ARG"
			PROGSYM=co_$EXTRA_ARG
		fi;;
	none)
		PROGRAM=none
		PROGSYM=none;;
esac

if [ -z $PROGRAM ]; then
	echo a service must be specified
	exit 1
fi

if [ ! -z $FREQ ]; then
	for i in 0 1 2 3; do cpufreq-set -c $i -f ${FREQ}GHz; done
fi

mkdir -p "$HEARTBEAT_ENABLED_DIR"

../x264-heartbeat-shared/x264 -B 400 --threads $THREAD_COUNT -o out.264 pipe.y4m >/dev/null & mplayer -nolirc ../../video-x264/tractor.mkv -vo yuv4mpeg:file=pipe.y4m -nosound > /dev/null &

while [ -z $PID ]; do
	PID=$(ls "$HEARTBEAT_ENABLED_DIR")
	PID_COUNT=$(echo $PID | wc -w)
	if [ $PID_COUNT -gt 1 ]; then
		echo "too many pids! $PID"
		exit 2
	elif [ $PID_COUNT -eq 0 ]; then
		unset PID
	fi
done

if [ ! -z $AFFINITY ]; then
	echo taskset -pc $AFFINITY $PID
	taskset -pc $AFFINITY $PID
fi

if [ $PROGRAM = none ]; then
	echo please run the service separately
else
	LOGNAME=logs/${PROGSYM}_hr${X264_MIN_HEART_RATE}-${X264_MAX_HEART_RATE}_t${THREAD_COUNT}_a${AFFINITY}_f${FREQ}${COUNTER}.txt
	echo Sending output to $LOGNAME
	if [ ! -z $DEBUG ]; then
		gdb --args $PROGRAM $EXTRA_ARGS 240 $AFFINITY
	else
		echo $PROGRAM $EXTRA_ARGS 240 $AFFINITY
		$PROGRAM $EXTRA_ARGS 240 $AFFINITY| tee $LOGNAME
	fi
fi

wait $PID
echo test process $PID terminated

