#!/usr/bin/env python3
"""Refuse a gate that cannot run here, instead of reporting success.

A gate has exactly two honest outcomes: it ran and passed, or it did not
pass.  "It could not run on this host" is the second, not the first.

This exists because the third outcome kept being chosen by accident.  A
Makefile branch that printed "run this from WSL" and exited 0 meant a
whole tree went unswept while `make` reported green, and that green was
then counted as evidence that a change was safe.  Printing a message is
not a mitigation: nobody reads a message attached to a success.
"""
import sys

if len(sys.argv) < 3:
    sys.stderr.write("usage: gate-unavailable.py <target> <reason...>\n")
    sys.exit(2)

target = sys.argv[1]
reason = " ".join(sys.argv[2:])
sys.stderr.write("%s: UNAVAILABLE on this host -- %s\n" % (target, reason))
sys.stderr.write("%s: refusing to exit 0; a gate that did not run has not passed.\n" % target)
sys.exit(1)
