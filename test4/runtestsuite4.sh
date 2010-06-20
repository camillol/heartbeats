#!/bin/bash

export HEARTBEAT_ENABLED_DIR="$PWD/HB4"
mkdir -p "$HEARTBEAT_ENABLED_DIR"
mkfifo pipe4.y4m
mkdir -p logs4

if [ $(ls "$HEARTBEAT_ENABLED_DIR" | wc -l) -gt 0 ]; then
	echo "please empty $HEARTBEAT_ENABLED_DIR"
	exit 2
fi

python testsuite4.py
