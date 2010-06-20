#!/bin/sh
../bin/frequencyscaler1 "$@" &
../bin/core-allocator "$@"
wait %1
