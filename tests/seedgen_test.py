"""End-to-end tests of kaepora-seedgen against the tables in remake/.

Usage: python3 -I -B tests/seedgen_test.py <kaepora-seedgen> <Kaepora's version> [test name ...]
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

ROOT = pathlib.Path(__file__).resolve().parent.parent
FIXTURES = ROOT / "tests" / "fixtures"
FIXED_SEED = "6332265119"
SEEDGEN = None
VERSION = None


def read_tsv(name):
    with open(ROOT / "remake" / name, newline="") as f:
        return list(csv.DictReader(f, delimiter="\t"))


SHUFFLED = [row for row in read_tsv("chests.tsv") if row["shuffled"] == "yes"]
FIRST_ROWS = {}
for row in SHUFFLED:
    FIRST_ROWS.setdefault(row["location"], row)
POOL = collections.Counter(row["item"] for row in FIRST_ROWS.values())
# Locations whose chest the files keep twice, under two hashes.
KEPT_TWICE = {location for location, rows in collections.Counter(row["location"] for row in SHUFFLED).items()
              if rows > 1}
ITEM_PAIRS = {(row["remake_item"], row["soh_item"]) for row in read_tsv("items.tsv")}
DUNGEONS = ["Deku Tree", "Dodongo's Cavern", "Jabu-Jabu's Belly", "Forest Temple", "Fire Temple", "Water Temple",
            "Shadow Temple", "Spirit Temple", "Bottom of the Well", "Ice Cavern", "Gerudo Training Ground",
            "Ganon's Castle"]
DEFAULT_SETTINGS = {"Shuffled dungeons": DUNGEONS, "Small and boss keys": "Anywhere in their dungeon",
                    "Maps and compasses": "Anywhere in their dungeon", "Kokiri Forest": "Closed",
                    "Rainbow Bridge": "After the Light Arrows", "Ganon's trials": "All six",
                    "Lens of Truth": "Needed"}
# The Fire Temple's lowest locked door, the Water Temple's west middle-floor door, the west door of the Forest Temple's
# bow chest room and the Spirit Temple's door between the statue room and the child climb room start open in every seed.
DOORS_OPEN = {"IsChainLockDoorOpen.4189300454150217955": True, "IsChainLockDoorOpen.2202807660383340401": True,
              "Gimmick_DefeatEnemy_Open_ForestTemple_03": True, "SpiritTemple_Room07_Unlocked": True}
MAP_DUNGEONS = ["InsideTheDekuTree", "DodongoCavern", "InsideJabuJabuBelly", "ForestTemple", "FireTemple", "WaterTemple",
                "ShadowTemple", "SpiritTemple", "BottomOfTheWell", "IceCavern"]
SMALL_KEY_DOORS = [
    "1163369963105284964", "2293725469462648563", "3225055400115517869", "5481916789431949409", "17124821462463640959",
    "530640766699404174", "8886594954069367001", "8924322743887267313", "17520323088057735070",
    "17586687124735290487", "17649976694470179290", "18033163985131850496",
    "8936252369722474095", "14915175233807466033", "15319553007255647544",
    "3785621893886752067", "7859741725793759796", "14964409186081456860", "15639855627873906140",
    "17286348970163505298",
    "1239725692126790626", "1598277279179137240", "5866554311248263901", "8208400900970984454",
    "14562151304140999752",
    "3180753716225647997", "15964434394101837654",
    "1782957447756991766", "4054769999674403462", "4861722785202029276", "9161635131903213609",
    "11215020957634035781", "11279749999185960905", "13610050855796186379", "13961115460808423078",
    "16099041652987252990",
    "2559901135200913892"]
BOSS_KEY_DUNGEONS = ["ForestTemple", "FireTemple", "WaterTemple", "ShadowTemple", "SpiritTemple", "InsideGanonCastle"]
KEYS = {"Item_Dungeon_SmallKey", "Item_Dungeon_BossKey"}
MAPS_AND_COMPASSES = {"Item_Dungeon_Map", "Item_Dungeon_Compass"}


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
        pool = collections.Counter(row["item"] for location, row in FIRST_ROWS.items()
                                   if location not in kept_locations)
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

    def test_route_names_no_gold_skulltula_token(self):
        # Any ten tokens buy the same Skulltula House reward, so the route names no token.
        self.assertNotIn("Gold Skulltula Token", playthrough_steps(self.generate(FIXED_SEED)[0]).values())

    def test_route_takes_what_link_is_handed_over_what_he_buys(self):
        # The stored seed's route needs a Goron Tunic, which the rolling Goron gives adult Link. The Goron City shop sells
        # one for 600 rupees, which take both wallets the Skulltula House gives.
        steps = playthrough_steps(self.generate(FIXED_SEED)[0])
        self.assertEqual(steps["GC Rolling Goron as Adult"], "Goron Tunic")
        self.assertNotIn("GC Shop Item 5", steps)
        self.assertNotIn("Kak 10 Gold Skulltula Reward", steps)
        self.assertNotIn("Kak 30 Gold Skulltula Reward", steps)

    def test_plan_opens_the_doors_the_rules_count_open(self):
        _, plan = self.generate("6")
        self.assertEqual(plan["start_values"], DOORS_OPEN)

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
        _, plan = self.generate("8")
        names = {entry["hash"]: entry["drop_actor"] for entry in plan["chests"]}
        for location in KEPT_TWICE:
            first, second = (row["hash"] for row in SHUFFLED if row["location"] == location)
            self.assertEqual(names[first], names[second], location)

    def test_loose_items_the_remake_keeps_in_chests_are_shuffled(self):
        chests = {"GV Crate Freestanding PoH": "15856256242099990922",
                  "Gerudo Training Ground Freestanding Key": "1883068192328530224",
                  "Graveyard Dampe Race Freestanding PoH": "5534919361313859554"}
        held = collections.defaultdict(set)
        for seed in range(220, 230):
            spoiler, plan = self.generate(str(seed))
            self.assertLessEqual(set(chests.values()), {entry["hash"] for entry in plan["chests"]})
            for location in chests:
                held[location].add(spoiler["locations"][location])
        for location in chests:
            self.assertGreater(len(held[location]), 1, location)

    def test_dins_fire_comes_from_the_summit_great_fairy(self):
        # She gives Din's Fire with the magic meter. The castle's Great Fairy gives nothing until Link holds it.
        steps = playthrough_steps(self.generate("9")[0])
        self.assertEqual(steps.get("DMT Great Fairy Din's Fire"), "Din's Fire")

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
        self.assertEqual(spoiler["settings"],
                         DEFAULT_SETTINGS | settings | {"Shuffled dungeons": ["Deku Tree", "Fire Temple"]})
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

    def test_forest_open(self):
        spoiler, plan = self.generate("16", {"Kokiri Forest": "Open"})
        self.assertEqual(spoiler["settings"]["Kokiri Forest"], "Open")
        self.assertEqual(plan["start_values"], DOORS_OPEN | {"KokiriForest_Npc_Kokiri_Mido_Child_Blocker": False,
                                                             "KokiriForest_Npc_Kokiri_007_Child_Blocker_00": False})

    def test_bridge_always_open(self):
        spoiler, plan = self.generate("17", {"Rainbow Bridge": "Always open"})
        self.assertEqual(spoiler["settings"]["Rainbow Bridge"], "Always open")
        self.assertEqual(plan["start_values"], DOORS_OPEN | {"HavePlayedEvent.GanonCastle_Adult_Field_Rainbow": True})

    def test_no_trials(self):
        spoiler, plan = self.generate("18", {"Ganon's trials": "None"})
        self.assertEqual(spoiler["settings"]["Ganon's trials"], "None")
        trials = ("Forest", "Water", "Shadow", "Fire", "Light", "Spirit")
        done = {"InsideGanonCastle_Dungeon_Barrier_Count": 0, "InsideGanonCastle_IsAllBarrierClear": True,
                "HavePlayedEvent.Dm_InsideGanonCastle_0090": True, "InsideGanonCastle_Dungeon_SeeBarrierGuide": True}
        for trial in trials:
            done[f"InsideGanonCastle_Dungeon_BarrierRemove_{trial}"] = True
            done[f"HavePlayedEvent.InsideGanonCastle_Dungeon_BarrierRemove_{trial}"] = True
        self.assertEqual(plan["start_values"], DOORS_OPEN | done)

    def test_keys_not_needed(self):
        spoiler, plan = self.generate("15", {"Small and boss keys": "Not needed"})
        self.assertEqual(spoiler["settings"]["Small and boss keys"], "Not needed")
        doors = {f"IsChainLockDoorOpen.{door}": True for door in SMALL_KEY_DOORS}
        boss_keys = {f"DungeonInfo.{dungeon}.IsGetBossKey": True for dungeon in BOSS_KEY_DUNGEONS}
        self.assertEqual(len(doors), 37)
        self.assertEqual(plan["start_values"], DOORS_OPEN | doors | boss_keys)
        self.assertEqual({entry["hash"] for entry in plan["chests"]}, {row["hash"] for row in SHUFFLED})

    def test_maps_and_compasses_given_at_the_start(self):
        spoiler, plan = self.generate("14", {"Maps and compasses": "Given at the start"})
        self.assertEqual(spoiler["settings"]["Maps and compasses"], "Given at the start")
        given = {f"DungeonInfo.{dungeon}.{value}": True for dungeon in MAP_DUNGEONS
                 for value in ("IsGetMap", "IsGetCompass")}
        self.assertEqual(plan["start_values"], DOORS_OPEN | given)
        self.assertEqual({entry["hash"] for entry in plan["chests"]}, {row["hash"] for row in SHUFFLED})

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
                    "Kokiri Forest": "Open", "Rainbow Bridge": "Always open", "Ganon's trials": "None",
                    "Lens of Truth": "Never needed"}
        for seed in range(400, 500):
            spoiler, _ = self.generate(str(seed), settings)
            self.assertNotIn("Lens of Truth", playthrough_steps(spoiler).values())

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
    SEEDGEN, VERSION = sys.argv[1:3]
    unittest.main(argv=[sys.argv[0], *sys.argv[3:]])
