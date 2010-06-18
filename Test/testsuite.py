#!/usr/bin/env python
from math import ceil
from os import system

hrminlimit = 4
hrmaxlimit = 12
hr_range_step_pairs = [ (1,1), (2,1), (3,2) ]
thread_num = 4
init_core = 4
init_freq = 1.2
programs = ["fs", "fs2", "ca", "co"]
repetitions = 5

for counter in xrange(1,repetitions+1):
	for hrrange, hrstep in hr_range_step_pairs:
		for i in xrange(int(ceil(float(hrmaxlimit - hrminlimit)/hrstep))):
			for progname in programs:
				hrmin = hrminlimit + i*hrstep
				hrmax = hrmin + hrrange
				cmd = "sh test2.sh -m %05.2f -M %05.2f -t %d -a 0-%d -f %d -p %s -c %d" % (hrmin, hrmax, thread_num, init_core-1, init_freq, progname, counter)
				print cmd
				system(cmd)

