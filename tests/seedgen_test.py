"""End-to-end tests of kaepora-seedgen against the tables in remake/.

Usage: python3 -I tests/seedgen_test.py <kaepora-seedgen> [test name ...]
Writes its seeds into a temporary folder under the current directory.
"""

import csv
import json
import pathlib
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent
SEEDGEN = None


def read_tsv(name):
    with open(ROOT / "remake" / name, newline="") as f:
        return list(csv.DictReader(f, delimiter="\t"))


SHUFFLED = [row for row in read_tsv("chests.tsv") if row["shuffled"] == "yes"]


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

    def test_only_shuffled_chests_change(self):
        first = self.generate("1")
        second = self.generate("2")
        shuffled = {row["location"] for row in SHUFFLED}
        self.assertEqual(first["locations"].keys(), second["locations"].keys())
        self.assertLessEqual(shuffled, first["locations"].keys())
        for location, item in first["locations"].items():
            if location not in shuffled:
                self.assertEqual(item, second["locations"][location], location)
        self.assertTrue(any(first["locations"][name] != second["locations"][name] for name in shuffled))

    def test_same_seed_same_files(self):
        self.assertEqual(self.run_seedgen("5"), self.run_seedgen("5"))

    def test_twenty_seeds_generate(self):
        for seed in range(100, 120):
            self.generate(str(seed))


if __name__ == "__main__":
    SEEDGEN = sys.argv[1]
    unittest.main(argv=[sys.argv[0], *sys.argv[2:]])
