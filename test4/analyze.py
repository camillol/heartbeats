#!/usr/bin/env python
from __future__ import with_statement
import os
import re
import sys
from collections import defaultdict
from operator import attrgetter

DEBUGLEVEL = 0

name_re = re.compile("^(.*)_hr(\d\d\.\d\d)-(\d\d\.\d\d)_t(\d+)_a(\d-\d)_f(\d+)_(.+).txt$")
line_re = re.compile("^(\d+)\t(\d+\.\d+)\t.*$")

class LogEntry(object):
	__slots__ = ['beat', 'hr', 'err', 'deltamid']
	def __init__(self, beat, hr, err, deltamid):
		self.beat = beat
		self.hr = hr
		self.err = err
		self.deltamid = deltamid

class TestEntry(object):
	__slots__ = ['test_type', 'progname', 'tag', 'avg_hr', 'avg_err', 'avg_sq_err', 'avg_sq_deltamid']
	def __init__(self, test_type, progname, tag, avg_hr, avg_err, avg_sq_err, avg_sq_deltamid):
		self.test_type = test_type
		self.progname = progname
		self.tag = tag
		self.avg_hr = avg_hr
		self.avg_err = avg_err
		self.avg_sq_err = avg_sq_err
		self.avg_sq_deltamid = avg_sq_deltamid


if len(sys.argv) < 2:
	print "DO WANT LOGS DIRECTORY"
	exit(1)
logdir = sys.argv[1]
if len(sys.argv) >= 3: skip_initial = int(sys.argv[2])
else: skip_initial = 0

test_types = defaultdict(list)
programs = defaultdict(list)

print "analyzing logs in %s; skipping %d initial beats" % (logdir, skip_initial)
for name in os.listdir(logdir):
	m = name_re.match(name)
	if m == None:
		if not name.startswith("."):
			print "unrecognized file name:", name
		continue
	progname = m.group(1)
	hr_min = float(m.group(2))
	hr_max = float(m.group(3))
	threads = int(m.group(4))
	init_affinity = m.group(5)
	init_freq = float(m.group(6))
	tag = m.group(7)
	
	hr_midpoint = (hr_min + hr_max)/2
	log = []
	
	path = os.path.join(logdir, name)
	with open(path) as f:
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
	
	beat_count = len(log)
	if beat_count == 0:
		print "WARNING: empty log:", name
		continue
	elif beat_count != 240:
		print "WARNING: beat count is %d, beats from %d to %d:" % (beat_count, log[0].beat, log[-1].beat), name
	
	log = log[skip_initial:]
	beat_count = len(log)
	
	avg_hr = sum(l.hr for l in log) / beat_count
	avg_err = sum(l.err for l in log) / beat_count
	avg_sq_err = sum(l.err*l.err for l in log) / beat_count
	avg_sq_deltamid = sum(l.deltamid*l.deltamid for l in log) / beat_count
	
	if DEBUGLEVEL >= 1:
		print "[%d|%s|%f] %05.2f - %05.2f %s: avg hr: %f avg err: %f, avg err^2: %f, avg dm^2: %f" % \
			(threads, init_affinity, init_freq, hr_min, hr_max, progname, avg_hr, avg_err, avg_sq_err, avg_sq_deltamid)
	
	test_type = (hr_min, hr_max, threads, init_freq, init_affinity)
	test = TestEntry(test_type, progname, tag, avg_hr, avg_err, avg_sq_err, avg_sq_deltamid)
	test_types[test_type].append(test)
	programs[progname].append(test)

for test_type, test_list in test_types.items():
	test_list.sort(key=attrgetter('avg_sq_err'))
	print "hr: %05.2f-%05.2f - %d threads - init: freq %05.2f aff %s" % test_type
	print "av hr\tav err\tav sqer\tav sqdm\tprogname (run)"
	for test in test_list:
		print "%05.2f\t%05.3f\t%05.3f\t%05.3f\t%s (%s)" % \
			(test.avg_hr, test.avg_err, test.avg_sq_err, test.avg_sq_deltamid, test.progname, test.tag)
	print

program_avg_sqe = []

for progname, test_list in programs.items():
	test_list.sort(key=attrgetter('avg_sq_err'))
	print progname
	print "av hr\tav err\tav sqer\tav sqdm\ttesttype"
	for test in test_list:
		print "%05.2f\t%05.3f\t%05.3f\t%05.3f\t" % \
			(test.avg_hr, test.avg_err, test.avg_sq_err, test.avg_sq_deltamid) + \
			"%05.2f-%05.2f %d %05.2f %s" % test.test_type
	avg_sqe = sum(t.avg_sq_err for t in test_list) / len(test_list)
	program_avg_sqe.append((avg_sqe, progname))
	print "average square error overall: %5.2f" % avg_sqe
	print

program_avg_sqe.sort()
for avg_sqe, progname in program_avg_sqe:
	print "%5.2f\t%s" % (avg_sqe, progname)

