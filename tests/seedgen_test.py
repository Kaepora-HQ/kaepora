"""End-to-end tests of kaepora-seedgen against the tables in remake/.

Usage: python3 -I tests/seedgen_test.py <kaepora-seedgen> [test name ...]
Writes its seeds into a temporary folder under the current directory.
"""

import collections
import csv
import json
import pathlib
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent
FIXTURES = ROOT / "tests" / "fixtures"
FIXED_SEED = "6332265119"
SEEDGEN = None


def read_tsv(name):
    with open(ROOT / "remake" / name, newline="") as f:
        return list(csv.DictReader(f, delimiter="\t"))


SHUFFLED = [row for row in read_tsv("chests.tsv") if row["shuffled"] == "yes"]
# What each shuffled location holds in the unmodified game. A location whose chest the files keep twice counts once.
POOL = collections.Counter({row["location"]: row["item"] for row in SHUFFLED}.values())
ITEM_PAIRS = {(row["remake_item"], row["soh_item"]) for row in read_tsv("items.tsv")}


def folder(path):
    return path.split("/")[1]


class SeedgenTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(dir=".")
        self.addCleanup(self.tmp.cleanup)

    def run_seedgen(self, seed):
        out = pathlib.Path(self.tmp.name) / seed
        out.mkdir(exist_ok=True)
        spoiler, plan = out / "spoiler.json", out / "plan.json"
        result = subprocess.run([SEEDGEN, seed, spoiler, plan], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        return spoiler.read_bytes(), plan.read_bytes()

    def generate(self, seed):
        spoiler, plan = self.run_seedgen(seed)
        return json.loads(spoiler), json.loads(plan)

    def test_only_shuffled_chests_change(self):
        first, _ = self.generate("1")
        second, _ = self.generate("2")
        shuffled = {row["location"] for row in SHUFFLED}
        self.assertEqual(first["locations"].keys(), second["locations"].keys())
        self.assertLessEqual(shuffled, first["locations"].keys())
        for location, item in first["locations"].items():
            if location not in shuffled:
                self.assertEqual(item, second["locations"][location], location)
        self.assertTrue(any(first["locations"][name] != second["locations"][name] for name in shuffled))

    def test_plan_lists_each_shuffled_chest(self):
        _, plan = self.generate("3")
        self.assertEqual(plan["seed"], "3")
        planned = {entry["hash"]: entry for entry in plan["chests"]}
        self.assertEqual(len(planned), len(plan["chests"]))
        self.assertEqual(planned.keys(), {row["hash"] for row in SHUFFLED})
        for row in SHUFFLED:
            self.assertEqual(planned[row["hash"]]["files"], row["files"].split(","))
        # A location whose chest the files keep twice writes its one item to both, so names count once per location.
        rows = {row["hash"]: row for row in SHUFFLED}
        by_location = {rows[entry["hash"]]["location"]: entry["drop_actor"] for entry in plan["chests"]}
        self.assertEqual(collections.Counter(by_location.values()), POOL)

    def test_plan_matches_spoiler(self):
        spoiler, plan = self.generate("4")
        rows = {row["hash"]: row for row in SHUFFLED}
        for entry in plan["chests"]:
            location = rows[entry["hash"]]["location"]
            self.assertIn((entry["drop_actor"], spoiler["locations"][location]), ITEM_PAIRS, location)
        # A remake dungeon item names no dungeon, so it must stay in the dungeon it came from.
        dungeon_names = {name for name, _ in ITEM_PAIRS if sum(pair[0] == name for pair in ITEM_PAIRS) > 1}
        self.assertEqual(
            collections.Counter((folder(e["files"][0]), e["drop_actor"]) for e in plan["chests"]
                                if e["drop_actor"] in dungeon_names),
            collections.Counter((folder(row["files"]), row["item"]) for row in SHUFFLED
                                if row["item"] in dungeon_names))

    def test_plan_opens_the_doors_the_rules_count_open(self):
        # The Fire Temple's lowest locked door and the Water Temple's west middle-floor door.
        _, plan = self.generate("6")
        self.assertEqual(plan["start_flags"], ["IsChainLockDoorOpen.4189300454150217955",
                                               "IsChainLockDoorOpen.2202807660383340401"])

    def test_scrub_seeds_and_the_large_magic_jar_are_placed(self):
        # SoH has neither item. Each shuffled chest that holds one gives it to the pool, named as the game names it.
        spoiler, _ = self.generate("7")
        placed = collections.Counter(spoiler["locations"][location]
                                     for location in {row["location"] for row in SHUFFLED})
        for remake, soh in (("Item_ScrubBusinessSeed", "Business Scrub Seed"),
                            ("Tbox_MagicJar_Large", "Large Magic Jar")):
            self.assertGreater(POOL[remake], 0, remake)
            self.assertEqual(placed[soh], POOL[remake], soh)

    def test_a_chest_kept_twice_holds_one_item(self):
        # The Kakariko lookout chest stands in both versions of the adult village, and the Gerudo Valley storms grotto
        # chest in the child and the adult files, each under its own hash.
        _, plan = self.generate("8")
        names = {entry["hash"]: entry["drop_actor"] for entry in plan["chests"]}
        for first, second in (("13080955777249750889", "11929552275106311978"),
                              ("18413589940135955604", "6205043616869463865")):
            self.assertIn(first, names)
            self.assertEqual(names[first], names[second])

    def test_same_seed_same_files(self):
        self.assertEqual(self.run_seedgen("5"), self.run_seedgen("5"))

    def test_twenty_seeds_generate(self):
        for seed in range(100, 120):
            self.generate(str(seed))

    def test_spirit_dungeon_items_stay_out_of_the_colossus(self):
        # SoH counts these two chests as Spirit Temple locations, but they stand in the Desert Colossus, and the game
        # credits a key, map or compass to the scene Link stands in.
        colossus = ["Spirit Temple Silver Gauntlets Chest", "Spirit Temple Mirror Shield Chest"]
        spirit = {"Spirit Temple Small Key", "Spirit Temple Boss Key", "Spirit Temple Map", "Spirit Temple Compass"}
        placed = collections.Counter()
        for seed in range(200, 220):
            spoiler, _ = self.generate(str(seed))
            placed.update(spoiler["locations"][name] for name in colossus)
        self.assertFalse(spirit & placed.keys())
        # Other items still land there.
        self.assertGreater(len(placed), len(colossus))

    def test_fixed_seed_matches_fixture(self):
        spoiler, plan = self.run_seedgen(FIXED_SEED)
        self.assertEqual(spoiler, (FIXTURES / f"spoiler-{FIXED_SEED}.json").read_bytes())
        self.assertEqual(plan, (FIXTURES / f"plan-{FIXED_SEED}.json").read_bytes())


if __name__ == "__main__":
    SEEDGEN = sys.argv[1]
    unittest.main(argv=[sys.argv[0], *sys.argv[2:]])
