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
                    "Shop goods": "Vanilla", "Lens of Truth": "Needed", "Gossip stones": "Give hints"}
NO_HINTS = {"Gossip stones": "Vanilla"}
EVERY_SETTING_CHANGED = {
    "Shuffled dungeons": [name for name in DUNGEONS if name not in ("Water Temple", "Ice Cavern")],
    "Small and boss keys": "In their own chests", "Maps and compasses": "In their own chests", "Kokiri Forest": "Open",
    "Rainbow Bridge": "Always open", "Ganon's trials": "None", "Shop goods": "Four per shop shuffled",
    "Lens of Truth": "Never needed"}
# The Fire Temple's lowest locked door, the Water Temple's west middle-floor door, the west door of the Forest Temple's
# bow chest room and the Spirit Temple's door between the statue room and the child climb room start open in every seed.
DOORS_OPEN = {"IsChainLockDoorOpen.4189300454150217955": True, "IsChainLockDoorOpen.2202807660383340401": True,
              "Gimmick_DefeatEnemy_Open_ForestTemple_03": True, "SpiritTemple_Room07_Unlocked": True}
SHOP_SLOTS = read_tsv("shops.tsv")
# SoH's shopsanity takes a shop's items in this order. A count stops at five.
SHOPSANITY_ORDER = [7, 5, 8, 6, 3]
SHOP_COUNTS = {"One per shop shuffled": 1, "Two per shop shuffled": 2, "Three per shop shuffled": 3,
               "Four per shop shuffled": 4, "Five per shop shuffled": 5}


def shuffled_shop_slots(count):
    return [row for row in SHOP_SLOTS if int(row["number"]) in SHOPSANITY_ORDER[:count]]


SHOPS_SHUFFLED = {"Shop goods": "Four per shop shuffled"}
SHOPS_AT_RANDOM = {"Shop goods": "A random number per shop"}
# A seed whose hints name a slot that only five per shop shuffle.
HINT_SEED_AT_FIVE = "1"
KEPT_FROM_SHOPS = {"Item_Range_Bow", "Item_Range_SlingShot", "Item_BombBag_Small", "Item_BombchuBundle_Small"}
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

STONES = {row["label"]: row["location"] for row in read_tsv("stones.tsv")}
PLACE_BY_AREA_PREFIX = {row["area"]: row["says"] for row in read_tsv("places.tsv")}
DUNGEON_PLACES = {"Deku Tree", "Dodongos Cavern", "Jabu Jabus Belly", "Forest Temple", "Fire Temple", "Water Temple",
                  "Shadow Temple", "Spirit Temple", "Bottom of the Well", "Ice Cavern", "Gerudo Training Ground",
                  "Ganon's Castle"}
ITEM_NAMES = {row["remake_item"]: f"{row['article']} {row['name']}".strip() for row in read_tsv("item_names.tsv")}
CHEST_SENTENCES = {row["location"]: row["sentence"] for row in read_tsv("chest_hints.tsv")}
JUNK = [row["hint"] for row in read_tsv("junk_hints.tsv")]
SOH_MAJOR_ITEMS = {"Boomerang", "Progressive Bow", "Progressive Slingshot", "Progressive Hookshot",
                   "Megaton Hammer", "Iron Boots", "Hover Boots", "Lens of Truth", "Silver Gauntlets",
                   "Golden Gauntlets", "Mirror Shield", "Kokiri Sword", "Progressive Bomb Bag", "Ice Arrows"}
BOX_LINES = 2
LINE_LENGTH = 65


def folder(path):
    return path.split("/")[1]


def without_version(log):
    """The spoiler log's text without the line of Kaepora's version, which changes with every release."""
    return re.sub(rb'^    "kaepora": ".*",\n', b"", log, count=1, flags=re.MULTILINE)


def shop_slot(row):
    return {"shopkeepers": [dict(zip(("actor", "goods_list"), cell.split(":")))
                            for cell in row["shopkeepers"].split(",")],
            "slot": int(row["slot"]),
            "shelves": [dict(zip(("file", "hash"), cell.split(":"))) for cell in row["shelves"].split(",")]}


def playthrough_steps(log):
    return {location: item for sphere in log["playthrough"].values() for location, item in sphere.items()}


def place_of(location):
    prefixes = [area for area in PLACE_BY_AREA_PREFIX if location.startswith(area + " ")]
    return max(prefixes, key=len) if prefixes else None


def plain(text):
    text = re.sub(r"\{page\}|[\n^&]", " ", text).replace("@", "Link")
    return " ".join(re.sub(r"\{[^}]*\}|#", "", text).split())


