export X264_MIN_HEART_RATE=100
export X264_MAX_HEART_RATE=200

../x264-heartbeat-shared/x264 -B 400 --threads 4 -o out.264 pipe.y4m & mplayer ../../video-x264/tractor.mkv -vo yuv4mpeg:file=pipe.y4m -nosound

