#!/usr/bin/env python3
"""rt950_sat.py - command-line entry point of the satellite tool.

The implementation lives in pc/rt950_toolkit/sat.py (shared with the
RT-950 Toolkit desktop application).  Usage: python rt950_sat.py --help
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "pc"))
from rt950_toolkit.sat import main  # noqa: E402

if __name__ == "__main__":
    sys.exit(main())
