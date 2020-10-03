#!/usr/bin/env python3

import fileinput
import re
import sys

# (1601712265.251039) can0 042##10000000000000000

last_ts = None

line_regex = re.compile("^\((\d+\.\d*)\)\s+")
lineno = 0
for line in fileinput.input():
	lineno += 1

	m = line_regex.match(line)
	if None is m:
		sys.stderr.write(f"malformed line {lineno}: '{line}'")
		sys.exit(-1)


	ts = float(m.group(1))


	if None is last_ts:
		last_ts = ts
	else:
		if ts < last_ts:
			sys.stderr.write(f"line {lineno-1}-{lineno}: {last_ts} > {ts}'")
			sys.exit(1)
		else:
			last_ts = ts
