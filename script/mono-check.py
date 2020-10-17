#!/usr/bin/env python3

import argparse
import re
import sys

# (1601712265.251039) can0 042##10000000000000000

try:
	parser = argparse.ArgumentParser(description="check for timestamp monotony")
	parser.add_argument("--continue-on-error", default=False, action="store_true")
	parser.add_argument("file", metavar="FILE", nargs='?', type=argparse.FileType('r'), default=sys.stdin, help="read from FILE, else STDIN")
	args = parser.parse_args()

	last_ts = None

	line_regex = re.compile("^\s*\(([+-]?\d+\.\d*)\)\s+")
	lineno = 0
	for line in args.file:
		lineno += 1

		m = line_regex.match(line)
		if None is m:
			sys.stderr.write(f"malformed line {lineno}: '{line}'\n")
			sys.exit(-1)


		ts = float(m.group(1))

		if ts < 0:
			sys.stderr.write(f"line {lineno}: {ts}\n")
			sys.exit(1)

		if None is last_ts:
			last_ts = ts
		else:
			if ts < last_ts:
				sys.stderr.write(f"line {lineno-1}-{lineno}: {last_ts} > {ts}\n")
				if not args.continue_on_error:
					sys.exit(1)
			else:
				last_ts = ts

except KeyboardInterrupt:
	pass
