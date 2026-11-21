"""Runs one part of the window's cases, so that ctest can run the parts side by side.

Usage: python3 -I tests/window_part.py <kaepora-window-test> <part> <parts>
"""

import subprocess
import sys

program, part, parts = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
listed = subprocess.run([program, "-functions"], capture_output=True, text=True, check=True).stdout.split()
cases = [case.removesuffix("()") for case in listed]
sys.exit(subprocess.run([program, *cases[part - 1::parts]]).returncode)
