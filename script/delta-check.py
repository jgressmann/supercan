#!/usr/bin/env python3

import argparse
import fileinput
import re
import sys

# (1601712265.251039) can0 042##10000000000000000

try:

	parser = argparse.ArgumentParser(description="check for of out of bounds timestamp deltas")
	parser.add_argument("--interval-ms", metavar="N", required=True, type=int, help="interval between messages in milliseconds")
	parser.add_argument("--threshold-ms", metavar="N", default=1, type=int, help="tolerable jitter in milliseconds")
	# parser.add_argument("-q", "--quiet", default=False, action="store_true", help="suppress output")
	parser.add_argument("--continue-on-error", default=False, action="store_true")
	parser.add_argument("file", metavar="FILE", nargs='?', type=argparse.FileType('r'), default=sys.stdin, help="read from FILE, else STDIN")

	args = parser.parse_args()
	if args.interval_ms <= 0:
		raise ValueError('argument to --interval-ms must be positive')

	if args.threshold_ms <= 0:
		raise ValueError('argument to --threshold-ms must be positive')

	if args.threshold_ms >= args.interval_ms:
		raise ValueError('tolerance must not exceed interval')


	last_ts = None

	line_regex = re.compile("^\s*\(([+-]?\d+\.\d*)\)\s+")
	lineno = 0
	for line in args.file:
		lineno += 1

		m = line_regex.match(line)
		if None is m:
			sys.stderr.write(f"malformed line {lineno}: '{line}'\n")
			sys.exit(-1)


		ts = float(m.group(1)) * 1000

		if None is last_ts:
			last_ts = ts
		else:
			target = args.interval_ms + last_ts
			delta = target - ts
			last_ts = ts

			if (delta < 0 and -delta > args.threshold_ms) or (delta >= 0 and delta > args.threshold_ms):
				sys.stderr.write(f"line {lineno-1}-{lineno}: {abs(delta)} > {args.threshold_ms} [ms]\n")
				if not args.continue_on_error:
					sys.exit(1)
except KeyboardInterrupt:
	pass
