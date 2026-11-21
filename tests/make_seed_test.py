"""End-to-end tests of MakeSeed, the window's one call that makes a seed folder, through kaepora-make-seed.

Usage: python3 -I tests/make_seed_test.py <kaepora-make-seed> <kaepora-seedgen> <kaepora-patch> <game.nup> \
           [test name ...]
"""

import csv
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
        settings = json.dumps({"Shop goods": "Five per shop shuffled"})
        result = subprocess.run([SEEDGEN, "19", spoiler, plan, settings], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        result = subprocess.run([PATCH, NUP, plan, mod], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(len(json.loads(plan.read_text())["shops"]), 40)

    def test_a_seed_with_gifts_and_shop_goods_shuffled_is_patched(self):
        # Seed 32 sells the Giant's Wallet and the Light Arrows, which only gifts hold in the game, in shop slots.
        spoiler, plan, mod = self.tmp / "spoiler.json", self.tmp / "plan.json", self.tmp / "mod"
        settings = json.dumps({"Shop goods": "Four per shop shuffled", "Gifts": "Shuffled"})
        result = subprocess.run([SEEDGEN, "32", spoiler, plan, settings], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        result = subprocess.run([PATCH, NUP, plan, mod], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        sold = {entry["item"] for entry in json.loads(plan.read_text())["shops"]}
        for item in ("Item_Wallet_Large", "Item_Arrow_Light"):
            self.assertIn(item, sold)
            self.assertTrue((mod / f"romfs/Pack/Actor/Shop_{item}.pack.zs").is_file(), item)

    def test_a_seed_with_gold_skulltulas_shuffled_is_patched(self):
        spoiler, plan, mod = self.tmp / "spoiler.json", self.tmp / "plan.json", self.tmp / "mod"
        settings = json.dumps({"Gold Skulltulas": "All"})
        result = subprocess.run([SEEDGEN, "21", spoiler, plan, settings], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        result = subprocess.run([PATCH, NUP, plan, mod], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        skulltulas = json.loads(plan.read_text())["skulltulas"]
        copies = {f"Obj_Token_Gold_{entry['item']}.pack.zs" for entry in skulltulas
                  if entry["item"] != "Item_Token_Gold"}
        self.assertEqual(len(skulltulas), 100)
        self.assertEqual({p.name for p in (mod / "romfs" / "Pack" / "Actor").glob("Obj_Token_Gold_*")}, copies)

    def test_a_seed_with_loose_items_shuffled_is_patched(self):
        spoiler, plan, mod = self.tmp / "spoiler.json", self.tmp / "plan.json", self.tmp / "mod"
        settings = json.dumps({"Loose items": "Shuffled"})
        result = subprocess.run([SEEDGEN, "21", spoiler, plan, settings], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        result = subprocess.run([PATCH, NUP, plan, mod], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(json.loads(plan.read_text())["loose_items"])

    def test_a_seed_with_songs_shuffled_is_patched(self):
        spoiler, plan, mod = self.tmp / "spoiler.json", self.tmp / "plan.json", self.tmp / "mod"
        settings = json.dumps({"Songs": "Shuffled among themselves"})
        result = subprocess.run([SEEDGEN, "24", spoiler, plan, settings], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        result = subprocess.run([PATCH, NUP, plan, mod], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(len(json.loads(plan.read_text())["event_numbers"]), 26)

    def test_a_seed_with_dungeon_rewards_shuffled_is_patched(self):
        # A scene after a boss, or Rauru's in the Chamber of Sages, that gives another item than the game's is written.
        spoiler, plan, mod = self.tmp / "spoiler.json", self.tmp / "plan.json", self.tmp / "mod"
        settings = json.dumps({"Dungeon rewards": "Shuffled", "Gifts": "Shuffled"})
        result = subprocess.run([SEEDGEN, "31", spoiler, plan, settings], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        result = subprocess.run([PATCH, NUP, plan, mod], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        with open(pathlib.Path(__file__).resolve().parent.parent / "remake" / "rewards.tsv", newline="") as f:
            own = {row["flow"]: row["item"] for row in csv.DictReader(f, delimiter="\t")}
        placed = {entry["flow"]: entry["text"] for entry in json.loads(plan.read_text())["event_texts"]
                  if entry["flow"] in own}
        changed = {flow for flow, item in placed.items() if item != own[flow]}
        self.assertEqual(placed.keys(), own.keys())
        self.assertTrue(changed)
        self.assertEqual({flow for flow in own if (mod / "romfs" / flow).is_file()}, changed)

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
