"""End-to-end tests of kaepora-seedgen.

Usage: python3 -I tests/seedgen_test.py <kaepora-seedgen> [test name ...]
Writes its seeds into a temporary folder under the current directory.
"""

import json
import pathlib
import subprocess
import sys
import tempfile
import unittest

SEEDGEN = None


class SeedgenTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(dir=".")
        self.addCleanup(self.tmp.cleanup)

    def run_seedgen(self, seed):
        out = pathlib.Path(self.tmp.name) / seed
        out.mkdir(exist_ok=True)
        spoiler = out / "spoiler.json"
        result = subprocess.run([SEEDGEN, seed, spoiler], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        return spoiler.read_bytes()

    def generate(self, seed):
        return json.loads(self.run_seedgen(seed))

    def test_same_seed_same_files(self):
        self.assertEqual(self.run_seedgen("5"), self.run_seedgen("5"))

    def test_twenty_seeds_generate(self):
        for seed in range(100, 120):
            self.generate(str(seed))


if __name__ == "__main__":
    SEEDGEN = sys.argv[1]
    unittest.main(argv=[sys.argv[0], *sys.argv[2:]])
