"""End-to-end tests of kaepora-seedgen against the tables in remake/.

Usage: python3 -I -B tests/seedgen_test.py <kaepora-seedgen> <kaepora-reach> <Kaepora's version> [test name ...]
Writes its seeds into a temporary folder under the current directory.
"""

import collections
import csv
import json
import pathlib
import re
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import reach  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parent.parent
FIXTURES = ROOT / "tests" / "fixtures"
FIXED_SEED = "6332265119"
SEEDGEN = None
VERSION = None


def read_tsv(name):
    with open(ROOT / "remake" / name, newline="") as f:
        return list(csv.DictReader(f, delimiter="\t"))


SHUFFLED = [row for row in read_tsv("chests.tsv") if row["shuffled"] == "yes"]
# What each shuffled location holds in the unmodified game. A location whose chest the files keep twice counts once.
POOL = collections.Counter({row["location"]: row["item"] for row in SHUFFLED}.values())
ITEM_PAIRS = {(row["remake_item"], row["soh_item"]) for row in read_tsv("items.tsv")}
DUNGEONS = ["Deku Tree", "Dodongo's Cavern", "Jabu-Jabu's Belly", "Forest Temple", "Fire Temple", "Water Temple",
            "Shadow Temple", "Spirit Temple", "Bottom of the Well", "Ice Cavern", "Gerudo Training Ground",
            "Ganon's Castle"]
DEFAULT_SETTINGS = {"Shuffled dungeons": DUNGEONS, "Small and boss keys": "Anywhere in their dungeon",
                    "Maps and compasses": "Anywhere in their dungeon", "Lens of Truth": "Needed"}
KEYS = {"Item_Dungeon_SmallKey", "Item_Dungeon_BossKey"}
MAPS_AND_COMPASSES = {"Item_Dungeon_Map", "Item_Dungeon_Compass"}
GOLDEN_GAUNTLETS_CHEST = "Ganon's Castle Shadow Trial Golden Gauntlets Chest"
EVERY_KEY = [("Link's Pocket", item) for item in (
    *(f"{dungeon} Key Ring" for dungeon in ("Forest Temple", "Fire Temple", "Water Temple", "Spirit Temple",
                                            "Shadow Temple", "Bottom of the Well", "Training Ground",
                                            "Gerudo Fortress", "Ganon's Castle", "Chest Game")),
    *(f"{dungeon} Boss Key" for dungeon in ("Forest Temple", "Fire Temple", "Water Temple", "Spirit Temple",
                                            "Shadow Temple", "Ganon's Castle")))]
# The logic asks for a place to buy bombchus before Link uses one, and the game does not. Both ask for the bomb bag.
BOMBCHUS = [("Link's Pocket", "Buy Bombchu (10)"), ("Link's Pocket", "Progressive Bomb Bag")]
# The game pays each chest of these locations on its own.
KEPT_TWICE = {location for location, rows in collections.Counter(row["location"] for row in SHUFFLED).items()
              if rows > 1}


def folder(path):
    return path.split("/")[1]


def without_version(log):
    """The spoiler log's text without the line of Kaepora's version, which changes with every release."""
    return re.sub(rb'^    "kaepora": ".*",\n', b"", log, count=1, flags=re.MULTILINE)


def playthrough_steps(log):
    return {location: item for sphere in log["playthrough"].values() for location, item in sphere.items()}


class SeedgenTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(dir=".")
        self.addCleanup(self.tmp.cleanup)

    def run_seedgen(self, seed, settings=None):
        out = pathlib.Path(self.tmp.name) / seed
        out.mkdir(exist_ok=True)
        spoiler, plan = out / "spoiler.json", out / "plan.json"
        args = [] if settings is None else [json.dumps(settings)]
        result = subprocess.run([SEEDGEN, seed, spoiler, plan, *args], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        return spoiler.read_bytes(), plan.read_bytes()

    def generate(self, seed, settings=None):
        spoiler, plan = self.run_seedgen(seed, settings)
        return json.loads(spoiler), json.loads(plan)

    def assert_kept_in_place(self, kept, settings, seeds, *, on_route):
        rows = {row["hash"]: row for row in SHUFFLED}
        kept_locations = {row["location"] for row in kept}
        pool = collections.Counter({row["location"]: row["item"] for row in SHUFFLED
                                    if row["location"] not in kept_locations}.values())
        items = {row["location"]: row["item"] for row in kept}
        stepped = set()
        for seed in seeds:
            spoiler, plan = self.generate(seed, settings)
            planned = {rows[entry["hash"]]["location"]: entry["drop_actor"] for entry in plan["chests"]}
            self.assertFalse(kept_locations & planned.keys())
            self.assertFalse(kept_locations & spoiler["locations"].keys())
            self.assertEqual(collections.Counter(planned.values()), pool)
            for location, item in playthrough_steps(spoiler).items():
                if location in items:
                    self.assertIn((items[location], item), ITEM_PAIRS, location)
                    stepped.add(location)
        self.assertEqual(bool(stepped), on_route)

    def test_only_shuffled_chests_change(self):
        first, _ = self.generate("1")
        second, _ = self.generate("2")
        shuffled = {row["location"] for row in SHUFFLED}
        self.assertEqual(first["locations"].keys(), second["locations"].keys())
        self.assertLessEqual(shuffled, first["locations"].keys())
        # The log names an unshuffled location only where the playthrough steps on it.
        first_steps, second_steps = playthrough_steps(first), playthrough_steps(second)
        unshuffled = (first_steps.keys() & second_steps.keys()) - shuffled
        self.assertTrue(unshuffled)
        for location in unshuffled:
            self.assertEqual(first_steps[location], second_steps[location], location)
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

    def test_spoiler_log_lists_the_shuffled_locations_and_the_route(self):
        spoiler, _ = self.generate("KAEPORA")
        self.assertEqual(list(spoiler), ["kaepora", "seed", "settings", "locations", "playthrough"])
        self.assertEqual(spoiler["kaepora"], VERSION)
        self.assertEqual(spoiler["seed"], "KAEPORA")
        self.assertEqual(spoiler["settings"], DEFAULT_SETTINGS)
        self.assertEqual(list(spoiler["settings"]), list(DEFAULT_SETTINGS))
        # A location whose chest the files keep twice is listed once.
        self.assertEqual(list(spoiler["locations"]), list(dict.fromkeys(row["location"] for row in SHUFFLED)))
        self.assertEqual(list(spoiler["playthrough"].values())[-1], {"Ganon": "Triforce"})

    def test_plan_opens_the_doors_the_rules_count_open(self):
        # The Fire Temple's lowest locked door and the Water Temple's west middle-floor door.
        _, plan = self.generate("6")
        self.assertEqual(plan["start_flags"], ["IsChainLockDoorOpen.4189300454150217955",
                                               "IsChainLockDoorOpen.2202807660383340401"])

    def test_plan_keeps_quests_from_going_back(self):
        escape = "Event/EventFlow/Dm_SY_BancChange_EscapeDungeon.bfevfl.zs"
        water = "Event/EventFlow/Get_StoneOfWater.bfevfl.zs"
        malon = "Event/EventFlow/Npc_Hylia_Malon_Adult.bfevfl.zs"
        _, plan = self.generate("12")
        self.assertEqual(plan["event_texts"], [
            {"flow": escape, "event": "Event4", "param": "ChallengeName", "text": "Get_StoneOfForest"},
            {"flow": escape, "event": "Event4", "param": "StepName", "text": "Complete"},
            {"flow": water, "event": "Event80", "param": "ChallengeName", "text": "Get_StoneOfFire"},
            {"flow": water, "event": "Event80", "param": "StepName", "text": "Complete"},
            {"flow": malon, "event": "Event179", "param": "GameDataName", "text": "Get_Epona_IsAfter_Ready_Exp"}])

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

    def test_the_crate_heart_piece_and_the_training_ground_key_are_shuffled(self):
        # SoH counts both as loose items. The remake keeps each in a chest.
        chests = {"GV Crate Freestanding PoH": "15856256242099990922",
                  "Gerudo Training Ground Freestanding Key": "1883068192328530224"}
        held = collections.defaultdict(set)
        for seed in range(220, 230):
            spoiler, plan = self.generate(str(seed))
            self.assertLessEqual(set(chests.values()), {entry["hash"] for entry in plan["chests"]})
            for location in chests:
                held[location].add(spoiler["locations"][location])
        for location in chests:
            self.assertGreater(len(held[location]), 1, location)

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

    def test_spoiler_log_records_the_settings(self):
        settings = {"Shuffled dungeons": ["Fire Temple", "Deku Tree"], "Small and boss keys": "In their own chests",
                    "Maps and compasses": "In their own chests", "Lens of Truth": "Never needed"}
        spoiler, _ = self.generate("9", settings)
        self.assertEqual(spoiler["settings"], {**settings, "Shuffled dungeons": ["Deku Tree", "Fire Temple"]})
        self.assertEqual(list(spoiler), ["kaepora", "seed", "settings", "locations", "playthrough"])

    def test_default_settings_are_the_settings_left_out(self):
        self.assertEqual(self.run_seedgen("10", DEFAULT_SETTINGS), self.run_seedgen("10"))
        self.assertEqual(self.run_seedgen("10", {}), self.run_seedgen("10"))

    def test_an_unknown_setting_is_refused(self):
        out = pathlib.Path(self.tmp.name)
        for settings, message in (({"Lens of Truth": "Sometimes"}, "Sometimes"),
                                  ({"Shuffled dungeons": ["Deku Tree", "Hyrule Castle"]}, "Hyrule Castle"),
                                  ({"Forest": "Open"}, "Forest")):
            result = subprocess.run([SEEDGEN, "11", out / "spoiler.json", out / "plan.json", json.dumps(settings)],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 1)
            self.assertIn(message, result.stderr)
            self.assertFalse((out / "spoiler.json").exists())

    def test_a_dungeon_left_out_keeps_its_chests(self):
        left_out = {"Forest Temple": "ForestTemple", "Deku Tree": "InsideTheDekuTree"}
        kept = [row for row in SHUFFLED if folder(row["files"]) in left_out.values()]
        self.assertTrue(kept)
        settings = {"Shuffled dungeons": [name for name in DUNGEONS if name not in left_out]}
        self.assert_kept_in_place(kept, settings, [str(seed) for seed in range(300, 310)], on_route=True)

    def test_keys_stay_in_their_own_chests(self):
        kept = [row for row in SHUFFLED if row["item"] in KEYS]
        self.assertTrue(kept)
        self.assert_kept_in_place(kept, {"Small and boss keys": "In their own chests"},
                                  [str(seed) for seed in range(310, 320)], on_route=True)

    def test_maps_and_compasses_stay_in_their_own_chests(self):
        kept = [row for row in SHUFFLED if row["item"] in MAPS_AND_COMPASSES]
        self.assertTrue(kept)
        self.assert_kept_in_place(kept, {"Maps and compasses": "In their own chests"},
                                  [str(seed) for seed in range(320, 330)], on_route=False)

    def test_lens_never_needed_leaves_the_lens_off_the_route(self):
        for seed in range(330, 345):
            needed, _ = self.generate(str(seed))
            never, _ = self.generate(str(seed), {"Lens of Truth": "Never needed"})
            self.assertIn("Lens of Truth", playthrough_steps(needed).values())
            self.assertNotIn("Lens of Truth", playthrough_steps(never).values())
            self.assertIn("Bongo Bongo", playthrough_steps(never))

    def test_every_setting_changed_at_once(self):
        settings = {"Shuffled dungeons": [name for name in DUNGEONS if name not in ("Water Temple", "Ice Cavern")],
                    "Small and boss keys": "In their own chests", "Maps and compasses": "In their own chests",
                    "Lens of Truth": "Never needed"}
        for seed in range(400, 500):
            spoiler, _ = self.generate(str(seed), settings)
            self.assertNotIn("Lens of Truth", playthrough_steps(spoiler).values())

    def test_strength_items_come_in_rising_order(self):
        no_bracelet = ("GC Darunias Joy", "Green Rupee")
        bracelet_at_start = ("Link's Pocket", "Goron's Bracelet")

        def without(*left_out):
            return {"Shuffled dungeons": [name for name in DUNGEONS if name not in left_out]}

        for seed, settings in (("5029", {}), ("5032", {}), ("5038", {}), ("5022", {}), ("70217", {}), ("5015", {}),
                               ("947011", {"Shuffled dungeons": ["Ganon's Castle"]}),
                               ("5107", without("Spirit Temple")),
                               ("75021", {"Small and boss keys": "In their own chests"}),
                               ("73421", {**without("Dodongo's Cavern", "Water Temple", "Shadow Temple",
                                                    "Spirit Temple", "Ice Cavern"),
                                          "Small and boss keys": "In their own chests",
                                          "Maps and compasses": "In their own chests"}),
                               ("5002", without("Ganon's Castle"))):
            spoiler, _ = self.generate(seed, settings)
            placed = list(spoiler["locations"].items())
            where = {item: location for location, item in placed}
            silver = where["Silver Gauntlets"]
            golden = where.get("Golden Gauntlets", GOLDEN_GAUNTLETS_CHEST)
            self.assertEqual("Golden Gauntlets" in where,
                             "Ganon's Castle" in settings.get("Shuffled dungeons", DUNGEONS))
            self.assertTrue(reach.reachable(golden, *placed, settings=settings), seed)
            for held in ([], EVERY_KEY + BOMBCHUS):
                self.assertFalse(reach.reachable(silver, *placed, no_bracelet, *held, settings=settings), seed)
                self.assertFalse(reach.reachable(golden, *placed, no_bracelet, *held, settings=settings), seed)
                self.assertFalse(reach.reachable(golden, *placed, (silver, "Green Rupee"), bracelet_at_start, *held,
                                                 settings=settings), seed)
            self.assertFalse({silver, golden} & KEPT_TWICE, seed)
            spheres = {item: sphere for sphere, steps in spoiler["playthrough"].items() for item in steps.values()}
            self.assertLess(spheres["Goron's Bracelet"], spheres["Silver Gauntlets"], seed)
            self.assertLess(spheres["Silver Gauntlets"], spheres["Golden Gauntlets"], seed)

    def test_seed_text_ignores_case(self):
        upper = self.run_seedgen("OWL")
        self.assertEqual(self.run_seedgen("owl"), upper)
        self.assertEqual(self.run_seedgen("oWl"), upper)
        spoiler, plan = map(json.loads, upper)
        self.assertEqual((spoiler["seed"], plan["seed"]), ("OWL", "OWL"))

    def test_fixed_seed_matches_fixture(self):
        spoiler, plan = self.run_seedgen(FIXED_SEED)
        self.assertEqual(without_version(spoiler),
                         without_version((FIXTURES / f"spoiler-{FIXED_SEED}.json").read_bytes()))
        self.assertEqual(plan, (FIXTURES / f"plan-{FIXED_SEED}.json").read_bytes())


if __name__ == "__main__":
    SEEDGEN, reach.PROGRAM, VERSION = sys.argv[1:4]
    unittest.main(argv=[sys.argv[0], *sys.argv[4:]])
