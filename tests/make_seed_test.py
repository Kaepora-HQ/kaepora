"""End-to-end tests of MakeSeed, the window's one call that makes a seed folder, through kaepora-make-seed.

Usage: python3 -I tests/make_seed_test.py <kaepora-make-seed> <kaepora-seedgen> <kaepora-patch> <game.nup> \
           [test name ...]
"""

import json
import pathlib
import subprocess
import sys
import tempfile
import unittest

MAKE_SEED = SEEDGEN = PATCH = NUP = None


def files(folder):
    return {p.relative_to(folder).as_posix(): p.read_bytes() for p in sorted(folder.rglob("*")) if p.is_file()}


class MakeSeedTest(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.tmp = pathlib.Path(tmp.name)

    def make_seeds(self, *seeds):
        """Makes each (seed, folder) in one process."""
        args = [arg for seed, folder in seeds for arg in (seed, NUP, folder)]
        return subprocess.run([MAKE_SEED, *args], capture_output=True, text=True)

    def test_same_files_as_the_two_programs(self):
        spoiler, plan, mod = self.tmp / "spoiler.json", self.tmp / "plan.json", self.tmp / "mod"
        result = subprocess.run([SEEDGEN, "7", spoiler, plan], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        result = subprocess.run([PATCH, NUP, plan, mod], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)

        result = self.make_seeds(("7", self.tmp / "seed"))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(files(self.tmp / "seed"), {**files(mod), "spoiler.json": spoiler.read_bytes()})

    def test_a_seed_with_shop_goods_shuffled_is_patched(self):
        spoiler, plan, mod = self.tmp / "spoiler.json", self.tmp / "plan.json", self.tmp / "mod"
        settings = json.dumps({"Shop goods": "Four per shop shuffled"})
        result = subprocess.run([SEEDGEN, "19", spoiler, plan, settings], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        result = subprocess.run([PATCH, NUP, plan, mod], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(len(json.loads(plan.read_text())["shops"]), 32)

    def test_two_seeds_in_one_process(self):
        for seed in ["7", "8"]:
            result = self.make_seeds((seed, self.tmp / "alone" / seed))
            self.assertEqual(result.returncode, 0, result.stderr)
        result = self.make_seeds(("7", self.tmp / "together" / "7"), ("8", self.tmp / "together" / "8"))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(files(self.tmp / "together"), files(self.tmp / "alone"))

    def test_same_seed_twice_in_one_process(self):
        result = self.make_seeds(("7", self.tmp / "first"), ("7", self.tmp / "second"))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(files(self.tmp / "second"), files(self.tmp / "first"))

    def test_existing_folder_is_refused(self):
        seed = self.tmp / "seed"
        seed.mkdir()
        (seed / "kept").write_text("kept")
        result = self.make_seeds(("7", seed))
        self.assertEqual(result.returncode, 1)
        self.assertEqual(result.stderr, f"{seed}: already exists\n")
        self.assertEqual(files(seed), {"kept": b"kept"})

    def test_new_folder_in_a_missing_folder(self):
        seed = self.tmp / "library" / "seeds" / "7"
        result = self.make_seeds(("7", seed))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue((seed / "spoiler.json").is_file())
        self.assertTrue(any(name.startswith("romfs/") for name in files(seed)))


if __name__ == "__main__":
    MAKE_SEED, SEEDGEN, PATCH, NUP = sys.argv[1:5]
    unittest.main(argv=[sys.argv[0], *sys.argv[5:]])
