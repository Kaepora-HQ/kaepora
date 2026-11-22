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
                    "Rainbow Bridge": "After the Light Arrows", "Ganon's trials": "All six", "Opening": "Skipped",
                    "Cutscenes": "Skipped", "Start with": [],
                    "Shop goods": "Vanilla", "Gold Skulltulas": "Vanilla", "Gifts": [], "Songs": "Vanilla",
                    "Loose items": "Vanilla", "Dungeon rewards": "Vanilla", "Lens of Truth": "Needed",
                    "Gossip stones": "Give hints", "Hints from characters": "Give hints"}
NO_HINTS = {"Gossip stones": "Vanilla"}
NO_CHARACTER_HINTS = {"Hints from characters": "Vanilla"}
EVERY_SETTING_CHANGED = {
    "Shuffled dungeons": [name for name in DUNGEONS if name not in ("Water Temple", "Ice Cavern")],
    "Small and boss keys": "In their own chests", "Maps and compasses": "In their own chests", "Kokiri Forest": "Open",
    "Rainbow Bridge": "Always open", "Ganon's trials": "None", "Opening": "Played", "Cutscenes": "Played",
    "Start with": ["Hover Boots", "Megaton Hammer"],
    "Shop goods": "Four per shop shuffled", "Gold Skulltulas": "All", "Gifts": "Shuffled",
    "Songs": "Shuffled among themselves", "Loose items": "Shuffled", "Dungeon rewards": "Shuffled",
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
HINT_SEED_AT_FIVE = "2"
WEAPONS_WITH_CONTAINERS = {"Item_Range_Bow", "Item_Range_SlingShot"}
KEPT_FROM_SHOPS = WEAPONS_WITH_CONTAINERS | {"Item_BombBag_Small", "Item_BombchuBundle_Small",
                                             "Item_BombchuBundle_Large", "Item_Bottle", "Item_Bottle_Milk",
                                             "Obj_Armor_Zora"}
GIFT_SITES = read_tsv("gifts.tsv")
GIFT_LOCATIONS = {(row["flow"], row["event"], row["param"]): row["location"] for row in GIFT_SITES}
GIFT_POOL = collections.Counter({row["location"]: row["item"] for row in GIFT_SITES}.values())
GIFTS_SHUFFLED = {"Gifts": "Shuffled"}
# The eight places whose prize makes a bag bigger, and the bigger bag each adds to the shuffle, by SoH's name and the
# remake's.
BAG_PRIZES = {"Market Shooting Gallery": ("Big Deku Seed Bullet Bag", "Item_BulletBag_Medium"),
              "LW Target in Woods": ("Biggest Deku Seed Bullet Bag", "Item_BulletBag_Large"),
              "Kak Shooting Gallery Reward": ("Big Quiver", "Item_Quiver_Medium"),
              "GF HBA 1500 Points": ("Biggest Quiver", "Item_Quiver_Large"),
              "Market Bombchu Bowling First Prize": ("Big Bomb Bag", "Item_BombBag_Medium"),
              "GC Rolling Goron as Child": ("Biggest Bomb Bag", "Item_BombBag_Large"),
              "LW Deku Theater Skull Mask": ("Deku Stick Capacity (20)", "Item_StickBag_Medium"),
              "LW Deku Theater Mask of Truth": ("Deku Nut Capacity (30)", "Item_NutBag_Medium")}
BIGGER_BAGS = dict(BAG_PRIZES.values())
# Three gifts where the story moves, and what each gives in the game.
STORY_GIFTS = {"GF Freed All Carpenters": "Item_Token_Gerudo", "ZD King Zora Thawed": "Obj_Armor_Zora",
               "Market ToT Light Arrow Cutscene": "Item_Arrow_Light"}
GIFT_GROUPS = ["Minigames", "Characters", "House of Skulltula", "Story gifts"]
GIFT_GROUP = {row["location"]: row["group"] for row in GIFT_SITES}
REWARD_SITES = read_tsv("rewards.tsv")
REWARD_LOCATIONS = {(row["flow"], row["event"], row["param"]): row["location"] for row in REWARD_SITES}
REWARDS = {row["item"] for row in REWARD_SITES}
REWARDS_SHUFFLED = {"Dungeon rewards": "Shuffled"}
# The SoH area a hint names each reward place by. Their names have no area in front.
REWARD_AREAS = {"Queen Gohma": "Deku Tree", "King Dodongo": "Dodongos Cavern", "Barinade": "Jabu Jabus Belly",
                "Phantom Ganon": "Forest Temple", "Volvagia": "Fire Temple", "Morpha": "Water Temple",
                "Twinrova": "Spirit Temple", "Bongo Bongo": "Shadow Temple", "Gift from Rauru": "Market"}
AGONY_GUIDE_SHUT = {"flow": "Event/EventFlow/GatheringTokenGold.bfevfl.zs", "event": "Event371",
                    "param": "GameDataName", "text": "KakarikoVillage_GatheringTokenGold_ActionGuide_AgonyStone_True"}
KEYS_NOT_NEEDED = {"Small and boss keys": "Not needed"}
WARPS_A_CHILD_WALKS_OUT_OF = ("Minuet of Forest", "Serenade of Water", "Nocturne of Shadow", "Prelude of Light")
SKULLTULAS = read_tsv("skulltulas.tsv")
SKULLTULA_AT = {row["hash"]: row["location"] for row in SKULLTULAS}
SKULLTULA_PLACES = {row["location"]: row["place"] for row in SKULLTULAS if row["place"]}
SKULLTULAS_SHUFFLED = {"Gold Skulltulas": "All"}
TOKEN = "Item_Token_Gold"
LOOSE_ITEMS = read_tsv("loose_items.tsv")
LOOSE_SHUFFLED = {"Loose items": "Shuffled"}
SONG_PLACES = read_tsv("songs.tsv")
SONGS_SHUFFLED = {"Songs": "Shuffled among themselves"}
SOH_NAMES = {row["remake_item"]: row["soh_item"] for row in read_tsv("items.tsv")}
SONGS = {SOH_NAMES[row["song"]]: row for row in SONG_PLACES}
# SoH's area of each song place, whose name names none.
SONG_PLACE_AREAS = {"Song from Impa": "HC", "Song from Malon": "LLR", "Song from Saria": "SFM",
                    "Song from Royal Family's Tomb": "Graveyard", "Song from Ocarina of Time": "HF",
                    "Song from Windmill": "Kak", "Sheik in Forest": "SFM", "Sheik in Crater": "DMC",
                    "Sheik in Ice Cavern": "Ice Cavern", "Sheik at Colossus": "Colossus", "Sheik in Kakariko": "Kak",
                    "Sheik at Temple": "Market"}
# The event texts every plan carries.
QUEST_TEXTS = [
    {"flow": "Event/EventFlow/Dm_SY_BancChange_EscapeDungeon.bfevfl.zs", "event": "Event4", "param": "ChallengeName",
     "text": "Get_StoneOfForest"},
    {"flow": "Event/EventFlow/Dm_SY_BancChange_EscapeDungeon.bfevfl.zs", "event": "Event4", "param": "StepName",
     "text": "Complete"},
    {"flow": "Event/EventFlow/Get_StoneOfWater.bfevfl.zs", "event": "Event80", "param": "ChallengeName",
     "text": "Get_StoneOfFire"},
    {"flow": "Event/EventFlow/Get_StoneOfWater.bfevfl.zs", "event": "Event80", "param": "StepName", "text": "Complete"},
    {"flow": "Event/EventFlow/Get_StoneOfWater.bfevfl.zs", "event": "Event293", "param": "GameDataName",
     "text": "Sequence_IsAlternative_ZoraDomainAdult"},
    {"flow": "Event/EventFlow/Npc_Hylia_Malon_Adult.bfevfl.zs", "event": "Event179", "param": "GameDataName",
     "text": "Get_Epona_IsAfter_Ready_Exp"}]
# The Desert Colossus's way into the Wasteland and the Spirit Temple's front entrance, in both ages' files, stand only
# once the Spirit quest is past Step02 in every seed that shuffles the songs.
COLOSSUS_GATES = [{"file": f"Banc/DesertColossus/DesertColossus_{age}_Static.bcett.byml.zs", "hash": hash,
                   "flag": "SageOfSpirit_IsAfter_Step02_Exp"}
                  for age, hash in (("Adult", "12141577802165947392"), ("Adult", "13187812046230253833"),
                                    ("Child", "18409754566529742762"), ("Child", "15448624735210695325"))]
# The windmill man starts his lesson while it has not played, and the Bolero's scene marks itself played, not the
# Bolero known, in every seed that shuffles the songs.
SONG_FIXES = [
    {"flow": "Event/EventFlow/Npc_Hylia_009_Adult.bfevfl.zs", "event": "Event17", "param": "GameDataName",
     "text": "HavePlayedEvent.Dm_MelodyGet_Storm"},
    {"flow": "Event/EventFlow/Npc_Hylia_009_Adult.bfevfl.zs", "event": "Event69", "param": "GameDataName",
     "text": "HavePlayedEvent.Dm_MelodyGet_Storm"},
    {"flow": "Event/EventFlow/Dm_MelodyGet_Fire.bfevfl.zs", "event": "Event1", "param": "GameDataName",
     "text": "HavePlayedEvent.Dm_MelodyGet_Fire"}]
# What a seed can start with, by the game's names, and the actor a new game holds for each.
START_ITEMS = {"Kokiri Sword": "Weapon_Sword_Kokiri", "Deku Shield": "Weapon_Shield_Deku",
               "Hylian Shield": "Weapon_Shield_Hylian", "Mirror Shield": "Weapon_Shield_Mirror",
               "Iron Boots": "Armor_Adult_HeavyBoot", "Hover Boots": "Armor_Adult_HoverBoot",
               "Goron Tunic": "Armor_Adult_Goron", "Zora Tunic": "Armor_Adult_Zora", "Megaton Hammer": "Item_Hammer",
               "Boomerang": "Item_Boomerang"}
# The shuffled chest copies a started item leaves out of the pool. Every Deku Shield stays, as in SoH's pool.
START_COPIES = {"Kokiri Sword": "Item_Sword_Kokiri", "Hylian Shield": "TBox_Shield_Hylian",
                "Mirror Shield": "Item_Shield_Mirror", "Iron Boots": "Item_HeavyBoot", "Hover Boots": "Item_HoverBoot",
                "Megaton Hammer": "Item_Hammer", "Boomerang": "Item_Boomerang"}
START_STEPS = {
    "Fairy Slingshot (50 seeds)": ["Weapon_Range_SlingShot", "Item_BulletBag_Medium", "Item_BulletBag_Large"],
    "Fairy Bow (50 arrows)": ["Weapon_Range_Bow", "Item_Quiver_Medium", "Item_Quiver_Large"],
    "Bomb Bag (40 bombs)": ["Item_BombBag_Small", "Item_BombBag_Medium", "Item_BombBag_Large"],
    "Longshot": ["Item_Hookshot", "Item_Longshot"],
    "Golden Gauntlets": ["Armor_Child_Gauntlet_Goron", "Armor_Adult_Gauntlet_Silver",
                         "Armor_Adult_Gauntlet_Gold"],
    "Golden Scale": ["Item_Scale_Silver", "Item_Scale_Gold"],
    "Giant's Wallet": ["Item_Wallet_Medium", "Item_Wallet_Large"],
    "Deku Sticks (30)": ["Item_DekuStick", "Item_StickBag_Medium", "Item_StickBag_Large"],
    "Deku Nuts (40)": ["Item_DekuNut", "Item_NutBag_Medium", "Item_NutBag_Large"],
    "Fairy Ocarina": ["Item_Ocarina_Fairy"],
}
# The items that shuffled places name for those steps: chests, and with gifts shuffled the scales, the wallets and the
# bigger bags.
STEP_COPIES = ["Item_Range_SlingShot", "Item_Range_Bow", "Item_BombBag_Small", "Item_Longshot", "Item_Gauntlet_Silver",
               "Item_Gauntlet_Gold", "Item_Scale_Silver", "Item_Scale_Gold", "Item_Wallet_Medium", "Item_Wallet_Large",
               "Item_BulletBag_Medium", "Item_BulletBag_Large", "Item_Quiver_Medium", "Item_Quiver_Large",
               "Item_BombBag_Medium", "Item_BombBag_Large", "Item_StickBag_Medium", "Item_NutBag_Medium"]
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
PRIZE_LINES = read_tsv("prize_lines.tsv")
CHARACTER_HINTS = read_tsv("character_hints.tsv")
CHEST_GAME_PRIZE = "Market Treasure Chest Game Reward"
LENS_NEVER_NEEDED = {"Lens of Truth": "Never needed"}
FOREST_OPEN = {"Kokiri Forest": "Open"}
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


def soh_area(location):
    """The SoH area of a location: the prefix of its name, or for a song place or a reward place, whose names name
    none, its area."""
    if location in SONG_PLACE_AREAS:
        return SONG_PLACE_AREAS[location]
    prefixes = [area for area in PLACE_BY_AREA_PREFIX if location.startswith(area + " ")]
    return max(prefixes, key=len) if prefixes else REWARD_AREAS.get(location)


def place_of(location):
    """Where the remake has a location: the place remake/skulltulas.tsv gives a skulltula that stands elsewhere, or its
    SoH area."""
    return SKULLTULA_PLACES.get(location) or soh_area(location)


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


def gift_sites(plan, locations=GIFT_LOCATIONS):
    return [(locations[site], entry["text"]) for entry in plan["event_texts"]
            if (site := (entry["flow"], entry["event"], entry["param"])) in locations]


def reward_sites(plan):
    return gift_sites(plan, REWARD_LOCATIONS)


def placed_remake_items(spoiler, plan):
    rows = {row["hash"]: row["location"] for row in SHUFFLED}
    placed = {rows[entry["hash"]]: entry["drop_actor"] for entry in plan["chests"]}
    loose = {entry["hash"]: entry["item"] for entry in plan["loose_items"]}
    placed |= {row["location"]: loose.get(row["hash"], row["item"]) for row in LOOSE_ITEMS
               if row["location"] in spoiler["locations"]}
    placed |= {row["location"]: entry["item"] for row, entry in zip(shop_rows(plan), plan["shops"])}
    lessons = {tuple(row["gift"].split(":")): row["location"] for row in SONG_PLACES}
    placed |= {lessons[text["flow"], text["event"]]: text["text"] for text in plan["event_texts"]
               if (text["flow"], text["event"]) in lessons}
    placed |= dict(gift_sites(plan)) | dict(reward_sites(plan))
    return placed | {SKULLTULA_AT[entry["hash"]]: entry["item"] for entry in plan["skulltulas"]}


def in_a_dungeon(location):
    return soh_area(location) in DUNGEON_PLACES


def song_lessons(spoiler):
    """The event texts and numbers a plan carries for its song places: each place's lesson takes the melody and the
    line of the song it teaches, and its gift the song."""
    texts, numbers = [], []
    for place in SONG_PLACES:
        song = SONGS[spoiler["locations"][place["location"]]]
        for event in place["examples"].split(","):
            texts.append({"flow": place["lesson"], "event": event, "param": "MessageId", "text": song["line"]})
        flow, event = place["gift"].split(":")
        texts.append({"flow": flow, "event": event, "param": "ActorName", "text": song["song"]})
        for event in place["examples"].split(",") + place["follows"].split(","):
            numbers.append({"flow": place["lesson"], "event": event, "param": "MelodyType",
                            "value": int(song["melody"])})
    return texts, numbers


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
        self.assertEqual(list(spoiler), ["kaepora", "seed", "settings", "locations", "playthrough", "hints",
                                         "hints from characters"])
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

    def test_route_names_each_boss_before_the_steps_that_wait_for_it(self):
        # The game's gates wait for the bosses, not for their rewards. The stored seed's route takes the Ocarina of
        # Time, the Nocturne and the Light Arrows, so it names in an earlier sphere each boss their rules ask for.
        log, _ = self.generate(FIXED_SEED)
        sphere = {step: number for number, steps in enumerate(log["playthrough"].values()) for step in steps}
        waits = {"HF Ocarina of Time Item": ("King Dodongo", "Barinade"),
                 "Song from Ocarina of Time": ("King Dodongo", "Barinade"),
                 "Sheik in Kakariko": ("Phantom Ganon", "Volvagia", "Morpha"),
                 "Market ToT Light Arrow Cutscene": ("Bongo Bongo", "Twinrova")}
        for step, bosses in waits.items():
            for boss in bosses:
                self.assertIn(boss, sphere, step)
                self.assertLess(sphere[boss], sphere[step], (boss, step))
        # With the songs shuffled, Twinrova waits for Phantom Ganon, though seed 1 needs nothing he holds.
        log, _ = self.generate("1", SONGS_SHUFFLED)
        sphere = {step: number for number, steps in enumerate(log["playthrough"].values()) for step in steps}
        self.assertIn("Phantom Ganon", sphere)
        self.assertLess(sphere["Phantom Ganon"], sphere["Twinrova"])

    def test_with_king_zoras_place_shuffled_morpha_waits_for_his_thanks(self):
        # King Zora's ice stands only while the Water quest is not complete, so a Link who finishes the Water Temple
        # first never gets what King Zora hands over. With his place as in the game the Zora shop then gives the tunic.
        # With his place shuffled, every route that takes Morpha takes King Zora first, whatever he holds.
        for settings in ({"Gifts": ["Story gifts"]}, {"Gifts": GIFT_GROUPS}):
            for seed in map(str, range(1, 11)):
                log, _ = self.generate(seed, settings)
                sphere = {step: number for number, steps in enumerate(log["playthrough"].values()) for step in steps}
                self.assertIn("ZD King Zora Thawed", sphere, (settings, seed))
                self.assertLess(sphere["ZD King Zora Thawed"], sphere["Morpha"], (settings, seed))
            self.assertEqual(self.route_out_of_reach(log), [])

    def test_a_route_with_the_forest_exit_open_may_never_enter_the_deku_tree(self):
        # Closed Deku opens the way out of the forest.
        steps = playthrough_steps(self.generate("1", {"Kokiri Forest": "Closed Deku"})[0])
        self.assertIn("HF Ocarina of Time Item", steps)
        self.assertEqual([step for step in steps if step == "Queen Gohma" or step.startswith("Deku Tree ")], [])

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

    def test_a_route_that_goes_back_to_child_before_the_forest_temple_is_in_reach_in_its_sphere(self):
        # Only child Link drains the well, with the Song of Storms that only adult Link learns. Seed 3's route takes a
        # chest in the well before Phantom Ganon.
        log, _ = self.generate("3")
        spheres = list(log["playthrough"].values())
        forest_temple = next(number for number, steps in enumerate(spheres) if "Phantom Ganon" in steps)
        self.assertTrue(any(step.startswith("Bottom of the Well") for steps in spheres[:forest_temple] for step in steps))
        self.assertEqual(self.route_out_of_reach(log), [])

    def test_a_route_warps_a_child_to_the_colossus_only_after_a_song_that_takes_him_out(self):
        # A child cannot walk out of the Desert Colossus, and only the Requiem takes him there. With keys not needed,
        # seed 4's route takes steps that need the Requiem. Its adult Link holds the Longshot and the Hover Boots by
        # then, so he walks to the Colossus, and the child is the one who needs the song.
        log, _ = self.generate("4", KEYS_NOT_NEEDED)
        spheres = [list(steps) for steps in log["playthrough"].values()]
        given = {spoiler_item(item): number for number, steps in enumerate(log["playthrough"].values())
                 for item in steps.values()}
        requiem = next(location for location, item in playthrough_steps(log).items() if item == "Requiem of Spirit")
        without_requiem = [*log["locations"].items(), (requiem, "Green Rupee")]
        by_child = [number for number, steps in enumerate(spheres) if number > given["Requiem of Spirit"]
                    for step in steps if not reach.reachable(step, *without_requiem, settings=log["settings"])]
        self.assertTrue(by_child)
        way_out = min(given.get(song, len(spheres)) for song in WARPS_A_CHILD_WALKS_OUT_OF)
        self.assertLess(way_out, min(by_child))

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
        _, plan = self.generate("12")
        self.assertEqual(plan["event_texts"], QUEST_TEXTS)
        self.assertEqual(plan["event_numbers"], [])
        self.assertEqual(plan["presences"], [])

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
        self.assertEqual(list(spoiler), ["kaepora", "seed", "settings", "locations", "playthrough", "hints",
                                         "hints from characters"])

    def test_default_settings_are_the_settings_left_out(self):
        self.assertEqual(self.run_seedgen("10", DEFAULT_SETTINGS), self.run_seedgen("10"))
        self.assertEqual(self.run_seedgen("10", {}), self.run_seedgen("10"))

    def test_an_unknown_setting_is_refused(self):
        out = pathlib.Path(self.tmp.name)
        for settings, message in (({"Lens of Truth": "Sometimes"}, "Sometimes"),
                                  ({"Shuffled dungeons": ["Deku Tree", "Hyrule Castle"]}, "Hyrule Castle"),
                                  ({"Forest": "Open"}, "Forest"),
                                  ({"Start with": ["Kokiri Sword", "Master Sword"]}, "Master Sword"),
                                  ({"Start with": ["Hookshot", "Longshot"]}, "Hookshot and Longshot"),
                                  ({"Gifts": ["Minigames", "Fishing"]}, "Fishing")):
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

    def test_the_opening_is_skipped_unless_played(self):
        for settings, skipped in (({}, True), ({"Opening": "Played"}, False)):
            spoiler, plan = self.generate("20", settings)
            self.assertEqual(spoiler["settings"]["Opening"], "Played" if settings else "Skipped")
            self.assertIs(plan["opening_skipped"], skipped, settings)

    def test_cutscenes_are_skipped_unless_played(self):
        for settings, skipped in (({}, True), ({"Cutscenes": "Played"}, False)):
            spoiler, plan = self.generate("20", settings)
            self.assertEqual(spoiler["settings"]["Cutscenes"], "Played" if settings else "Skipped")
            self.assertIs(plan["cutscenes_skipped"], skipped, settings)

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

    def assert_every_setting_changed_at_once(self, seeds):
        # The place after a boss is a step for the boss whatever it holds, so only there may the Lens stand on the
        # route.
        bosses = REWARD_AREAS.keys() - {"Gift from Rauru"}
        for seed in seeds:
            spoiler, _ = self.generate(str(seed), EVERY_SETTING_CHANGED | NO_HINTS)
            lens = {place for place, item in playthrough_steps(spoiler).items() if item == "Lens of Truth"}
            self.assertLessEqual(lens, bosses, seed)
            # No shuffled place, the nine reward places among them, holds an item the seed starts with.
            placed = {spoiler_item(item) for item in spoiler["locations"].values()}
            self.assertFalse(placed & set(EVERY_SETTING_CHANGED["Start with"]), seed)
            self.assertEqual(list(spoiler["playthrough"].values())[-1], {"Ganon": "Triforce"}, seed)

    def test_every_setting_changed_at_once_400_to_419(self):
        # In seed 405 Queen Gohma's place holds the Lens.
        self.assert_every_setting_changed_at_once(range(400, 420))

    def test_every_setting_changed_at_once_420_to_439(self):
        self.assert_every_setting_changed_at_once(range(420, 440))

    def test_every_setting_changed_at_once_440_to_459(self):
        self.assert_every_setting_changed_at_once(range(440, 460))

    def test_every_setting_changed_at_once_460_to_479(self):
        self.assert_every_setting_changed_at_once(range(460, 480))

    def test_every_setting_changed_at_once_480_to_499(self):
        self.assert_every_setting_changed_at_once(range(480, 500))

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

    def test_gifts_shuffled(self):
        spoiler, plan = self.generate("21", GIFTS_SHUFFLED)
        self.assertEqual(spoiler["settings"]["Gifts"], GIFT_GROUPS)
        sites = gift_sites(plan)
        self.assertIn(AGONY_GUIDE_SHUT, plan["event_texts"])
        self.assertEqual([location for location, _ in sites], list(GIFT_LOCATIONS.values()))
        for location, item in sites:
            self.assertIn((item, spoiler["locations"][location]), ITEM_PAIRS, location)
        rows = {row["hash"]: row for row in SHUFFLED}
        chests = {rows[entry["hash"]]["location"]: entry["drop_actor"] for entry in plan["chests"]}
        self.assertEqual(collections.Counter(chests.values()) + collections.Counter(dict(sites).values()),
                         POOL + GIFT_POOL)

    def test_shuffled_is_every_gift_group_and_vanilla_is_none(self):
        for seed in ("21", "22"):
            self.assertEqual(self.run_seedgen(seed, {"Gifts": GIFT_GROUPS}), self.run_seedgen(seed, GIFTS_SHUFFLED))
            self.assertEqual(self.run_seedgen(seed, {"Gifts": []}), self.run_seedgen(seed, {"Gifts": "Vanilla"}))

    def test_one_gift_group_shuffles_its_places_alone(self):
        kept_on_route = 0
        for group in GIFT_GROUPS:
            spoiler, plan = self.generate("21", {"Gifts": [group]})
            self.assertEqual(spoiler["settings"]["Gifts"], [group])
            places = [location for location in dict.fromkeys(GIFT_LOCATIONS.values()) if GIFT_GROUP[location] == group]
            sites = gift_sites(plan)
            self.assertEqual(list(dict.fromkeys(location for location, _ in sites)), places, group)
            self.assertEqual(AGONY_GUIDE_SHUT in plan["event_texts"], group == "House of Skulltula")
            kept = GIFT_GROUP.keys() - set(places)
            self.assertFalse(kept & spoiler["locations"].keys(), group)
            vanilla = {row["location"]: row["item"] for row in GIFT_SITES}
            for location, item in playthrough_steps(spoiler).items():
                if location in kept:
                    kept_on_route += 1
                    self.assertIn((vanilla[location], item), ITEM_PAIRS, location)
            rows = {row["hash"]: row for row in SHUFFLED}
            chests = {rows[entry["hash"]]["location"]: entry["drop_actor"] for entry in plan["chests"]}
            self.assertEqual(collections.Counter(chests.values()) + collections.Counter(dict(sites).values()),
                             POOL + collections.Counter(vanilla[location] for location in places), group)
        self.assertTrue(kept_on_route)

    def test_the_gift_places_of_an_item_that_comes_in_steps_share_a_group(self):
        # A kept place gives the next step by what Link holds, and a shuffled one gives one step, so an item whose steps
        # sat in two groups could leave its last step out of a player's reach.
        for item, steps in START_STEPS.items():
            groups = {row["group"] for row in GIFT_SITES if {row["gives"], row["item"]} & set(steps)}
            self.assertLessEqual(len(groups), 1, (item, groups))

    def test_a_gift_given_at_two_sites_gives_one_item(self):
        given = collections.defaultdict(set)
        for seed in map(str, range(22, 27)):
            _, plan = self.generate(seed, GIFTS_SHUFFLED)
            items = collections.defaultdict(set)
            for location, item in gift_sites(plan):
                items[location].add(item)
            for location, item in items.items():
                self.assertEqual(len(item), 1, (seed, location))
                given[location] |= item
        twice = {location for location, sites in collections.Counter(GIFT_LOCATIONS.values()).items() if sites > 1}
        self.assertTrue(twice)
        self.assertTrue(any(len(given[location]) > 1 for location in twice))

    def test_what_a_shop_may_refuse_goes_to_any_other_shuffled_place(self):
        # Gifts bring the bottles and 20 Bombchus into the pool. They stay out of shops, as the Bomb Bag and 5 Bombchus
        # do, and each may sit at a gift.
        sold, given = collections.Counter(), collections.Counter()
        for seed in range(520, 540):
            _, plan = self.generate(str(seed), SHOPS_SHUFFLED | GIFTS_SHUFFLED)
            sold.update(entry["item"] for entry in plan["shops"])
            given.update(item for _, item in gift_sites(plan))
        self.assertFalse(sold.keys() & KEPT_FROM_SHOPS, sold)
        self.assertTrue(given.keys() & KEPT_FROM_SHOPS, given)

    def test_a_gift_can_hold_the_bow_and_the_slingshot(self):
        given = collections.Counter()
        for settings in (GIFTS_SHUFFLED, GIFTS_SHUFFLED | SHOPS_SHUFFLED):
            for seed in range(540, 560):
                _, plan = self.generate(str(seed), settings)
                given.update(item for _, item in gift_sites(plan))
        self.assertLessEqual(WEAPONS_WITH_CONTAINERS, given.keys(), given)

    def test_gifts_give_no_dungeon_item(self):
        given = collections.Counter()
        for seed in range(700, 720):
            _, plan = self.generate(str(seed), GIFTS_SHUFFLED)
            given.update(item for _, item in gift_sites(plan))
        self.assertFalse(given.keys() & (KEYS | MAPS_AND_COMPASSES | {"Obj_MiniGame_SmallKey"}), given)
        self.assertGreater(len(given), len(GIFT_POOL))

    def test_a_route_that_takes_gifts_is_in_reach_in_its_sphere(self):
        # Seed 10's route takes the Kokiri Sword from child fishing and the Lens of Truth from the man on the roof, among
        # other gifts.
        log, plan = self.generate("10", GIFTS_SHUFFLED)
        vanilla = {row["location"]: row["item"] for row in GIFT_SITES}
        self.assertTrue(any(location in playthrough_steps(log) and item != vanilla[location]
                            for location, item in gift_sites(plan)))
        self.assertEqual(list(log["playthrough"].values())[-1], {"Ganon": "Triforce"})
        self.assertEqual(self.route_out_of_reach(log), [])

    def test_hints_name_gifts_truly(self):
        # Seed 23 hints at a gift by its sentence: the adult fishing prize.
        spoiler, plan = self.assert_hints_are_true("23", GIFTS_SHUFFLED)
        sentences = {plain(CHEST_SENTENCES[location].replace("[[1]]", ITEM_NAMES[item]))
                     for location, item in gift_sites(plan)}
        self.assertTrue(sentences & set(spoiler["hints"].values()))

    def test_each_bag_prize_gives_its_placed_item_at_both_sites(self):
        spoiler, plan = self.generate("31", GIFTS_SHUFFLED)
        sites = collections.defaultdict(list)
        for location, item in gift_sites(plan):
            sites[location].append(item)
        for location in BAG_PRIZES:
            self.assertEqual(len(sites[location]), 2, location)
            self.assertEqual(len(set(sites[location])), 1, location)
            self.assertIn((sites[location][0], spoiler["locations"][location]), ITEM_PAIRS, location)

    def test_a_bigger_bag_has_one_name_wherever_it_lands(self):
        landed = collections.Counter()
        for seed in map(str, range(1400, 1410)):
            spoiler, plan = self.generate(seed, GIFTS_SHUFFLED | LOOSE_SHUFFLED | SKULLTULAS_SHUFFLED)
            for location, item in placed_remake_items(spoiler, plan).items():
                if (bag := spoiler_item(spoiler["locations"][location])) in BIGGER_BAGS:
                    self.assertEqual(item, BIGGER_BAGS[bag], (seed, location))
                    landed[bag] += 1
        self.assertEqual(landed, collections.Counter({bag: 10 for bag in BIGGER_BAGS}))

    def test_bigger_bags_stay_out_of_shops(self):
        # As the Bomb Bag: no shelf has been seen to sell a bag.
        sold, landed = collections.Counter(), 0
        for seed in map(str, range(1410, 1420)):
            spoiler, plan = self.generate(seed, SHOPS_SHUFFLED | GIFTS_SHUFFLED)
            sold.update(entry["item"] for entry in plan["shops"])
            landed += sum(spoiler_item(placed) in BIGGER_BAGS for placed in spoiler["locations"].values())
        self.assertFalse(sold.keys() & set(BIGGER_BAGS.values()), sold)
        self.assertEqual(landed, 10 * len(BIGGER_BAGS))

    def assert_bags_count_for_nothing(self, item, bags, target, others=()):
        """Some seed puts one of the bags in reach before any copy of the item. With every copy held back, and every
        copy of the others that also reach the target, the target stays out of reach, and the item where that bag lies
        would reach it."""
        for seed in map(str, range(1420, 1460)):
            spoiler, _ = self.generate(seed, GIFTS_SHUFFLED)
            settings = spoiler["settings"]
            held = [(location, "Green Rupee" if placed in {item, *others} else placed)
                    for location, placed in spoiler["locations"].items()]
            early = [location for location, placed in spoiler["locations"].items()
                     if placed in bags and location != target and reach.reachable(location, *held, settings=settings)]
            if early:
                self.assertFalse(reach.reachable(target, *held, settings=settings), seed)
                self.assertTrue(reach.reachable(target, *held, (early[0], item), settings=settings), seed)
                return
        self.fail(f"no seed puts {bags} in reach before {item}")

    def test_a_bigger_quiver_or_bullet_bag_is_no_weapon(self):
        self.assert_bags_count_for_nothing("Progressive Bow", {"Big Quiver", "Biggest Quiver"},
                                           "Kak Shooting Gallery Reward")
        self.assert_bags_count_for_nothing("Progressive Slingshot",
                                           {"Big Deku Seed Bullet Bag", "Biggest Deku Seed Bullet Bag"},
                                           "LW Target in Woods", others=("Boomerang",))

    def test_the_rolling_goron_waits_for_the_small_bomb_bag(self):
        self.assert_bags_count_for_nothing("Progressive Bomb Bag", {"Big Bomb Bag", "Biggest Bomb Bag"},
                                           "GC Rolling Goron as Child")

    def test_a_start_with_the_bows_40_arrows_shuffles_no_bigger_quiver(self):
        # The start holds the bigger quiver, so the Kakariko gallery adds no copy of it to the pool.
        spoiler, plan = self.generate("1480", GIFTS_SHUFFLED | {"Start with": ["Fairy Bow (40 arrows)"]})
        self.assertNotIn("Item_Quiver_Medium", placed_remake_items(spoiler, plan).values())
        self.assertIn("Item_Quiver_Medium", placed_remake_items(*self.generate("1480", GIFTS_SHUFFLED)).values())

    def test_the_gerudo_pass_the_zora_tunic_and_the_light_arrows_are_gifts(self):
        # Each of the three places writes the item placed there at its one event, and each item, wherever it lands,
        # goes by its one name. The pass goes only to a chest, a gift or a reward's place, as a stone does. A shop
        # refuses a tunic to a child, so none sells the Zora Tunic.
        settings = GIFTS_SHUFFLED | REWARDS_SHUFFLED | SHOPS_SHUFFLED | SKULLTULAS_SHUFFLED | LOOSE_SHUFFLED
        chests = {row["location"] for row in SHUFFLED}
        shops = {row["location"] for row in SHOP_SLOTS}
        skulltulas = {row["location"] for row in SKULLTULAS}
        kinds, moved = collections.defaultdict(set), set()
        for seed in map(str, range(640, 660)):
            spoiler, plan = self.generate(seed, settings)
            sites = gift_sites(plan)
            for location in STORY_GIFTS:
                given = [item for at, item in sites if at == location]
                self.assertEqual(len(given), 1, (seed, location))
                self.assertIn((given[0], spoiler["locations"][location]), ITEM_PAIRS, (seed, location))
            placed = placed_remake_items(spoiler, plan)
            for location, item in STORY_GIFTS.items():
                at = [place for place, held in placed.items() if held == item]
                self.assertEqual(len(at), 1, (seed, item))
                kinds[item].add("chest" if at[0] in chests else "gift" if at[0] in GIFT_LOCATIONS.values()
                                else "reward place" if at[0] in REWARD_AREAS else "shop" if at[0] in shops
                                else "skulltula" if at[0] in skulltulas else "loose")
                if at[0] != location:
                    moved.add(item)
        self.assertEqual(moved, set(STORY_GIFTS.values()))
        self.assertLessEqual(kinds["Item_Token_Gerudo"], {"chest", "gift", "reward place"}, kinds)
        self.assertNotIn("shop", kinds["Obj_Armor_Zora"], kinds)

    def test_the_route_names_the_rescue_and_zeldas_scene_before_what_waits_for_them(self):
        # The Gerudo let Link pass after the last carpenter's scene, and the bridge follows Zelda's scene, whatever
        # their places give. So the route names each place before any step that waits for it.
        other = set()
        for seed in map(str, range(660, 680)):
            log, _ = self.generate(seed, GIFTS_SHUFFLED)
            sphere = {step: number for number, steps in enumerate(log["playthrough"].values()) for step in steps}
            for step, number in sphere.items():
                if step.startswith(("Gerudo Training Ground", "Wasteland", "GF HBA")):
                    self.assertLess(sphere.get("GF Freed All Carpenters", number), number, (seed, step))
                if step.startswith("Ganon's Castle") or step == "Ganon":
                    self.assertLess(sphere.get("Market ToT Light Arrow Cutscene", number), number, (seed, step))
            other |= {location for location in ("GF Freed All Carpenters", "Market ToT Light Arrow Cutscene")
                      if location in sphere and (STORY_GIFTS[location], log["locations"][location]) not in ITEM_PAIRS}
        self.assertEqual(other, {"GF Freed All Carpenters", "Market ToT Light Arrow Cutscene"})

    def test_a_seed_with_the_pass_the_tunic_and_the_arrows_elsewhere_reaches_ganon(self):
        # Seed 2 puts the Gerudo Pass in Dodongo's Cavern, the Zora Tunic in Hyrule Field's Redead grotto and the Light
        # Arrows in the Ice Cavern, and its route takes the carpenters' rescue and Zelda's scene for what they open.
        log, _ = self.generate("2", GIFTS_SHUFFLED)
        for location, item in STORY_GIFTS.items():
            self.assertNotIn((item, log["locations"][location]), ITEM_PAIRS, location)
        self.assertLessEqual({"GF Freed All Carpenters", "Market ToT Light Arrow Cutscene"},
                             playthrough_steps(log).keys())
        self.assertEqual(list(log["playthrough"].values())[-1], {"Ganon": "Triforce"})
        self.assertEqual(self.route_out_of_reach(log), [])

    def test_dungeon_rewards_shuffled(self):
        # With the gifts at Vanilla the nine places join the chests' shuffle. Link's Pocket, where the patcher writes
        # nothing, holds no reward: the Light Medallion is one of the nine.
        spoiler, plan = self.generate("31", REWARDS_SHUFFLED)
        self.assertEqual(spoiler["settings"]["Dungeon rewards"], "Shuffled")
        sites = reward_sites(plan)
        self.assertEqual([location for location, _ in sites], list(REWARD_LOCATIONS.values()))
        for location, item in sites:
            self.assertIn((item, spoiler["locations"][location]), ITEM_PAIRS, location)
        rows = {row["hash"]: row for row in SHUFFLED}
        chests = {rows[entry["hash"]]["location"]: entry["drop_actor"] for entry in plan["chests"]}
        self.assertEqual(collections.Counter(chests.values()) + collections.Counter(item for _, item in sites),
                         POOL + collections.Counter(REWARDS))
        self.assertTrue(REWARDS & set(chests.values()))
        self.assertTrue({item for _, item in sites} - REWARDS)

    def test_stones_and_medallions_go_to_chests_gifts_and_the_nine_places(self):
        # A chest gave a stone and a medallion with their models and Link's hold-up pose, and a gift raises an item the
        # same way. No shop slot, Gold Skulltula or loose spot has been seen to give one.
        settings = REWARDS_SHUFFLED | GIFTS_SHUFFLED | SHOPS_SHUFFLED | SKULLTULAS_SHUFFLED | LOOSE_SHUFFLED
        chests = {row["location"] for row in SHUFFLED}
        held = collections.Counter()
        for seed in range(600, 620):
            spoiler, plan = self.generate(str(seed), settings)
            placed = placed_remake_items(spoiler, plan)
            self.assertEqual(sorted(item for item in placed.values() if item in REWARDS), sorted(REWARDS), seed)
            for location, item in placed.items():
                if item in REWARDS:
                    kind = ("chest" if location in chests else "gift" if location in GIFT_LOCATIONS.values()
                            else "reward place" if location in REWARD_AREAS else location)
                    held[item.startswith("Item_Medal_"), kind] += 1
        self.assertEqual({kind for _, kind in held}, {"chest", "gift", "reward place"}, held)
        self.assertTrue(held[True, "chest"] and held[False, "chest"], held)

    def test_a_dungeon_left_out_keeps_its_boss_reward(self):
        settings = REWARDS_SHUFFLED | GIFTS_SHUFFLED | {"Shuffled dungeons": [name for name in DUNGEONS
                                                                               if name != "Water Temple"]}
        spoiler, plan = self.generate("1", settings)
        self.assertNotIn("Morpha", spoiler["locations"])
        self.assertNotIn("Morpha", dict(reward_sites(plan)))
        self.assertNotIn("Item_Medal_Water", placed_remake_items(spoiler, plan).values())
        self.assertIn("Twinrova", spoiler["locations"])

    def test_hints_name_no_stone_or_medallion_as_an_item_to_find(self):
        # They open nothing in the remake. A hint about a place may still say what it holds.
        names = {ITEM_NAMES[item] for item in REWARDS}
        for seed in range(620, 640):
            spoiler, _ = self.generate(str(seed), REWARDS_SHUFFLED | GIFTS_SHUFFLED)
            for hint in spoiler["hints"].values():
                found = (re.fullmatch(r"They say that (.+) can be found at .+\.", hint)
                         or re.fullmatch(r"They say that .+ hoards (.+)\.", hint))
                self.assertFalse(found and found[1] in names, hint)

    def test_hints_name_dungeon_rewards_truly(self):
        self.assert_hints_are_true("32", REWARDS_SHUFFLED | GIFTS_SHUFFLED)

    def test_gold_skulltulas_shuffled(self):
        dungeon = [row for row in SKULLTULAS if in_a_dungeon(row["location"])]
        overworld = [row for row in SKULLTULAS if not in_a_dungeon(row["location"])]
        self.assertEqual((len(SKULLTULAS), len(dungeon)), (100, 44))
        rows = {row["hash"]: row for row in SHUFFLED}
        for choice, shuffled in (("All", SKULLTULAS), ("In dungeons", dungeon),
                                 ("In the overworld", overworld)):
            spoiler, plan = self.generate("21", {"Gold Skulltulas": choice})
            self.assertEqual(spoiler["settings"]["Gold Skulltulas"], choice)
            self.assertEqual([{"hash": entry["hash"], "files": entry["files"]} for entry in plan["skulltulas"]],
                             [{"hash": row["hash"], "files": row["files"].split(",")} for row in shuffled])
            for row, entry in zip(shuffled, plan["skulltulas"]):
                self.assertIn((entry["item"], spoiler["locations"][row["location"]]), ITEM_PAIRS, row["location"])
            kept = {row["location"] for row in SKULLTULAS} - {row["location"] for row in shuffled}
            self.assertFalse(kept & spoiler["locations"].keys())
            given = collections.Counter(entry["item"] for entry in plan["skulltulas"])
            self.assertFalse(given.keys() & (KEYS | MAPS_AND_COMPASSES | {"Obj_MiniGame_SmallKey"}), choice)
            chests = {rows[entry["hash"]]["location"]: entry["drop_actor"] for entry in plan["chests"]}
            self.assertEqual(collections.Counter(chests.values()) + given,
                             POOL + collections.Counter({TOKEN: len(shuffled)}), choice)

    def test_skulltulas_hold_the_bomb_bag_the_bow_and_the_slingshot(self):
        given = collections.Counter()
        for seed in range(700, 730):
            _, plan = self.generate(str(seed), SKULLTULAS_SHUFFLED)
            given.update(entry["item"] for entry in plan["skulltulas"])
        self.assertLessEqual(WEAPONS_WITH_CONTAINERS | {"Item_BombBag_Small"}, given.keys(), given)

    def test_a_dungeon_left_out_keeps_its_skulltulas(self):
        left_out = {"Dodongo's Cavern": "DodongoCavern", "Shadow Temple": "ShadowTemple"}
        settings = {"Shuffled dungeons": [name for name in DUNGEONS if name not in left_out], **SKULLTULAS_SHUFFLED}
        spoiler, plan = self.generate("1053", settings)
        kept = {row["location"] for row in SKULLTULAS if folder(row["files"]) in left_out.values()}
        self.assertTrue(kept)
        self.assertFalse(kept & spoiler["locations"].keys())
        self.assertEqual({SKULLTULA_AT[entry["hash"]] for entry in plan["skulltulas"]},
                         {row["location"] for row in SKULLTULAS} - kept)

    def test_a_route_with_gold_skulltulas_shuffled_is_in_reach_in_its_sphere(self):
        log, _ = self.generate("23", SKULLTULAS_SHUFFLED)
        skulltulas = {row["location"] for row in SKULLTULAS}
        self.assertEqual(len(skulltulas & playthrough_steps(log).keys()), 5)
        self.assertEqual(self.route_out_of_reach(log), [])

    def test_songs_shuffled_among_themselves(self):
        spoiler, plan = self.generate("21", SONGS_SHUFFLED)
        self.assertEqual(spoiler["settings"]["Songs"], "Shuffled among themselves")
        places = {row["location"] for row in SONG_PLACES}
        taught = {location: spoiler["locations"][location] for location in places}
        self.assertEqual(sorted(taught.values()), sorted(SONGS))
        self.assertTrue(any(SOH_NAMES[row["song"]] != taught[row["location"]] for row in SONG_PLACES))
        elsewhere = {spoiler_item(item) for location, item in spoiler["locations"].items() if location not in places}
        self.assertFalse(elsewhere & SONGS.keys())
        self.assertFalse({item for location, item in playthrough_steps(spoiler).items()
                          if location not in places} & SONGS.keys())
        self.assertEqual(len(places), 12)
        texts, numbers = song_lessons(spoiler)
        self.assertEqual(plan["event_texts"], QUEST_TEXTS + SONG_FIXES + texts)
        self.assertEqual(plan["event_numbers"], numbers)
        self.assertEqual(len(numbers), 26)
        self.assertEqual(plan["presences"], COLOSSUS_GATES)

    def test_a_route_that_learns_songs_at_other_places_is_in_reach_in_its_sphere(self):
        # Seed 22's route learns songs at places that teach others in the game.
        log, _ = self.generate("22", SONGS_SHUFFLED)
        places = {row["location"] for row in SONG_PLACES}
        self.assertTrue(any(location in places and SONGS[item]["location"] != location
                            for location, item in playthrough_steps(log).items()))
        self.assertEqual(self.route_out_of_reach(log), [])

    def test_with_songs_shuffled_both_stone_bosses_wait_for_zeldas_letter(self):
        # A child who finishes both stone dungeons before he meets Zelda may never get the Ocarina of Time. With the
        # songs shuffled, Malon can teach Zelda's Lullaby before the castle's courtyard, and seed 23's route took
        # King Dodongo and Barinade with no step there.
        log, _ = self.generate("23", SONGS_SHUFFLED)
        sphere = {step: number for number, steps in enumerate(log["playthrough"].values()) for step in steps}
        for boss in ("King Dodongo", "Barinade"):
            self.assertLess(sphere["HC Zeldas Letter"], sphere[boss], boss)
        self.assertEqual(self.route_out_of_reach(log), [])

    def test_both_stone_bosses_wait_for_zeldas_letter(self):
        # With the bracelet, a scale and the Bomb Bag in Mido's chests, a child opens both stone dungeons without the
        # courtyard, songs as they are in the game or not.
        early = [("KF Mido Top Left Chest", "Goron's Bracelet"), ("KF Mido Top Right Chest", "Progressive Scale"),
                 ("KF Mido Bottom Left Chest", "Progressive Bomb Bag")]
        for boss in ("King Dodongo", "Barinade"):
            self.assertTrue(reach.reachable(boss, *early), boss)
            self.assertFalse(reach.reachable(boss, *early, ("HC Zeldas Letter", "Green Rupee")), boss)

    def test_with_songs_shuffled_shadow_and_spirit_never_both_come_before_the_forest_temple(self):
        # The second of the Shadow and Spirit sages sets AllSage to Step02, the one step at which the Light Arrows scene
        # starts as Link walks into the Temple of Time. The Forest Temple's end then sets it back to Step01_00, and
        # nothing sets Step02 again. Seed 10 teaches the Nocturne in the meadow: with the Forest Temple's boss key held
        # back, Link can still clear one of the two, and never both.
        spoiler, _ = self.generate("10", SONGS_SHUFFLED)
        key = next(location for location, item in spoiler["locations"].items() if item == "Forest Temple Boss Key")
        placed = [*spoiler["locations"].items(), (key, "Green Rupee")]
        self.assertFalse(reach.reachable("Phantom Ganon", *placed, settings=spoiler["settings"]))
        cleared = [reach.reachable(boss, *placed, settings=spoiler["settings"]) for boss in ("Bongo Bongo", "Twinrova")]
        self.assertEqual(sorted(cleared), [False, True])

    def test_with_songs_shuffled_the_courtyard_scene_starts_played(self):
        # With the songs shuffled, a child can learn a warp song and, grown up, warp out of the Temple of Time before he
        # walks through its door. The scene there plays only while AllSage stands at Step01, and the adult's world map
        # waits for it.
        _, shuffled = self.generate("21", SONGS_SHUFFLED)
        _, vanilla = self.generate("21")
        self.assertIs(shuffled["start_values"]["HavePlayedEvent.Dm_TimeTemple_0065"], True)
        self.assertNotIn("HavePlayedEvent.Dm_TimeTemple_0065", vanilla["start_values"])

    def test_loose_items_shuffled(self):
        spoiler, plan = self.generate("21", LOOSE_SHUFFLED)
        self.assertEqual(spoiler["settings"]["Loose items"], "Shuffled")
        planned = {entry["hash"]: entry for entry in plan["loose_items"]}
        for row in LOOSE_ITEMS:
            placed = spoiler["locations"][row["location"]]
            entry = planned.pop(row["hash"], None)
            self.assertIn((row["item"] if entry is None else entry["item"], placed), ITEM_PAIRS, row["location"])
            if entry is not None:
                self.assertEqual(entry["files"], row["files"].split(","), row["location"])
                self.assertNotEqual(entry["item"], row["item"], row["location"])
        self.assertEqual(planned, {})
        self.assertTrue(plan["loose_items"])
        held = placed_remake_items(spoiler, plan)
        self.assertEqual(collections.Counter(held.values()),
                         POOL + collections.Counter(row["item"] for row in LOOSE_ITEMS))

    def test_loose_keys_follow_the_keys_setting(self):
        keys = {row["location"]: folder(row["files"]) for row in LOOSE_ITEMS if row["item"] == "Item_Dungeon_SmallKey"}
        names = {"BottomOfTheWell": "Bottom of the Well", "ShadowTemple": "Shadow Temple"}
        dungeon_item = re.compile(r".* (Small Key|Boss Key|Map|Compass)$")
        spoiler, _ = self.generate("22", LOOSE_SHUFFLED | {"Small and boss keys": "In their own chests"})
        self.assertFalse(keys.keys() & spoiler["locations"].keys())
        for choice in ("Anywhere in their dungeon", "Not needed"):
            moved = set()
            for seed in map(str, range(700, 710)):
                spoiler, _ = self.generate(seed, LOOSE_SHUFFLED | {"Small and boss keys": choice})
                for location, dungeon in keys.items():
                    item = spoiler["locations"][location]
                    if dungeon_item.fullmatch(item):
                        self.assertTrue(item.startswith(names[dungeon]), (seed, location, item))
                    else:
                        moved.add(location)
            self.assertEqual(moved, keys.keys(), choice)

    def test_loose_items_hold_the_bomb_bag_the_bow_and_the_slingshot(self):
        given = collections.Counter()
        for seed in range(300, 330):
            _, plan = self.generate(str(seed), LOOSE_SHUFFLED)
            given.update(entry["item"] for entry in plan["loose_items"])
        self.assertLessEqual(WEAPONS_WITH_CONTAINERS | {"Item_BombBag_Small"}, given.keys(), given)

    def test_a_dungeon_left_out_keeps_its_loose_items(self):
        settings = LOOSE_SHUFFLED | {"Shuffled dungeons": [name for name in DUNGEONS if name != "Shadow Temple"]}
        spoiler, _ = self.generate("23", settings)
        shadow = {row["location"] for row in LOOSE_ITEMS if folder(row["files"]) == "ShadowTemple"}
        self.assertTrue(shadow)
        self.assertFalse(shadow & spoiler["locations"].keys())
        self.assertIn("Bottom of the Well Freestanding Key", spoiler["locations"])

    def test_a_route_with_loose_items_shuffled_is_in_reach_in_its_sphere(self):
        log, _ = self.generate("24", LOOSE_SHUFFLED)
        self.assertTrue({row["location"] for row in LOOSE_ITEMS} & playthrough_steps(log).keys())
        self.assertEqual(self.route_out_of_reach(log), [])

    def test_hints_name_loose_items_truly(self):
        # Seed 5 hints at King Dodongo's heart container, the Zora's River heart piece near the domain and the Lon Lon
        # Ranch heart piece by their sentences.
        spoiler, plan = self.assert_hints_are_true("5", LOOSE_SHUFFLED)
        loose = {row["location"] for row in LOOSE_ITEMS}
        said = {plain(CHEST_SENTENCES[location].replace("[[1]]", ITEM_NAMES[item]))
                for location, item in placed_remake_items(spoiler, plan).items() if location in loose}
        self.assertTrue(said & set(spoiler["hints"].values()))

    def test_a_route_reaches_the_colossus_only_with_a_way_out(self):
        # The Desert Colossus's way into the Wasteland stands only once an adult has walked into the Wasteland from the
        # fortress, a walk no route names, so Link leaves the Colossus by another warp song or comes on foot. Seed 7's
        # route takes a step there before the carpenters' rescue. Were an adult's Requiem to count once the rescue is
        # done, seed 5's route, with gifts shuffled, would take one after the rescue with the Requiem as its only warp
        # song and no Lens of Truth for the Wasteland.
        for seed, settings in (("7", SONGS_SHUFFLED), ("5", GIFTS_SHUFFLED | SONGS_SHUFFLED)):
            log, _ = self.generate(seed, settings)
            spheres = list(log["playthrough"].values())
            there = next(number for number, steps in enumerate(spheres)
                         if any(step.startswith(("Colossus", "Spirit Temple")) or step == "Sheik at Colossus"
                                for step in steps))
            held = collections.Counter(item for steps in spheres[:there] for item in steps.values())
            # Both ages walk on from where the Minuet, the Serenade, the Nocturne and the Prelude land them. An adult
            # gets across the crater from the Bolero's landing with the Hookshot or the Hover Boots.
            warps_out = held.keys() & {"Minuet of Forest", "Serenade of Water", "Nocturne of Shadow",
                                       "Prelude of Light"} or held["Bolero of Fire"] and (
                held["Progressive Hookshot"] or held["Hover Boots"])
            rescued = any("GF Freed All Carpenters" in steps for steps in spheres[:there])
            walks_in = rescued and held["Lens of Truth"] and (held["Hover Boots"] or held["Progressive Hookshot"] >= 2)
            self.assertTrue(warps_out or walks_in, (seed, spheres[there]))

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
        held = {location: ITEM_NAMES[item] for location, item in placed_remake_items(spoiler, plan).items()}
        chest_hints = {plain(CHEST_SENTENCES[location].replace("[[1]]", item)) for location, item in held.items()}
        junk = {plain(line) for line in JUNK}
        places = {phrase: area for area, phrase in PLACE_BY_AREA_PREFIX.items()}
        placed = list(spoiler["locations"].items())
        self.assertTrue(reach.reachable("Ganon", *placed, settings=spoiler["settings"]))
        steps = playthrough_steps(spoiler)

        def items_at(area, dungeon):
            """Items that a hint names at the place, with "hoards" for a dungeon location and "can be found at" for
            any other, as SoH words it."""
            return [item for location, item in held.items()
                    if place_of(location) == area and in_a_dungeon(location) == dungeon]

        self.assert_character_hints_are_true(spoiler, plan)
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
                    self.assertIn(found[1], items_at(area, False))
                elif found := re.fullmatch(r"They say that (.+) hoards (.+)\.", hint):
                    area = places[found[1]]
                    self.assertIn(found[2], items_at(area, True))
                else:
                    self.fail("not a hint")
        return spoiler, plan

    def assert_character_hints_are_true(self, spoiler, plan):
        """Each character's hint names the place of the seed that holds its item, in each of its messages, and the
        spoiler log has what each message says. With the forest open, or a seed that starts with the Kokiri Sword,
        Mido says none."""
        texts = {(entry["file"], entry["label"]): plain(entry["text"]) for entry in plan["message_texts"]}
        said = collections.defaultdict(list)
        for row in CHARACTER_HINTS:
            if spoiler["settings"]["Kokiri Forest"] == "Open" or row["item"] in spoiler["settings"]["Start with"]:
                self.assertNotIn((row["file"], row["label"]), texts, spoiler["seed"])
                continue
            [location] = [location for location, placed in spoiler["locations"].items()
                          if spoiler_item(placed) == row["item"]]
            line = plain(row["sentence"].replace("[[1]]", PLACE_BY_AREA_PREFIX[place_of(location)]))
            self.assertEqual(texts[(row["file"], row["label"])], line, spoiler["seed"])
            said[row["hint"]].append(line)
        self.assertEqual(spoiler["hints from characters"], said)

    def stones_naming_the_sword(self, spoiler, plan):
        """The stones' hints that name the Kokiri Sword's place: its chest's own sentence, or one of the two named item
        hints, as SoH words them."""
        [sword] = [location for location, placed in spoiler["locations"].items()
                   if spoiler_item(placed) == "Kokiri Sword"]
        held = {location: ITEM_NAMES[item] for location, item in placed_remake_items(spoiler, plan).items()}
        named = {plain(CHEST_SENTENCES[sword].replace("[[1]]", held[sword])),
                 f"They say that the Kokiri Sword can be found at {PLACE_BY_AREA_PREFIX[place_of(sword)]}.",
                 f"They say that {PLACE_BY_AREA_PREFIX[place_of(sword)]} hoards the Kokiri Sword."}
        return named & set(spoiler["hints"].values())

    def test_mido_names_where_the_kokiri_sword_lies(self):
        for seed in range(800, 820):
            spoiler, plan = self.generate(str(seed))
            self.assert_character_hints_are_true(spoiler, plan)
            self.assertEqual(len(spoiler["hints from characters"]["KF Mido Hint"]), 3)
        # Seed 811 puts the sword in a chest of Mido's House.
        spoiler, _ = self.generate("811")
        self.assertIn("the Kokiri Sword I hid in Kokiri Forest!", spoiler["hints from characters"]["KF Mido Hint"][0])

    def test_a_place_a_character_names_is_not_named_again_by_a_stone(self):
        for seed in range(800, 860):
            spoiler, plan = self.generate(str(seed))
            self.assertFalse(self.stones_naming_the_sword(spoiler, plan), seed)

    def test_mido_gives_no_hint_with_the_forest_open(self):
        # Seen in the game: with the forest open, Mido greets a Link with no sword and no shield as one who has both,
        # and never says the lines that carry the hint. The stones may name the sword's place, as without him.
        named = 0
        for seed in range(860, 880):
            spoiler, plan = self.generate(str(seed), FOREST_OPEN)
            self.assert_character_hints_are_true(spoiler, plan)
            self.assertEqual(spoiler["hints from characters"], {})
            named += bool(self.stones_naming_the_sword(spoiler, plan))
        self.assertTrue(named)

    def test_a_seed_starts_with_what_is_chosen(self):
        chosen = list(START_ITEMS)
        spoiler, plan = self.generate("31", {"Start with": chosen[::-1]})
        self.assertEqual(spoiler["settings"]["Start with"], chosen)
        self.assertEqual(plan["start_items"], [START_ITEMS[name] for name in chosen])
        # A Red Rupee takes the place of each started item's shuffled copy, as SoH's pool puts junk in its place.
        rows = {row["hash"]: row for row in SHUFFLED}
        chests = {rows[entry["hash"]]["location"]: entry["drop_actor"] for entry in plan["chests"]}
        self.assertEqual(collections.Counter(chests.values()),
                         POOL - collections.Counter(START_COPIES.values())
                         + collections.Counter({"TBox_Rupee_Red": len(START_COPIES)}))
        # The logic holds each from the start, so the route takes none from a shuffled place, and Mido has no sword to
        # tell of. As SoH's logic does, it counts a Deku or Hylian Shield only once bought, so a route may buy one.
        self.assertFalse(set(START_ITEMS) & set(playthrough_steps(spoiler).values()))
        self.assertEqual(list(spoiler["playthrough"].values())[-1], {"Ganon": "Triforce"})
        self.assert_character_hints_are_true(spoiler, plan)
        self.assertNotIn("KF Mido Hint", spoiler["hints from characters"])
        self.assertEqual(self.route_out_of_reach(spoiler), [])

    def test_a_seed_starts_with_each_item_that_comes_in_steps_at_its_highest_step(self):
        chosen = list(START_STEPS)
        spoiler, plan = self.generate("32", {"Start with": chosen[::-1], "Gifts": "Shuffled"})
        self.assertEqual(spoiler["settings"]["Start with"], chosen)
        self.assertEqual(plan["start_items"], [actor for name in chosen for actor in START_STEPS[name]])
        placed = placed_remake_items(spoiler, plan)
        self.assertFalse(set(STEP_COPIES) & set(placed.values()))
        unstarted = placed_remake_items(*self.generate("32", {"Gifts": "Shuffled"}))
        self.assertEqual(list(placed.values()).count("TBox_Rupee_Red"),
                         list(unstarted.values()).count("TBox_Rupee_Red") + len(STEP_COPIES))
        self.assertEqual(list(spoiler["playthrough"].values())[-1], {"Ganon": "Triforce"})
        self.assertEqual(self.route_out_of_reach(spoiler), [])

    def test_a_started_zora_tunic_leaves_the_gifts(self):
        # With the gifts shuffled, King Zora's place adds a Zora Tunic to the pool. A seed that starts with one leaves
        # it out, with a Red Rupee in its stead.
        for seed in ("1", "2", "3"):
            placed = placed_remake_items(*self.generate(seed, {"Gifts": "Shuffled", "Start with": ["Zora Tunic"]}))
            unstarted = placed_remake_items(*self.generate(seed, GIFTS_SHUFFLED))
            self.assertNotIn("Obj_Armor_Zora", placed.values(), seed)
            self.assertEqual(list(placed.values()).count("TBox_Rupee_Red"),
                             list(unstarted.values()).count("TBox_Rupee_Red") + 1, seed)

    def test_a_place_that_gives_a_started_step_again_counts_for_nothing(self):
        # Dampé's race, Saria, the diving game, the two wallet rewards and the Bomb Bag's chest of a dungeon left out
        # each give one named step, which a seed that starts with it already holds. The logic would count the next step
        # there.
        again = {"Graveyard Hookshot Chest", "LW Gift From Saria", "ZD Diving Minigame", "Kak 10 Gold Skulltula Reward",
                 "Kak 30 Gold Skulltula Reward"}
        started = ["Hookshot", "Silver Scale", "Giant's Wallet", "Fairy Ocarina"]
        # In seed 9 the Giant's Wallet and both rewards would make four wallets, more than the logic's count holds.
        cases = [(seed, {"Start with": started}, again) for seed in ("9", "33", "34", "35", "36")]
        cases.append(("37", {"Start with": [*started, "Bomb Bag (20 bombs)"],
                             "Shuffled dungeons": [name for name in DUNGEONS if name != "Dodongo's Cavern"]},
                      again | {"Dodongos Cavern Bomb Bag Chest"}))
        for seed, settings, given_again in cases:
            spoiler, _ = self.generate(seed, settings)
            self.assertFalse(given_again & set(playthrough_steps(spoiler)), seed)
            self.assertEqual(list(spoiler["playthrough"].values())[-1], {"Ganon": "Triforce"}, seed)
            self.assertEqual(self.route_out_of_reach(spoiler), [], seed)

    def test_characters_give_no_hints_at_vanilla(self):
        # Seed 811 puts the Kokiri Sword in a chest of Mido's House, and with Mido silent a stone names it there.
        prize_lines = [(row["file"], row["label"]) for row in PRIZE_LINES]
        spoiler, plan = self.generate("811", NO_CHARACTER_HINTS)
        given, given_plan = self.generate("811")
        self.assertEqual(spoiler["settings"]["Hints from characters"], "Vanilla")
        self.assertEqual(spoiler["hints from characters"], {})
        self.assertEqual([(entry["file"], entry["label"]) for entry in plan["message_texts"]], prize_lines)
        self.assertEqual(plan["message_texts"], given_plan["message_texts"][:len(prize_lines)])
        self.assertEqual({key: value for key, value in plan.items() if key not in ("stone_texts", "message_texts")},
                         {key: value for key, value in given_plan.items()
                          if key not in ("stone_texts", "message_texts")})
        self.assertEqual((spoiler["locations"], spoiler["playthrough"]), (given["locations"], given["playthrough"]))
        self.assertIn("They say that the Kokiri Sword can be found at Kokiri Forest.", spoiler["hints"].values())
        self.assertNotIn("They say that the Kokiri Sword can be found at Kokiri Forest.", given["hints"].values())

    def test_hints_name_shop_goods_truly(self):
        # Seed 4 hints at a shuffled shop slot by its sentence.
        self.assert_hints_are_true("4", SHOPS_SHUFFLED)

    def test_hints_name_the_goods_only_five_per_shop_shuffle(self):
        # Seed HINT_SEED_AT_FIVE hints at an item 3 by its slot's sentence.
        seed, settings = HINT_SEED_AT_FIVE, {"Shop goods": "Five per shop shuffled"}
        self.assert_hints_are_true(seed, settings)
        spoiler, plan = self.generate(seed, settings)
        held = placed_remake_items(spoiler, plan)
        sentences = {plain(CHEST_SENTENCES[row["location"]].replace("[[1]]", ITEM_NAMES[held[row["location"]]]))
                     for row in SHOP_SLOTS if row["number"] == "3"}
        self.assertTrue(sentences & set(spoiler["hints"].values()))

    def test_the_chest_game_owner_names_the_prize(self):
        # Without the Lens the prize stays the game's own, a Piece of Heart.
        row = next(row for row in PRIZE_LINES if row["location"] == CHEST_GAME_PRIZE)
        named = set()
        for seed, settings in [(str(seed), None) for seed in range(700, 710)] + [("710", LENS_NEVER_NEEDED)]:
            spoiler, plan = self.generate(seed, settings)
            item = placed_remake_items(spoiler, plan).get(CHEST_GAME_PRIZE, "Obj_Heart_Piece")
            self.assertEqual(settings is None, CHEST_GAME_PRIZE in placed_remake_items(spoiler, plan), seed)
            [entry] = [entry for entry in plan["message_texts"] if (entry["file"], entry["label"])
                       == (row["file"], row["label"])]
            self.assertEqual(plain(entry["text"]), plain(row["sentence"].replace("[[1]]", ITEM_NAMES[item])), seed)
            named.add(item)
        self.assertGreater(len(named), 2, named)

    def test_every_message_fits_its_box(self):
        sites = {(row["file"], row["label"]): row for row in PRIZE_LINES + CHARACTER_HINTS}
        for seed, settings in [(str(seed), None) for seed in range(720, 730)] + [("730", SHOPS_SHUFFLED)]:
            _, plan = self.generate(seed, settings)
            self.assertTrue(plan["message_texts"])
            for entry in plan["message_texts"]:
                site = sites[(entry["file"], entry["label"])]
                for box in entry["text"].split("{page}"):
                    lines = box.split("\n")
                    self.assertLessEqual(len(lines), int(site["box_lines"]), entry)
                    for line in lines:
                        self.assertTrue(line.strip(), entry)
                        self.assertLessEqual(len(re.sub(r"\{[^}]*\}", "", line).encode()), int(site["line_length"]),
                                             entry)

    def test_hints_name_the_place_a_skulltula_stands_in(self):
        # Seed 408's route takes the Megaton Hammer from the Hyrule Field tree skulltula that SoH calls LLR GS Back Wall,
        # and a hint says Hyrule Field is on the way of the hero. Seed 664's route takes the Megaton Hammer from HC OGC
        # GS, in the scene the game calls Ganondorf's Castle, and a hint says that scene is on the way.
        for seed in ("408", "664"):
            self.assert_hints_are_true(seed, {"Gold Skulltulas": "In the overworld"})

    def test_hints_name_gold_skulltulas_truly(self):
        self.assert_hints_are_true("23", SKULLTULAS_SHUFFLED)
        spoiler, plan = self.generate("23", SKULLTULAS_SHUFFLED)
        skulltulas = {row["location"] for row in SKULLTULAS}
        said = {plain(CHEST_SENTENCES[location].replace("[[1]]", ITEM_NAMES[item]))
                for location, item in placed_remake_items(spoiler, plan).items() if location in skulltulas}
        self.assertTrue(said & set(spoiler["hints"].values()))

    def test_hints_name_song_places_truly(self):
        # Seed 24 hints at two song places by their sentences.
        spoiler, plan = self.generate("24", SONGS_SHUFFLED)
        held = placed_remake_items(spoiler, plan)
        sentences = {plain(CHEST_SENTENCES[row["location"]].replace("[[1]]", ITEM_NAMES[held[row["location"]]]))
                     for row in SONG_PLACES}
        self.assertTrue(sentences & set(spoiler["hints"].values()))
        self.assert_hints_are_true("24", SONGS_SHUFFLED)

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
