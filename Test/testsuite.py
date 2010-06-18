#!/usr/bin/env python
from math import ceil
from os import system

hrminlimit = 4
hrmaxlimit = 12
thread_num = 4
init_core = 4
init_freq = 1.2
programs = ["co -x step_heuristics", "co -x core_controller", "co -x machine_state_controller", "fs", "fs2", "ca", "co -x core_heuristics", "co -x freq_heuristics", "co -x uncoordinated_heuristics"]
repetitions = 5
hr_min_max_pairs = [(8, 9), (9, 10), (10, 11), (7, 8), (4, 5), (5, 6), (6, 7), (11, 12), (4, 6), (5, 7), (6, 8), (7, 9), (8, 10), (9, 11), (10, 12), (11, 13), (4, 7), (6, 9), (8, 11), (10, 13)]

#hr_range_step_pairs = [ (1,1), (2,1), (3,2) ]
#for hrrange, hrstep in hr_range_step_pairs:
#	for i in xrange(int(ceil(float(hrmaxlimit - hrminlimit)/hrstep))):
#		hrmin = hrminlimit + i*hrstep
#		hrmax = hrmin + hrrange
#		hr_min_max_pairs.append((hrmin, hrmax))
#print hr_min_max_pairs
#exit(1)

for counter in xrange(1,repetitions+1):
	for hrmin, hrmax in hr_min_max_pairs:
		for progname in programs:
			cmd = "sh test2.sh -m %05.2f -M %05.2f -t %d -a 0-%d -f %d -p %s -c %d" % (hrmin, hrmax, thread_num, init_core-1, init_freq, progname, counter)
			print cmd
			status = system(cmd)
			if status != 0:
				print "test execution failed! aborting..."
				exit(2)

