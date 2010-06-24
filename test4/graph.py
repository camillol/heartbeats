#!/usr/bin/env python
from __future__ import with_statement
import os
import re
import sys
from collections import defaultdict
from operator import attrgetter

import matplotlib.pyplot as plt

DEBUGLEVEL = 0

name_re = re.compile("^(.*)_hr(\d\d(?:\.\d\d)?)-(\d\d(?:\.\d\d)?)_t(\d+)_a(\d-\d)_f(\d+)_(.+).txt$")
line_re = re.compile("^(\d+)\t(\d+\.\d+)\t.*$")

class LogEntry(object):
	__slots__ = ['beat', 'hr', 'err', 'deltamid']
	def __init__(self, beat, hr, err, deltamid):
		self.beat = beat
		self.hr = hr
		self.err = err
		self.deltamid = deltamid
	def __str__(self):
		return "b:%d\thr:%05.2f\terr:%05.2f\tdm:%05.2f" % (self.beat, self.hr, self.err, self.deltamid)

class TestEntry(object):
	__slots__ = ['test_type', 'progname', 'tag', 'avg_hr', 'stdv_hr', 'avg_err', 'avg_sq_err', 'avg_sq_deltamid', 'log', 'name']
	def __init__(self, test_type, progname, tag, avg_hr, stdv_hr, avg_err, avg_sq_err, avg_sq_deltamid, log, name):
		self.test_type = test_type
		self.progname = progname
		self.tag = tag
		self.avg_hr = avg_hr
		self.stdv_hr = stdv_hr
		self.avg_err = avg_err
		self.avg_sq_err = avg_sq_err
		self.avg_sq_deltamid = avg_sq_deltamid
		self.log = log
		self.name = name

def read_log(logfile, hr_max, hr_min):
	hr_midpoint = (hr_min + hr_max) / 2
	log = []
	with open(logfile) as f:
		for line in f:
			m = line_re.match(line)
			if m == None:
				continue
			beat = int(m.group(1))
			if beat < 1 or beat > 250:
				continue
			hr = float(m.group(2))
			err = max(hr - hr_max, 0) + min(hr - hr_min, 0)
			deltamid = hr - hr_midpoint
			log.append(LogEntry(beat, hr, err, deltamid))
	return log

if len(sys.argv) < 2:
	print "DO WANT FILE TO GRAPH"
	exit(1)
logfiles = sys.argv[1:]

tests = []

for logfile in logfiles:
	name = os.path.basename(logfile)
	m = name_re.match(name)
	if m == None:
		print "cannot parse file name:", name
		continue
	progname = m.group(1)
	hr_min = float(m.group(2))
	hr_max = float(m.group(3))
	threads = int(m.group(4))
	init_affinity = m.group(5)
	init_freq = float(m.group(6))
	tag = m.group(7)
	
	log = read_log(logfile, hr_max, hr_min)
	
	beat_count = len(log)
	if beat_count == 0:
		print "WARNING: empty log:", name
		continue

	avg_hr = sum(l.hr for l in log) / beat_count
	stdv_hr = sum((l.hr-avg_hr)**2 for l in log) / beat_count
	avg_err = sum(l.err for l in log) / beat_count
	avg_sq_err = sum(l.err*l.err for l in log) / beat_count
	avg_sq_deltamid = sum(l.deltamid*l.deltamid for l in log) / beat_count
	
	test_type = (hr_min, hr_max, threads, init_freq, init_affinity)
	test = TestEntry(test_type, progname, tag, avg_hr, stdv_hr, avg_err, avg_sq_err, avg_sq_deltamid, log, name)
	tests.append(test)

# TODO: check that all tests are of the same type?

fig = plt.figure()
if 0:	# legend outside
	a = fig.add_axes([0.05, 0.25, 0.9, 0.7])
	a.axhspan(hr_min, hr_max, alpha=0.2, color='green')
	for test in tests:
		a.plot([l.beat for l in test.log], [l.hr for l in test.log], label=test.name)
	lines = a.get_lines()
	#fig.legend(lines, [line.get_label() for line in lines], loc=(0.05, 0.0))
	fig.legend(lines, [line.get_label() for line in lines], bbox_to_anchor=(0.05, 0.0, 0.9, 0.2), mode='expand')
else:
#	a = fig.add_subplot(111)
	a = fig.add_axes([0.05, 0.05, 0.9, 0.9])
	a.axhspan(hr_min, hr_max, alpha=0.2, color='green')
	for test in tests:
		a.plot([l.beat for l in test.log], [l.hr for l in test.log], label=test.name)
	lines = a.get_lines()
	a.legend()
fig.show()

#pylab.plot([l.beat for l in log], [l.hr for l in log])

#import matplotlib.font_manager 
#prop = matplotlib.font_manager.FontProperties(size=5) 
#legend(prop=prop)