def shop_rows(plan):
    """The shops.tsv row of each of the plan's shop slots, in the plan's order."""
    rows = {json.dumps(shop_slot(row), sort_keys=True): row for row in SHOP_SLOTS}
    return [rows[json.dumps({key: entry[key] for key in ("shopkeepers", "slot", "shelves")}, sort_keys=True)]
            for entry in plan["shops"]]


def shop_of(row):
    return row["location"].rsplit(" Item ", 1)[0]


def placed_remake_items(plan):
    rows = {row["hash"]: row["location"] for row in SHUFFLED}
    placed = {rows[entry["hash"]]: entry["drop_actor"] for entry in plan["chests"]}
    return placed | {row["location"]: entry["item"] for row, entry in zip(shop_rows(plan), plan["shops"])}


def spoiler_item(placed):
    """The item of a spoiler log's location, which for a shop slot comes with its price."""
    return placed["item"] if isinstance(placed, dict) else placed


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
        self.assertEqual(list(spoiler), ["kaepora", "seed", "settings", "locations", "playthrough", "hints"])
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

    def route_out_of_reach(self, log):
        """The route's steps Link cannot reach in their sphere. He follows the route sphere by sphere, holding what the
        spheres before gave him, and a shuffled location off the route holds nothing that counts. A shop slot keeps
        its price."""
        spheres = [list(steps) for steps in log["playthrough"].values()]
        route = {step for steps in spheres for step in steps}

        def nothing(location):
            placed = log["locations"].get(location)
            return {"item": "Green Rupee", "price": placed["price"]} if isinstance(placed, dict) else "Green Rupee"

        placed = [(location, item if location in route else nothing(location))
                  for location, item in log["locations"].items()]
        out_of_reach = []
        for number, steps in enumerate(spheres):
            later = [(step, nothing(step)) for sphere in spheres[number:] for step in sphere]
            out_of_reach += [f"sphere {number:02}: {step}" for step in steps
                             if not reach.reachable(step, *placed, *later, settings=log["settings"])]
        return out_of_reach

    def test_route_steps_are_in_reach_in_their_sphere(self):
        # In seed 1, the pruning drops what three steps counted on in an earlier sphere.
        log, _ = self.generate("1")
        self.assertEqual(self.route_out_of_reach(log), [])

    def test_a_route_that_buys_from_shops_is_in_reach_in_its_sphere(self):
        # Seed 3's route buys the Hookshot, the Megaton Hammer and the Mirror Shield at shuffled slots.
        log, _ = self.generate("3", SHOPS_SHUFFLED)
        self.assertTrue(any(isinstance(log["locations"].get(step), dict)
                            for steps in log["playthrough"].values() for step in steps))
        self.assertEqual(self.route_out_of_reach(log), [])

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
                    "Maps and compasses": "In their own chests", "Lens of Truth": "Never needed", **NO_HINTS}
        spoiler, _ = self.generate("9", settings)
        self.assertEqual(spoiler["settings"],
                         DEFAULT_SETTINGS | settings | {"Shuffled dungeons": ["Deku Tree", "Fire Temple"]})
        self.assertEqual(list(spoiler), ["kaepora", "seed", "settings", "locations", "playthrough", "hints"])

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

    def test_a_price_a_box_cannot_hold_or_a_range_upside_down_is_refused(self):
        out = pathlib.Path(self.tmp.name)
        one = SHOPS_SHUFFLED | {"Shop prices": "One price"}
        between = SHOPS_SHUFFLED | {"Shop prices": "Between two prices"}
        for settings, message in ((one | {"Shop price": 101}, "Shop price: 101"),
                                  (one | {"Shop price": 1000}, "Shop price: 1000"),
                                  (one | {"Shop price": "100"}, 'Shop price: "100"'),
                                  (between | {"Shop price range": [20]}, "Shop price range: [20]"),
                                  (between | {"Shop price range": [200, 20]}, "first number is above its second")):
            result = subprocess.run([SEEDGEN, "11", out / "spoiler.json", out / "plan.json", json.dumps(settings)],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 1, settings)
            self.assertIn(message, result.stderr)
            self.assertFalse((out / "spoiler.json").exists())

    def test_a_price_setting_nothing_applies_is_refused(self):
        out = pathlib.Path(self.tmp.name)
        for settings, key in (({"Shop prices": "One price"}, "Shop prices"),
                              ({"Shop goods": "Vanilla", "Shop price": 100}, "Shop price"),
                              ({"Shop goods": "Vanilla", "Shop price range": [20, 200]}, "Shop price range"),
                              (SHOPS_SHUFFLED | {"Shop price": 135}, "Shop price"),
                              (SHOPS_SHUFFLED | {"Shop prices": "One price", "Shop price range": [20, 40]},
                               "Shop price range"),
                              (SHOPS_SHUFFLED | {"Shop prices": "Between two prices", "Shop price": 100}, "Shop price")):
            result = subprocess.run([SEEDGEN, "11", out / "spoiler.json", out / "plan.json", json.dumps(settings)],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 1, settings)
            self.assertIn(key + ":", result.stderr, settings)
            self.assertFalse((out / "spoiler.json").exists())

    def test_the_spoiler_log_holds_only_the_prices_in_effect(self):
        for settings, prices in ((SHOPS_SHUFFLED, {"Shop prices": "Cheap"}),
                                 (SHOPS_SHUFFLED | {"Shop prices": "One price"},
                                  {"Shop prices": "One price", "Shop price": 100}),
                                 (SHOPS_SHUFFLED | {"Shop prices": "Between two prices"},
                                  {"Shop prices": "Between two prices", "Shop price range": [20, 200]})):
            spoiler, _ = self.generate("12", settings)
            self.assertEqual({key: value for key, value in spoiler["settings"].items() if key.startswith("Shop pric")},
                             prices)

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

    def test_forest_closed_deku(self):
        spoiler, plan = self.generate("19", {"Kokiri Forest": "Closed Deku"})
        self.assertEqual(spoiler["settings"]["Kokiri Forest"], "Closed Deku")
        self.assertEqual(plan["start_values"], DOORS_OPEN | {"KokiriForest_Npc_Kokiri_007_Child_Blocker_00": False})

    def test_only_closed_deku_removes_midos_castle_entry(self):
        for choice, removed in (("Closed", False), ("Closed Deku", True), ("Open", False)):
            _, plan = self.generate("19", {"Kokiri Forest": choice})
            self.assertIs(plan["mido_castle_entry_removed"], removed, choice)

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
        for seed in range(400, 500):
            spoiler, _ = self.generate(str(seed), EVERY_SETTING_CHANGED | NO_HINTS)
            self.assertNotIn("Lens of Truth", playthrough_steps(spoiler).values())

    def assert_shop_slots_sell_the_pool(self, spoiler, plan, slots):
        """The plan's shop slots are the given rows, in table order, each with the spoiler log's item and a cheap price,
        and the shuffled chests and slots hold the pool and a Red Rupee for each slot."""
        self.assertEqual([{key: entry[key] for key in ("shopkeepers", "slot", "shelves")} for entry in plan["shops"]],
                         [shop_slot(row) for row in slots])
        for row, entry in zip(slots, plan["shops"]):
            placed = spoiler["locations"][row["location"]]
            self.assertEqual(list(placed), ["item", "price"], row["location"])
            self.assertIn((entry["item"], placed["item"]), ITEM_PAIRS, row["location"])
            # SoH's cheap balanced prices.
            self.assertEqual(entry["price"], placed["price"])
            self.assertIn(entry["price"], range(0, 100, 5))
        kept = {row["location"] for row in SHOP_SLOTS} - {row["location"] for row in slots}
        self.assertFalse(kept & spoiler["locations"].keys())
        rows = {row["hash"]: row for row in SHUFFLED}
        chests = {rows[entry["hash"]]["location"]: entry["drop_actor"] for entry in plan["chests"]}
        self.assertEqual(collections.Counter(chests.values()) + collections.Counter(e["item"] for e in plan["shops"]),
                         POOL + collections.Counter({"TBox_Rupee_Red": len(slots)}))

    def test_each_count_shuffles_the_first_goods_of_every_shop_in_sohs_order(self):
        for words, count in SHOP_COUNTS.items():
            with self.subTest(words):
                spoiler, plan = self.generate("19", {"Shop goods": words})
                self.assertEqual(spoiler["settings"]["Shop goods"], words)
                self.assertEqual(len(plan["shops"]), 8 * count)
                self.assert_shop_slots_sell_the_pool(spoiler, plan, shuffled_shop_slots(count))

    def test_a_random_number_per_shop_shuffles_one_to_five_of_its_first_goods(self):
        counts = collections.Counter()
        for seed in range(520, 530):
            spoiler, plan = self.generate(str(seed), SHOPS_AT_RANDOM)
            self.assertEqual(spoiler["settings"]["Shop goods"], "A random number per shop")
            rows = shop_rows(plan)
            numbers = collections.defaultdict(set)
            for row in rows:
                numbers[shop_of(row)].add(int(row["number"]))
            self.assertEqual(set(numbers), {shop_of(row) for row in SHOP_SLOTS}, seed)
            for shop, taken in numbers.items():
                self.assertIn(len(taken), range(1, 6), (seed, shop))
                self.assertEqual(taken, set(SHOPSANITY_ORDER[:len(taken)]), (seed, shop))
                counts[len(taken)] += 1
            self.assertEqual(rows, [row for row in SHOP_SLOTS if row in rows], seed)
            self.assert_shop_slots_sell_the_pool(spoiler, plan, rows)
        self.assertEqual(set(counts), set(range(1, 6)))

    def shop_prices(self, seed, settings):
        spoiler, plan = self.generate(seed, SHOPS_SHUFFLED | settings)
        self.assertEqual({key: spoiler["settings"][key] for key in settings}, settings)
        prices = {row["location"]: entry["price"] for row, entry in zip(shop_rows(plan), plan["shops"])}
        self.assertEqual(prices, {location: spoiler["locations"][location]["price"] for location in prices})
        return prices

    def test_vanilla_prices_are_each_slots_own(self):
        prices = self.shop_prices("19", {"Shop prices": "Vanilla"})
        # A poe costs 200 and a fish 50 at the Market Potion Shop. 20, 15 and 30 bombchus cost 240, 210 and 300.
        self.assertEqual({location: prices[location] for location in (
            "Market Potion Shop Item 7", "Market Potion Shop Item 8", "Market Bombchu Shop Item 5",
            "Market Bombchu Shop Item 7", "Market Bombchu Shop Item 8")}, {
            "Market Potion Shop Item 7": 200, "Market Potion Shop Item 8": 50, "Market Bombchu Shop Item 5": 240,
            "Market Bombchu Shop Item 7": 210, "Market Bombchu Shop Item 8": 300})
        self.assertEqual(self.shop_prices("20", {"Shop prices": "Vanilla"}), prices)

    def test_balanced_prices_go_past_cheap_ones(self):
        prices = [price for seed in ("19", "20")
                  for price in self.shop_prices(seed, {"Shop prices": "Balanced"}).values()]
        self.assertLessEqual(set(prices), set(range(0, 300, 5)))
        self.assertGreater(max(prices), 95)

    def test_one_price_for_every_slot(self):
        prices = self.shop_prices("19", {"Shop prices": "One price", "Shop price": 135})
        self.assertEqual(set(prices.values()), {135})

    def test_prices_between_two_prices(self):
        prices = self.shop_prices("19", {"Shop prices": "Between two prices", "Shop price range": [40, 60]})
        self.assertLessEqual(set(prices.values()), set(range(40, 61)))
        self.assertGreater(len(set(prices.values())), 1)

    def test_shops_sell_no_dungeon_item_and_nothing_a_shop_may_refuse(self):
        sold = collections.Counter()
        for seed in range(500, 520):
            spoiler, plan = self.generate(str(seed), SHOPS_SHUFFLED)
            self.assertEqual(list(spoiler["playthrough"].values())[-1], {"Ganon": "Triforce"}, seed)
            sold.update(entry["item"] for entry in plan["shops"])
        self.assertFalse(sold.keys() & (KEPT_FROM_SHOPS | KEYS | MAPS_AND_COMPASSES), sold)

    def test_seed_text_ignores_case(self):
        upper = self.run_seedgen("OWL")
        self.assertEqual(self.run_seedgen("owl"), upper)
        self.assertEqual(self.run_seedgen("oWl"), upper)
        spoiler, plan = map(json.loads, upper)
        self.assertEqual((spoiler["seed"], plan["seed"]), ("OWL", "OWL"))

    def test_every_hint_is_true(self):
        for seed in (FIXED_SEED, *map(str, range(600, 620))):
            self.assert_hints_are_true(seed)
        for seed in map(str, range(620, 625)):
            self.assert_hints_are_true(seed, EVERY_SETTING_CHANGED)

    def assert_hints_are_true(self, seed, settings=None):
        spoiler, plan = self.generate(seed, settings)
        hints = spoiler["hints"]
        self.assertEqual(list(hints), list(STONES.values()))
        self.assertEqual({entry["label"]: plain(entry["text"]) for entry in plan["stone_texts"]},
                         {label: hints[location] for label, location in STONES.items()})
        held = {location: ITEM_NAMES[item] for location, item in placed_remake_items(plan).items()}
        chest_hints = {plain(CHEST_SENTENCES[location].replace("[[1]]", item)) for location, item in held.items()}
        junk = {plain(line) for line in JUNK}
        places = {phrase: area for area, phrase in PLACE_BY_AREA_PREFIX.items()}
        placed = list(spoiler["locations"].items())
        self.assertTrue(reach.reachable("Ganon", *placed, settings=spoiler["settings"]))
        steps = playthrough_steps(spoiler)

        def items_at(area):
            return [item for location, item in held.items() if place_of(location) == area]

        for stone, hint in hints.items():
            with self.subTest(seed=seed, stone=stone, hint=hint):
                if hint in chest_hints or hint in junk:
                    continue
                if found := re.fullmatch(r"They say that (.+) is on the way of the hero\.", hint):
                    area = places[found[1]]
                    needed = [location for location in steps if location in spoiler["locations"]
                              and place_of(location) == area]
                    self.assertTrue(any(not reach.reachable("Ganon", *placed, (location, "Green Rupee"),
                                                            settings=spoiler["settings"])
                                        for location in needed))
                elif found := re.fullmatch(r"They say that plundering (.+) is a foolish choice\.", hint):
                    # SoH's rule: no chest of the area holds a major item or one the seed cannot be finished without.
                    area = places[found[1]]
                    chests = [location for location in spoiler["locations"] if place_of(location) == area]
                    self.assertTrue(chests)
                    self.assertFalse(SOH_MAJOR_ITEMS
                                     & {spoiler_item(spoiler["locations"][location]) for location in chests})
                    self.assertTrue(all(reach.reachable("Ganon", *placed, (location, "Green Rupee"),
                                                        settings=spoiler["settings"])
                                        for location in steps if location in chests))
                elif found := re.fullmatch(r"They say that (.+) can be found at (.+)\.", hint):
                    area = places[found[2]]
                    self.assertNotIn(area, DUNGEON_PLACES)
                    self.assertIn(found[1], items_at(area))
                elif found := re.fullmatch(r"They say that (.+) hoards (.+)\.", hint):
                    area = places[found[1]]
                    self.assertIn(area, DUNGEON_PLACES)
                    self.assertIn(found[2], items_at(area))
                else:
                    self.fail("not a hint")

    def test_hints_name_shop_goods_truly(self):
        # Seed 4 hints at a shuffled shop slot by its sentence.
        self.assert_hints_are_true("4", SHOPS_SHUFFLED)

    def test_hints_name_the_goods_only_five_per_shop_shuffle(self):
        # Seed HINT_SEED_AT_FIVE hints at an item 3 by its slot's sentence.
        seed, settings = HINT_SEED_AT_FIVE, {"Shop goods": "Five per shop shuffled"}
        self.assert_hints_are_true(seed, settings)
        spoiler, plan = self.generate(seed, settings)
        held = placed_remake_items(plan)
        sentences = {plain(CHEST_SENTENCES[row["location"]].replace("[[1]]", ITEM_NAMES[held[row["location"]]]))
                     for row in SHOP_SLOTS if row["number"] == "3"}
        self.assertTrue(sentences & set(spoiler["hints"].values()))

    def test_every_stone_has_a_hint_that_fits_its_boxes(self):
        for seed in (FIXED_SEED, *map(str, range(620, 640))):
            _, plan = self.generate(seed)
            self.assertEqual([entry["label"] for entry in plan["stone_texts"]], list(STONES))
            for entry in plan["stone_texts"]:
                for box in entry["text"].split("{page}"):
                    lines = box.split("\n")
                    self.assertLessEqual(len(lines), BOX_LINES, entry)
                    for line in lines:
                        self.assertTrue(line.strip(), entry)
                        self.assertLessEqual(len(re.sub(r"\{[^}]*\}", "", line)), LINE_LENGTH, entry)

    def test_hints_never_move_an_item(self):
        for seed in map(str, range(640, 650)):
            spoiler, plan = self.generate(seed)
            without_hints, plan_without_hints = self.generate(seed, NO_HINTS)
            self.assertEqual((spoiler["locations"], spoiler["playthrough"], plan["chests"]),
                             (without_hints["locations"], without_hints["playthrough"], plan_without_hints["chests"]))
            self.assertTrue(spoiler["hints"])
            self.assertEqual((without_hints["hints"], plan_without_hints["stone_texts"]), ({}, []))

    def test_fixed_seed_matches_fixture(self):
        spoiler, plan = self.run_seedgen(FIXED_SEED)
        self.assertEqual(without_version(spoiler),
                         without_version((FIXTURES / f"spoiler-{FIXED_SEED}.json").read_bytes()))
        self.assertEqual(plan, (FIXTURES / f"plan-{FIXED_SEED}.json").read_bytes())


if __name__ == "__main__":
    SEEDGEN, reach.PROGRAM, VERSION = sys.argv[1:4]
    unittest.main(argv=[sys.argv[0], *sys.argv[4:]])
