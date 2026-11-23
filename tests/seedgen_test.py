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
SKIPS = ["Opening movie", "Player skippable scenes", "New area scenes", "Tutorial messages", "Saria's greeting"]
DEFAULT_SETTINGS = {"Shuffled dungeons": DUNGEONS, "Small and boss keys": "Anywhere in their dungeon",
                    "Maps and compasses": "Anywhere in their dungeon", "Kokiri Forest": "Closed",
                    "Door of Time": "Closed",
                    "Rainbow Bridge": "After the Light Arrows", "Ganon's trials": "All six", "Skip": SKIPS,
                    "Start with": [],
                    "Shop goods": "Vanilla", "Gold Skulltulas": "Vanilla",
                    "Gifts": ["Minigames", "Characters", "House of Skulltula", "Great Fairies", "Story gifts",
                              "Biggoron Quest", "Deku Scrubs (One Time Rewards)"],
                    "Songs": "Shuffled among themselves", "Loose items": "Shuffled",
                    "Dungeon rewards": "Shuffled among themselves", "Lens of Truth": "Needed",
                    "Gossip stones": "Give hints", "Hints from characters": "Give hints"}
# The settings that shuffle the chests alone.
CHESTS_ALONE = {"Gifts": [], "Songs": "Vanilla", "Loose items": "Vanilla", "Dungeon rewards": "Vanilla"}
NO_HINTS = {"Gossip stones": "Vanilla"}
NO_CHARACTER_HINTS = {"Hints from characters": "Vanilla"}
EVERY_SETTING_CHANGED = {
    "Shuffled dungeons": [name for name in DUNGEONS if name not in ("Water Temple", "Ice Cavern")],
    "Small and boss keys": "In their own chests", "Maps and compasses": "In their own chests", "Kokiri Forest": "Open",
    "Door of Time": "Open",
    "Rainbow Bridge": "Always open", "Ganon's trials": "None", "Skip": [],
    "Start with": ["Hover Boots", "Megaton Hammer"],
    "Shop goods": "Four per shop shuffled", "Gold Skulltulas": "All",
    "Gifts": ["Minigames", "Characters", "House of Skulltula", "Story gifts", "Malon's Egg", "Biggoron Quest",
              "Deku Scrubs (One Time Rewards)"],
    "Songs": "Shuffled among themselves", "Loose items": "Shuffled", "Dungeon rewards": "Shuffled",
    "Lens of Truth": "Never needed"}
# In every seed the Fire Temple's lowest locked door, the Water Temple's west middle-floor door, the west door of the
# Forest Temple's bow chest room and the Spirit Temple's door between the statue room and the child climb room start
# open, and the world's time runs from the start and through the night.
EVERY_SEED = {"IsChainLockDoorOpen.4189300454150217955": True, "IsChainLockDoorOpen.2202807660383340401": True,
              "Gimmick_DefeatEnemy_Open_ForestTemple_03": True, "SpiritTemple_Room07_Unlocked": True,
              "HyruleField_ArriveEntrance": True, "HavePlayedEvent.Dm_HrlField_0020": True}
SARIAS_GREETING = {"HavePlayedEvent.Dm_Kokiri_0045": True, "HavePlayedEvent.Dm_Kokiri_0050": True,
                   "HavePlayedEvent.Dm_Kokiri_0060": True, "IsVisitLocation.KokiriForest": True}
TUTORIALS = {row["value"]: row["start"] == "true" for row in read_tsv("tutorials.tsv")}
TUTORIAL_NEXTS = [{"flow": "Event/EventFlow/Dm_SY_BancChangeArrive.bfevfl.zs", "event": "Event9", "next": "Event10"},
                  {"flow": "Event/EventFlow/Dm_FairyFountain_Power.bfevfl.zs", "event": "Event141", "next": None}]
DEFAULT_START_VALUES = EVERY_SEED | TUTORIALS | SARIAS_GREETING
SHOP_SLOTS = read_tsv("shops.tsv")
# SoH's shopsanity takes a shop's items in this order.
SHOPSANITY_ORDER = [7, 5, 8, 6, 3, 1, 4]
SHOP_COUNTS = {"Goods shuffled among the shops": 0, "One per shop shuffled": 1, "Two per shop shuffled": 2,
               "Three per shop shuffled": 3, "Four per shop shuffled": 4, "Five per shop shuffled": 5,
               "Six per shop shuffled": 6, "Seven per shop shuffled": 7}
# SoH's goods for the shelves a count leaves, the most needed first (`3drando/shops.cpp`, `GetMinVanillaShopItems`). A
# count of N leaves 64 - 8N shelves, and the fill puts the first 64 - 8N goods on them.
SOH_KEPT_GOODS = [
    "Buy Deku Shield", "Buy Hylian Shield", "Buy Goron Tunic", "Buy Zora Tunic", "Buy Deku Nut (5)",
    "Buy Bombs (20)", "Buy Bombchu (10)", "Buy Deku Stick (1)", "Buy Fairy's Spirit", "Buy Deku Seeds (30)",
    "Buy Arrows (10)", "Buy Blue Fire", "Buy Red Potion [30]", "Buy Green Potion", "Buy Deku Nut (10)",
    "Buy Bombchu (10)", "Buy Bombchu (10)", "Buy Bombchu (20)", "Buy Bombs (5) [25]", "Buy Bombs (5) [35]",
    "Buy Bombs (10)", "Buy Deku Nut (5)", "Buy Arrows (30)", "Buy Arrows (50)", "Buy Arrows (10)",
    "Buy Fairy's Spirit", "Buy Bottle Bug", "Buy Fish", "Buy Hylian Shield", "Buy Bottle Bug",
    "Buy Deku Stick (1)", "Buy Deku Stick (1)", "Buy Blue Fire", "Buy Fish", "Buy Bombchu (10)",
    "Buy Deku Nut (5)", "Buy Arrows (10)", "Buy Bombchu (20)", "Buy Bombs (5) [35]", "Buy Red Potion [30]",
    "Buy Bombs (30)", "Buy Bombchu (20)", "Buy Deku Nut (5)", "Buy Arrows (10)", "Buy Deku Nut (5)",
    "Buy Arrows (30)", "Buy Red Potion [40]", "Buy Fish", "Buy Bombchu (20)", "Buy Arrows (30)",
    "Buy Red Potion [50]", "Buy Arrows (30)", "Buy Deku Nut (5)", "Buy Arrows (50)", "Buy Arrows (50)",
    "Buy Green Potion", "Buy Poe", "Buy Poe", "Buy Heart", "Buy Heart", "Buy Heart", "Buy Heart", "Buy Heart",
    "Buy Heart"
]


def goods_name(item):
    """The spoiler log's name for one of SoH's goods: SoH's own, without the price SoH tells some goods apart by, as
    "Buy Bombs (5) [25]"."""
    return re.sub(r" \[\d+\]$", "", item)


# By the spoiler log's names. Goods SoH tells apart by a price are the same goods at one price in the remake.
KEPT_GOODS = {goods_name(row["item"]): row for row in read_tsv("kept_goods.tsv")}


def shuffled_shop_slots(count):
    return [row for row in SHOP_SLOTS if int(row["number"]) in SHOPSANITY_ORDER[:count]]


SHOPS_SHUFFLED = {"Shop goods": "Four per shop shuffled"}
SHOPS_AT_RANDOM = {"Shop goods": "A random number per shop"}
SEVEN_PER_SHOP = {"Shop goods": "Seven per shop shuffled"}
# A seed whose hints name a slot that only five per shop shuffle, and one for six and seven.
HINT_SEED_AT_FIVE = "3"
HINT_SEED_AT_SEVEN = "2"
WEAPONS_WITH_CONTAINERS = {"Item_Range_Bow", "Item_Range_SlingShot"}
KEPT_FROM_SHOPS = WEAPONS_WITH_CONTAINERS | {"Item_BombBag_Small", "Item_BombchuBundle_Small",
                                             "Item_BombchuBundle_Large", "Item_Bottle", "Item_Bottle_Milk",
                                             "Obj_Armor_Zora"}
GIFT_SITES = read_tsv("gifts.tsv")
GIFT_LOCATIONS = {(row["flow"], row["event"], row["param"]): row["location"] for row in GIFT_SITES}
# With adult trade off, SoH's pool holds the Claim Check and no Pocket Egg, and its own list gives adult Anju's place
# the Claim Check.
POOLED_AS_SOH = {"Kak Anju as Adult": "Item_ClaimCheck"}
GIFT_POOL = collections.Counter(POOLED_AS_SOH.get(row["location"], row["item"])
                                for row in {row["location"]: row for row in GIFT_SITES}.values())
GIFT_GROUPS = ["Minigames", "Characters", "House of Skulltula", "Great Fairies", "Story gifts", "Malon's Egg",
               "Biggoron Quest", "Deku Scrubs (One Time Rewards)"]
# The groups "Shuffled" names, in a seed's settings and in the settings a window remembers.
RELEASED_GIFT_GROUPS = ["Minigames", "Characters", "House of Skulltula", "Story gifts"]
GIFTS_SHUFFLED = {"Gifts": GIFT_GROUPS}
DEKU_SCRUBS_SHUFFLED = {"Gifts": ["Deku Scrubs (One Time Rewards)"]}
# The ten places whose prize or sale makes a bag bigger, and the bigger bag each adds to the shuffle, by SoH's name and
# the remake's.
BAG_PRIZES = {"Market Shooting Gallery": ("Big Deku Seed Bullet Bag", "Item_BulletBag_Medium"),
              "LW Target in Woods": ("Biggest Deku Seed Bullet Bag", "Item_BulletBag_Large"),
              "Kak Shooting Gallery Reward": ("Big Quiver", "Item_Quiver_Medium"),
              "GF HBA 1500 Points": ("Biggest Quiver", "Item_Quiver_Large"),
              "Market Bombchu Bowling First Prize": ("Big Bomb Bag", "Item_BombBag_Medium"),
              "GC Rolling Goron as Child": ("Biggest Bomb Bag", "Item_BombBag_Large"),
              "LW Deku Theater Skull Mask": ("Deku Stick Capacity (20)", "Item_StickBag_Medium"),
              "LW Deku Theater Mask of Truth": ("Deku Nut Capacity (30)", "Item_NutBag_Medium"),
              "LW Deku Scrub Near Bridge": ("Deku Stick Capacity (30)", "Item_StickBag_Large"),
              "LW Deku Scrub Grotto Front": ("Deku Nut Capacity (40)", "Item_NutBag_Large")}
BIGGER_BAGS = dict(BAG_PRIZES.values())
# Each Great Fairy's saved Bool of the mod's own, and the two events of her flow that name it.
FOUNTAIN_CLAIMS = read_tsv("fountain_claims.tsv")
# The items whose find brings the meter to a Link who has none, by SoH's names: a spell, the Lens, magic arrows or a
# blessing.
METER_ITEMS = {"Din's Fire", "Farore's Wind", "Nayru's Love", "Lens of Truth", "Fire Arrows", "Ice Arrows",
               "Light Arrows", "Progressive Magic Meter"}
# What the six Great Fairies give in the game, by their remake names.
FAIRY_ITEMS = {"Item_FairyBless_MP", "Item_DinFire", "Item_FairyBless_MPDouble", "Item_FairyBless_DefenseDouble",
               "Item_FaroreWind", "Item_NayruLove"}
# Three gifts where the story moves, and what each gives in the game.
STORY_GIFTS = {"GF Freed All Carpenters": "Item_Token_Gerudo", "ZD King Zora Thawed": "Obj_Armor_Zora",
               "Market ToT Light Arrow Cutscene": "Item_Arrow_Light"}
# The gifts of Goron City where the story moves, and what each gives in the game.
GORON_GIFTS = {"GC Darunias Joy": "Item_Gauntlet_Goron", "GC Rolling Goron as Adult": "Obj_Armor_Goron"}
GIFT_GROUP = {row["location"]: row["group"] for row in GIFT_SITES}
# What a shuffled group adds that only a chest, a gift's call or a reward's place gives: each item with its group and
# the place that holds it in SoH's own list.
KEPT_IN_CHESTS_AND_CALLS = {"Item_WeirdEgg": ("Malon's Egg", "HC Malon Egg"),
                            "Item_ClaimCheck": ("Biggoron Quest", "Kak Anju as Adult"),
                            "Item_Sword_Biggoron": ("Biggoron Quest", "DMT Trade Claim Check")}
SCRUB_FLOW = "Event/EventFlow/Enemy_Scrub_Business_Common.bfevfl.zs"
REWARD_SITES = read_tsv("rewards.tsv")
REWARD_LOCATIONS = {(row["flow"], row["event"], row["param"]): row["location"] for row in REWARD_SITES}
REWARDS = {row["item"] for row in REWARD_SITES}
REWARDS_SHUFFLED = {"Dungeon rewards": "Shuffled"}
REWARDS_AMONG_THEMSELVES = {"Dungeon rewards": "Shuffled among themselves"}
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
GIFT_PLACES = {row["location"]: row["place"] for row in GIFT_SITES if row["place"]}
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
    {"flow": "Event/EventFlow/Dm_SY_BancChange_EscapeDungeon.bfevfl.zs", "event": "Event208", "param": "ChallengeName",
     "text": "SageOfForest"},
    {"flow": "Event/EventFlow/Dm_SY_BancChange_EscapeDungeon.bfevfl.zs", "event": "Event208", "param": "StepName",
     "text": "Complete"},
    {"flow": "Event/EventFlow/Get_StoneOfWater.bfevfl.zs", "event": "Event80", "param": "ChallengeName",
     "text": "Get_StoneOfFire"},
    {"flow": "Event/EventFlow/Get_StoneOfWater.bfevfl.zs", "event": "Event80", "param": "StepName", "text": "Complete"},
    {"flow": "Event/EventFlow/Get_StoneOfWater.bfevfl.zs", "event": "Event293", "param": "GameDataName",
     "text": "Sequence_IsAlternative_ZoraDomainAdult"},
    {"flow": "Event/EventFlow/Npc_Hylia_Zelda_Child.bfevfl.zs", "event": "Event42", "param": "ChallengeName",
     "text": "Get_StoneOfFire"},
    {"flow": "Event/EventFlow/Npc_Hylia_Zelda_Child.bfevfl.zs", "event": "Event42", "param": "StepName",
     "text": "Complete"},
    {"flow": "Event/EventFlow/Dm_SY_Ocarina_SariaSong.bfevfl.zs", "event": "Event172", "param": "ChallengeName",
     "text": "Get_StoneOfFire"},
    {"flow": "Event/EventFlow/Dm_SY_Ocarina_SariaSong.bfevfl.zs", "event": "Event172", "param": "StepName",
     "text": "Complete"},
    {"flow": "Event/EventFlow/DoorOfTime.bfevfl.zs", "event": "Event38", "param": "GameDataName",
     "text": "Kaepora_BothStones_AND_DoorOfTime_IsAfter_Step01_Exp"},
    {"flow": "Event/EventFlow/DoorOfTime.bfevfl.zs", "event": "Event148", "param": "ChallengeName",
     "text": "Get_StoneOfWater"},
    {"flow": "Event/EventFlow/DoorOfTime.bfevfl.zs", "event": "Event148", "param": "StepName", "text": "Complete"},
    {"flow": "Event/EventFlow/Npc_Hylia_Malon_Adult.bfevfl.zs", "event": "Event179", "param": "GameDataName",
     "text": "Get_Epona_IsAfter_Ready_Exp"},
    {"flow": "Event/EventFlow/Get_StoneOfFire.bfevfl.zs", "event": "Event39", "param": "GameDataName",
     "text": "Kaepora_HavePlayedEvent_Dm_Goron_0030_Exp"},
    {"flow": "Event/EventFlow/MiniGame_DampeRace.bfevfl.zs", "event": "Event295", "param": "InstanceName",
     "text": "ExitLookPos"}]
# The next events every plan carries, in Dampé's race.
DAMPES_RACE_NEXTS = [{"flow": "Event/EventFlow/MiniGame_DampeRace.bfevfl.zs", "event": event, "next": next_event}
                     for event, next_event in (("Event442", "Event33"), ("Event201", "Event35"),
                                               ("Event200", "Event35"), ("Event112", "Event95"),
                                               ("Event121", "Event198"), ("Event277", "Event276"),
                                               ("Event276", "Event353"), ("Event86", "Event56"),
                                               ("Event129", "Event131"), ("Event98", "Event99"),
                                               ("Event226", "Event158"), ("Event415", "Event221"),
                                               ("Event207", "Event221"))]
# The expression of every plan's own that Darunia's switch after his dance asks: his gift's scene played.
DANCE_EXPRESSION = {"name": "Kaepora_HavePlayedEvent_Dm_Goron_0030_Exp", "all_of": ["HavePlayedEvent.Dm_Goron_0030"]}
# The expression the Door of Time's stone check asks in every seed.
STONE_CHECK = {"name": "Kaepora_BothStones_AND_DoorOfTime_IsAfter_Step01_Exp",
               "all_of": ["Get_StoneOfFire_IsCompleted_Exp", "Get_StoneOfWater_IsCompleted_Exp",
                          "DoorOfTime_IsAfter_Step01_Exp"]}
# The expressions of every plan's own that Darunia's son's entries for the Fire quest complete ask: each holds while
# the entry's own flag and his door's flag both do.
SON_EXPRESSIONS = [{"name": f"Kaepora_{flag.removesuffix('_Exp')}_AND_DoorOpen_Exp",
                    "all_of": [flag, "SageOfFire_Step02_DoorOpen"]}
                   for flag in ("SageOfFire_IsCompleted_Exp", "SageOfFire_FromCrater", "SageOfFire_FromCrater_FarJr",
                                "SageOfFire_FromDeathMt", "SageOfFire_FromDeathMt2")]
# King Zora's ice in the domain an adult sees once the Water quest is complete stands there until his thaw has played,
# in every seed.
# The area at the top of the vine climb, which stands only for a Link whose save says he got the Weird Egg while Malon's
# place is shuffled.
VINE_HATCH = {"file": "Banc/HyruleCastle/HyruleCastle_Static.bcett.byml.zs", "hash": "4190489777303842820",
              "flag": "IsGet.Item_WeirdEgg"}
KING_ZORA_ICE = {"file": "Banc/ZoraDomain/ZoraDomain_Adult_Alternative_Static.bcett.byml.zs",
                 "hash": "3341274640047764997", "flag": "HavePlayedEvent.SageOfWater_KingZora_IsMelted_Ice",
                 "replaces": "SageOfWater_IsCompleted_Exp"}
# The seven invisible walls at Zora's River's boulders, in the adult's river, stand only for a child in every seed. They
# stand while both boulders are whole and the Water quest is below Step03, and SoH lets an adult by with nothing.
RIVER_WALLS = [{"file": "Banc/ZoraRiver/ZoraRiver_Adult_Static.bcett.byml.zs", "hash": hash,
                "flag": "Sequence_IsChildhood"}
               for hash in ("7850395794748438732", "13781426553798151043", "7982339630050931435",
                            "14077907143773739344", "9371429508151251838", "11266001672277786960",
                            "4565321854739336115")]
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
# The items that shuffled places name for those steps: chests, and with gifts shuffled Darunia's bracelet, the scales,
# the wallets and the bigger and biggest bags.
STEP_COPIES = ["Item_Range_SlingShot", "Item_Range_Bow", "Item_BombBag_Small", "Item_Hookshot", "Item_Longshot",
               "Item_Gauntlet_Goron", "Item_Gauntlet_Silver", "Item_Gauntlet_Gold", "Item_Scale_Silver",
               "Item_Scale_Gold", "Item_Wallet_Medium", "Item_Wallet_Large", "Item_BulletBag_Medium",
               "Item_BulletBag_Large", "Item_Quiver_Medium", "Item_Quiver_Large", "Item_BombBag_Medium",
               "Item_BombBag_Large", "Item_StickBag_Medium", "Item_StickBag_Large", "Item_NutBag_Medium",
               "Item_NutBag_Large"]
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
# Every place of the remake's tables, shuffled or kept as the game has it.
EVERY_PLACE = {row["location"] for table in (read_tsv("chests.tsv"), SHOP_SLOTS, GIFT_SITES, REWARD_SITES, SKULLTULAS,
                                             LOOSE_ITEMS, SONG_PLACES) for row in table}
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
DOOR_MODULE = "Logic/Market_Common_f252.logic.module.ainb"
FIRST_PULL_MODULE = "Logic/Market_Child_Prefab_e700.logic.module.ainb"
DOOR_OF_TIME_QUEST = "Work/Challenge/DoorOfTime.game__challenge__Challenge.gyml"
ALL_SAGE_QUEST = "Work/Challenge/AllSage.game__challenge__Challenge.gyml"
SOH_MAJOR_ITEMS = {"Boomerang", "Progressive Bow", "Progressive Slingshot", "Progressive Hookshot",
                   "Megaton Hammer", "Iron Boots", "Hover Boots", "Lens of Truth", "Silver Gauntlets",
                   "Golden Gauntlets", "Mirror Shield", "Kokiri Sword", "Progressive Bomb Bag", "Ice Arrows"}
BOX_LINES = 2
LINE_LENGTH = 65


def folder(path):
    return path.split("/")[1]


def without_skip(log):
    return log | {"settings": {key: value for key, value in log["settings"].items() if key != "Skip"}}


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
    """Where the remake has a location: the place remake/skulltulas.tsv or remake/gifts.tsv gives one that stands
    elsewhere, or its SoH area."""
    return SKULLTULA_PLACES.get(location) or GIFT_PLACES.get(location) or soh_area(location)


def plain(text):
    text = re.sub(r"\{page\}|[\n^&]", " ", text).replace("@", "Link")
    return " ".join(re.sub(r"\{[^}]*\}|#", "", text).split())


def sales(plan):
    """The plan's shop slots that sell an item, once."""
    return [entry for entry in plan["shops"] if "item" in entry]


def kept_goods(plan):
    """The plan's shop slots that sell the game's goods."""
    return [entry for entry in plan["shops"] if "goods" in entry]


def shop_rows(entries):
    """The shops.tsv row of each of the plan's shop slots given, in their order."""
    rows = {json.dumps(shop_slot(row), sort_keys=True): row for row in SHOP_SLOTS}
    return [rows[json.dumps({key: entry[key] for key in ("shopkeepers", "slot", "shelves")}, sort_keys=True)]
            for entry in entries]


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
    placed |= {row["location"]: entry["item"] for row, entry in zip(shop_rows(sales(plan)), sales(plan))}
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
        first, _ = self.generate("1", CHESTS_ALONE)
        second, _ = self.generate("2", CHESTS_ALONE)
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
        _, plan = self.generate("3", CHESTS_ALONE)
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
        spoiler, plan = self.generate("4", CHESTS_ALONE)
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
        # The chests come first, and a location whose chest the files keep twice is listed once.
        chests = list(dict.fromkeys(row["location"] for row in SHUFFLED))
        self.assertEqual(list(spoiler["locations"])[:len(chests)], chests)
        self.assertEqual(list(spoiler["playthrough"].values())[-1], {"Ganon": "Triforce"})

    def test_a_seed_with_no_settings_shuffles_what_sohs_default_seed_shuffles(self):
        # SoH's default seed shuffles every gift place but Malon's, whose Weird Egg starts at Vanilla, the loose items,
        # the songs among the song places and the dungeon rewards among the places that hand one over. It leaves the
        # Gold Skulltulas and the shops' goods where the game has them.
        spoiler, plan = self.generate("1")
        listed = spoiler["locations"].keys()
        malons = {row["location"] for row in GIFT_SITES if row["group"] == "Malon's Egg"}
        self.assertLessEqual({row["location"] for row in GIFT_SITES} - malons, listed)
        self.assertLessEqual({row["location"] for row in LOOSE_ITEMS}, listed)
        self.assertLessEqual({row["location"] for row in SONG_PLACES}, listed)
        self.assertEqual(sorted(dict(reward_sites(plan)).values()), sorted(REWARDS))
        for kept in (malons, {row["location"] for row in SKULLTULAS}, {row["location"] for row in SHOP_SLOTS}):
            self.assertFalse(kept & listed, kept)
        self.assertEqual((plan["skulltulas"], plan["shops"]), ([], []))

    def test_route_names_no_gold_skulltula_token(self):
        # Any ten tokens buy the same Skulltula House reward, so the route names no token.
        self.assertNotIn("Gold Skulltula Token", playthrough_steps(self.generate(FIXED_SEED)[0]).values())

    def test_route_takes_what_link_is_handed_over_what_he_buys(self):
        # The stored seed's route needs a Goron Tunic, which the rolling Goron gives adult Link. The Goron City shop sells
        # one for 600 rupees, which take both wallets the Skulltula House gives.
        steps = playthrough_steps(self.generate(FIXED_SEED, CHESTS_ALONE)[0])
        self.assertEqual(steps["GC Rolling Goron as Adult"], "Goron Tunic")
        self.assertNotIn("GC Shop Item 5", steps)
        self.assertNotIn("Kak 10 Gold Skulltula Reward", steps)
        self.assertNotIn("Kak 30 Gold Skulltula Reward", steps)

    def test_route_names_each_boss_before_the_steps_that_wait_for_it(self):
        # The game's gates wait for the bosses, not for their rewards. The stored seed's route takes the Ocarina of
        # Time, the Nocturne and the Light Arrows, so it names in an earlier sphere each boss their rules ask for.
        log, _ = self.generate(FIXED_SEED, CHESTS_ALONE)
        sphere = {step: number for number, steps in enumerate(log["playthrough"].values()) for step in steps}
        waits = {"HF Ocarina of Time Item": ("King Dodongo", "Barinade"),
                 "Song from Ocarina of Time": ("King Dodongo", "Barinade"),
                 "Sheik in Kakariko": ("Phantom Ganon", "Volvagia", "Morpha"),
                 "Market ToT Light Arrow Cutscene": ("Bongo Bongo", "Twinrova")}
        for step, bosses in waits.items():
            for boss in bosses:
                self.assertIn(boss, sphere, step)
                self.assertLess(sphere[boss], sphere[step], (boss, step))

    def test_a_route_with_the_forest_exit_open_may_never_enter_the_deku_tree(self):
        # Closed Deku opens the way out of the forest.
        steps = playthrough_steps(self.generate("1", CHESTS_ALONE | {"Kokiri Forest": "Closed Deku"})[0])
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
        # Paring a route keeps what each later step counts on.
        log, _ = self.generate("1")
        self.assertEqual(self.route_out_of_reach(log), [])

    def test_a_seller_left_off_the_route_stands_in_for_no_step(self):
        # The route names one seller of each purchase, the first it reaches. With the Door of Time open and the dungeon
        # rewards shuffled, seed 19's route buys Bombchus at the Market's Bombchu Shop, which opens after Dodongo's
        # Cavern. The Haunted Wasteland's carpet merchant, who does not wait for the cavern, sells them too.
        log, _ = self.generate("19", CHESTS_ALONE | {"Door of Time": "Open", "Dungeon rewards": "Shuffled"})
        self.assertTrue([step for step in playthrough_steps(log) if step.startswith("Market Bombchu Shop")])
        self.assertNotIn("Wasteland Carpet Salesman", playthrough_steps(log))
        self.assertEqual(list(log["playthrough"].values())[-1], {"Ganon": "Triforce"})
        self.assertEqual(self.route_out_of_reach(log), [])

    def test_a_route_that_goes_back_to_child_before_the_forest_temple_is_in_reach_in_its_sphere(self):
        # Only child Link drains the well, with the Song of Storms that only adult Link learns. Seed 1's route takes a
        # chest in the well before Phantom Ganon.
        log, _ = self.generate("1", CHESTS_ALONE)
        spheres = list(log["playthrough"].values())
        forest_temple = next(number for number, steps in enumerate(spheres) if "Phantom Ganon" in steps)
        self.assertTrue(any(step.startswith("Bottom of the Well") for steps in spheres[:forest_temple] for step in steps))
        self.assertEqual(self.route_out_of_reach(log), [])

    def test_a_route_warps_a_child_to_the_colossus_only_after_a_song_that_takes_him_out(self):
        # A child cannot walk out of the Desert Colossus, and only the Requiem takes him there. With keys not needed,
        # seed 2's route takes steps that need the Requiem. Its adult Link holds the Longshot and the Hover Boots by
        # then, so he walks to the Colossus, and the child is the one who needs the song.
        log, _ = self.generate("2", CHESTS_ALONE | KEYS_NOT_NEEDED)
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
        # Seed 3's route buys at shop slots.
        log, _ = self.generate("3", SHOPS_SHUFFLED)
        self.assertTrue(any(isinstance(log["locations"].get(step), dict)
                            for steps in log["playthrough"].values() for step in steps))
        self.assertEqual(self.route_out_of_reach(log), [])

    def test_plan_starts_what_every_seed_starts(self):
        _, plan = self.generate("6", CHESTS_ALONE)
        self.assertEqual(plan["start_values"], DEFAULT_START_VALUES)

    def test_plan_keeps_quests_from_going_back(self):
        _, plan = self.generate("12", CHESTS_ALONE)
        self.assertEqual(plan["event_texts"], QUEST_TEXTS)
        self.assertEqual(plan["event_numbers"], [])
        self.assertEqual(plan["expressions"], [DANCE_EXPRESSION, STONE_CHECK, *SON_EXPRESSIONS])

    def test_plan_keeps_king_zoras_ice_until_he_is_thawed(self):
        # The game's ice in the domain after the Water Temple never stands, so a Link who finished the temple first
        # would never get what King Zora hands over.
        for settings, presences in ((CHESTS_ALONE, [KING_ZORA_ICE] + RIVER_WALLS),
                                    (CHESTS_ALONE | {"Gifts": GIFT_GROUPS},
                                     [KING_ZORA_ICE] + RIVER_WALLS + [VINE_HATCH])):
            _, plan = self.generate("12", settings)
            self.assertEqual(plan["presences"], presences, settings)

    def test_an_adult_passes_zoras_rivers_boulders_before_the_water_quest(self):
        # With the Door of Time open, an adult can reach the river before the child's Water quest, and the walls would
        # then ask him for a bomb or the hammer.
        for settings in (None, {"Door of Time": "Open"}):
            _, plan = self.generate("12", settings)
            self.assertEqual(plan["presences"][1:8], RIVER_WALLS, settings)

    def test_scrub_seeds_and_the_large_magic_jar_are_placed(self):
        # SoH has neither item. Each shuffled chest that holds one gives it to the pool, named as the game names it.
        spoiler, _ = self.generate("7", CHESTS_ALONE)
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

    def test_dampes_first_chest_holds_what_the_seed_places_there(self):
        # Every race ends at the first chest until it is opened, whatever Link owns, so the chest is shuffled, and
        # Dampé names what it holds as he gives it.
        held = set()
        for seed in range(220, 230):
            spoiler, plan = self.generate(str(seed))
            self.assertIn("16041366920820397198", {entry["hash"] for entry in plan["chests"]})
            item = placed_remake_items(spoiler, plan)["Graveyard Hookshot Chest"]
            (line,) = [entry["text"] for entry in plan["message_texts"]
                       if (entry["file"], entry["label"]) == ("EventFlowMsg/Npc_Hylia_Dampe_002.msbt", "Talk_03")]
            self.assertIn(f"I call it... {ITEM_NAMES[item]}!", plain(line), seed)
            held.add(item)
        self.assertGreater(len(held), 1)

    def test_dins_fire_comes_from_the_summit_great_fairy(self):
        # She gives Din's Fire with the magic meter. The castle's Great Fairy gives nothing until Link holds it.
        steps = playthrough_steps(self.generate("9", CHESTS_ALONE)[0])
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
                                  ({"Gifts": ["Minigames", "Fishing"]}, "Fishing"),
                                  ({"Skip": ["Opening movie", "Credits"]}, "Credits"),
                                  ({"Skip": [], "Opening": "Played"}, "Opening: applies only")):
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
                              (SHOPS_SHUFFLED | {"Shop prices": "Between two prices", "Shop price": 100}, "Shop price"),
                              ({"Shop goods": "Goods shuffled among the shops", "Shop prices": "Cheap"},
                               "Shop prices")):
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
                                  {"Shop prices": "Between two prices", "Shop price range": [20, 200]}),
                                 ({"Shop goods": "Goods shuffled among the shops"}, {})):
            spoiler, _ = self.generate("12", settings)
            self.assertEqual({key: value for key, value in spoiler["settings"].items() if key.startswith("Shop pric")},
                             prices)

    def test_a_dungeon_left_out_keeps_its_chests(self):
        left_out = {"Forest Temple": "ForestTemple", "Deku Tree": "InsideTheDekuTree"}
        kept = [row for row in SHUFFLED if folder(row["files"]) in left_out.values()]
        self.assertTrue(kept)
        settings = {"Shuffled dungeons": [name for name in DUNGEONS if name not in left_out]}
        self.assert_kept_in_place(kept, CHESTS_ALONE | settings, [str(seed) for seed in range(300, 310)],
                                  on_route=True)

    def test_keys_stay_in_their_own_chests(self):
        kept = [row for row in SHUFFLED if row["item"] in KEYS]
        self.assertTrue(kept)
        self.assert_kept_in_place(kept, CHESTS_ALONE | {"Small and boss keys": "In their own chests"},
                                  [str(seed) for seed in range(310, 320)], on_route=True)

    def test_maps_and_compasses_stay_in_their_own_chests(self):
        kept = [row for row in SHUFFLED if row["item"] in MAPS_AND_COMPASSES]
        self.assertTrue(kept)
        self.assert_kept_in_place(kept, CHESTS_ALONE | {"Maps and compasses": "In their own chests"},
                                  [str(seed) for seed in range(320, 330)], on_route=False)

    def test_forest_open(self):
        spoiler, plan = self.generate("16", CHESTS_ALONE | {"Kokiri Forest": "Open"})
        self.assertEqual(spoiler["settings"]["Kokiri Forest"], "Open")
        self.assertEqual(plan["start_values"],
                         DEFAULT_START_VALUES | {"KokiriForest_Npc_Kokiri_Mido_Child_Blocker": False,
                                                 "KokiriForest_Npc_Kokiri_007_Child_Blocker_00": False})

    def test_forest_closed_deku(self):
        spoiler, plan = self.generate("19", CHESTS_ALONE | {"Kokiri Forest": "Closed Deku"})
        self.assertEqual(spoiler["settings"]["Kokiri Forest"], "Closed Deku")
        self.assertEqual(plan["start_values"],
                         DEFAULT_START_VALUES | {"KokiriForest_Npc_Kokiri_007_Child_Blocker_00": False})

    def test_an_open_door_of_time_opens_the_door_and_the_first_pull_and_keeps_the_quest(self):
        # The door opens once AllSage is at Ready, true in every new game, and the child's first pull waits for AllSage
        # at Ready exactly, true until that pull's last scene. Rauru's write of the Door of Time's quest names the
        # Light sage's quest, which the next scene completes, so the child's story keeps its step.
        door = [{"file": DOOR_MODULE, "from": DOOR_OF_TIME_QUEST, "to": ALL_SAGE_QUEST},
                {"file": DOOR_MODULE, "from": "Step06", "to": "Ready"},
                {"file": FIRST_PULL_MODULE, "from": DOOR_OF_TIME_QUEST, "to": ALL_SAGE_QUEST},
                {"file": FIRST_PULL_MODULE, "from": "Step06", "to": "Ready"}]
        rauru = {"flow": "Event/EventFlow/DoorOfTime.bfevfl.zs", "event": "Event93", "param": "ChallengeName",
                 "text": "SageOfLight"}
        spoiler, plan = self.generate("21", {"Door of Time": "Open"})
        self.assertEqual(spoiler["settings"]["Door of Time"], "Open")
        self.assertEqual(plan["module_texts"], door)
        self.assertIn(rauru, plan["event_texts"])
        spoiler, plan = self.generate("21")
        self.assertEqual(spoiler["settings"]["Door of Time"], "Closed")
        self.assertEqual(plan["module_texts"], [])
        self.assertNotIn(rauru, plan["event_texts"])

    def test_only_closed_deku_removes_midos_castle_entry(self):
        for choice, removed in (("Closed", False), ("Closed Deku", True), ("Open", False)):
            _, plan = self.generate("19", {"Kokiri Forest": choice})
            self.assertIs(plan["mido_castle_entry_removed"], removed, choice)

    def assert_skip_adds_only_its_parts(self, skip, with_parts):
        others = [name for name in SKIPS if name != skip]
        spoiler, plan = self.generate("20")
        played, plan_played = self.generate("20", {"Skip": others})
        self.assertEqual((spoiler["settings"]["Skip"], played["settings"]["Skip"]), (SKIPS, others))
        self.assertEqual(without_skip(spoiler), without_skip(played))
        self.assertNotEqual(plan, plan_played)
        unordered = lambda plan: {key: sorted(value, key=json.dumps) if isinstance(value, list) else value
                                  for key, value in plan.items()}
        self.assertEqual(unordered(plan), unordered(with_parts(plan_played)))

    def test_the_opening_movie_is_skipped_while_listed(self):
        self.assert_skip_adds_only_its_parts("Opening movie", lambda plan: plan | {"opening_skipped": True})

    def test_player_skippable_scenes_are_skipped_while_listed(self):
        self.assert_skip_adds_only_its_parts("Player skippable scenes", lambda plan: plan | {"cutscenes_skipped": True})

    def test_new_area_scenes_are_cut_while_listed(self):
        rows = read_tsv("area_scenes.tsv")
        entries = [{"flow": row["flow"], "entry": row["from"], "event": row["to"]}
                   for row in rows if row["kind"] == "entry"]
        nexts = [{"flow": row["flow"], "event": row["from"], "next": row["to"] or None}
                 for row in rows if row["kind"] == "next"]
        self.assert_skip_adds_only_its_parts("New area scenes", lambda plan: plan | {
            "event_entries": plan["event_entries"] + entries, "event_nexts": plan["event_nexts"] + nexts})

    def test_tutorial_messages_are_skipped_while_listed(self):
        self.assert_skip_adds_only_its_parts("Tutorial messages", lambda plan: plan | {
            "start_values": plan["start_values"] | TUTORIALS, "event_nexts": plan["event_nexts"] + TUTORIAL_NEXTS})

    def test_sarias_greeting_is_skipped_while_listed(self):
        self.assert_skip_adds_only_its_parts(
            "Saria's greeting", lambda plan: plan | {"start_values": plan["start_values"] | SARIAS_GREETING})

    def test_bridge_always_open(self):
        spoiler, plan = self.generate("17", CHESTS_ALONE | {"Rainbow Bridge": "Always open"})
        self.assertEqual(spoiler["settings"]["Rainbow Bridge"], "Always open")
        self.assertEqual(plan["start_values"],
                         DEFAULT_START_VALUES | {"HavePlayedEvent.GanonCastle_Adult_Field_Rainbow": True})

    def test_no_trials(self):
        spoiler, plan = self.generate("18", CHESTS_ALONE | {"Ganon's trials": "None"})
        self.assertEqual(spoiler["settings"]["Ganon's trials"], "None")
        trials = ("Forest", "Water", "Shadow", "Fire", "Light", "Spirit")
        done = {"InsideGanonCastle_Dungeon_Barrier_Count": 0, "InsideGanonCastle_IsAllBarrierClear": True,
                "HavePlayedEvent.Dm_InsideGanonCastle_0090": True, "InsideGanonCastle_Dungeon_SeeBarrierGuide": True}
        for trial in trials:
            done[f"InsideGanonCastle_Dungeon_BarrierRemove_{trial}"] = True
            done[f"HavePlayedEvent.InsideGanonCastle_Dungeon_BarrierRemove_{trial}"] = True
        self.assertEqual(plan["start_values"], DEFAULT_START_VALUES | done)

    def test_keys_not_needed(self):
        spoiler, plan = self.generate("15", CHESTS_ALONE | {"Small and boss keys": "Not needed"})
        self.assertEqual(spoiler["settings"]["Small and boss keys"], "Not needed")
        doors = {f"IsChainLockDoorOpen.{door}": True for door in SMALL_KEY_DOORS}
        boss_keys = {f"DungeonInfo.{dungeon}.IsGetBossKey": True for dungeon in BOSS_KEY_DUNGEONS}
        self.assertEqual(len(doors), 37)
        self.assertEqual(plan["start_values"], DEFAULT_START_VALUES | doors | boss_keys)
        self.assertEqual({entry["hash"] for entry in plan["chests"]}, {row["hash"] for row in SHUFFLED})

    def test_maps_and_compasses_given_at_the_start(self):
        spoiler, plan = self.generate("14", CHESTS_ALONE | {"Maps and compasses": "Given at the start"})
        self.assertEqual(spoiler["settings"]["Maps and compasses"], "Given at the start")
        given = {f"DungeonInfo.{dungeon}.{value}": True for dungeon in MAP_DUNGEONS
                 for value in ("IsGetMap", "IsGetCompass")}
        self.assertEqual(plan["start_values"], DEFAULT_START_VALUES | given)
        self.assertEqual({entry["hash"] for entry in plan["chests"]}, {row["hash"] for row in SHUFFLED})

    def test_lens_never_needed_leaves_the_lens_off_the_route(self):
        for seed in range(330, 345):
            needed, _ = self.generate(str(seed), CHESTS_ALONE)
            never, _ = self.generate(str(seed), CHESTS_ALONE | {"Lens of Truth": "Never needed"})
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
        """The plan's shop slots that sell an item are the given rows, in table order, each with the spoiler log's item
        and a cheap price, and the shuffled chests and slots hold the pool and a Red Rupee for each slot. Every other
        slot sells one of SoH's goods at the game's price, the first of SoH's list."""
        self.assertEqual(shop_rows(sales(plan)), slots)
        for row, entry in zip(slots, sales(plan)):
            placed = spoiler["locations"][row["location"]]
            self.assertEqual(list(placed), ["item", "price"], row["location"])
            self.assertIn((entry["item"], placed["item"]), ITEM_PAIRS, row["location"])
            # SoH's cheap balanced prices.
            self.assertEqual(entry["price"], placed["price"])
            self.assertIn(entry["price"], range(0, 100, 5))
        kept = [row for row in SHOP_SLOTS if row not in slots]
        self.assertEqual(shop_rows(kept_goods(plan)), kept)
        held = [spoiler["locations"][row["location"]] for row in kept]
        self.assertEqual(collections.Counter(placed["item"] for placed in held),
                         collections.Counter(map(goods_name, SOH_KEPT_GOODS[:len(kept)])))
        for row, entry, placed in zip(kept, kept_goods(plan), held):
            goods = KEPT_GOODS[placed["item"]]
            self.assertEqual((entry["goods"], placed["price"]), (goods["goods"], int(goods["price"])), row["location"])
        rows = {row["hash"]: row for row in SHUFFLED}
        chests = {rows[entry["hash"]]["location"]: entry["drop_actor"] for entry in plan["chests"]}
        self.assertEqual(collections.Counter(chests.values()) + collections.Counter(e["item"] for e in sales(plan)),
                         POOL + collections.Counter({"TBox_Rupee_Red": len(slots)}))

    def test_each_count_shuffles_the_first_goods_of_every_shop_in_its_order(self):
        for words, count in SHOP_COUNTS.items():
            with self.subTest(words):
                spoiler, plan = self.generate("19", CHESTS_ALONE | {"Shop goods": words})
                self.assertEqual(spoiler["settings"]["Shop goods"], words)
                self.assert_shop_slots_sell_the_pool(spoiler, plan, shuffled_shop_slots(count))

    def test_a_random_number_per_shop_shuffles_one_to_seven_of_its_first_goods(self):
        counts = collections.Counter()
        for seed in range(520, 530):
            spoiler, plan = self.generate(str(seed), CHESTS_ALONE | SHOPS_AT_RANDOM)
            self.assertEqual(spoiler["settings"]["Shop goods"], "A random number per shop")
            rows = shop_rows(sales(plan))
            numbers = collections.defaultdict(set)
            for row in rows:
                numbers[shop_of(row)].add(int(row["number"]))
            self.assertEqual(set(numbers), {shop_of(row) for row in SHOP_SLOTS}, seed)
            for shop, taken in numbers.items():
                self.assertIn(len(taken), range(1, 8), (seed, shop))
                self.assertEqual(taken, set(SHOPSANITY_ORDER[:len(taken)]), (seed, shop))
                counts[len(taken)] += 1
            self.assertEqual(rows, [row for row in SHOP_SLOTS if row in rows], seed)
            self.assert_shop_slots_sell_the_pool(spoiler, plan, rows)
        self.assertEqual(set(counts), set(range(1, 8)))

    def test_seven_per_shop_sells_a_deku_shield_a_child_in_a_closed_forest_reaches(self):
        # The way out of a closed forest waits for Queen Gohma, whose door asks for a nut sent back with a Deku Shield,
        # and the logic counts only a shield Link buys. The one shelf each shop keeps at seven sells it somewhere.
        for seed in map(str, range(19, 24)):
            spoiler, _ = self.generate(seed, SEVEN_PER_SHOP)
            self.assertEqual(spoiler["settings"]["Kokiri Forest"], "Closed")
            self.assertEqual([spoiler_item(placed) for placed in spoiler["locations"].values()].count(
                "Buy Deku Shield"), 1, seed)
            self.assertTrue(reach.reachable("Queen Gohma", *spoiler["locations"].items(),
                                            settings=spoiler["settings"]), seed)

    def test_a_started_fairy_ocarina_with_shop_goods_shuffled_makes_a_seed(self):
        # A child in a closed forest learns no song before he leaves it, so the ocarina he starts with takes him
        # nowhere until then. Seed 2's route buys the Deku Shield from a kept shelf before its first song.
        log, _ = self.generate("2", SHOPS_SHUFFLED | {"Gifts": [], "Start with": ["Fairy Ocarina"]})
        given = {spoiler_item(item): number for number, steps in enumerate(log["playthrough"].values())
                 for item in steps.values()}
        self.assertLess(given["Buy Deku Shield"], min(given[song] for song in SONGS if song in given))
        self.assertEqual(list(log["playthrough"].values())[-1], {"Ganon": "Triforce"})
        self.assertEqual(self.route_out_of_reach(log), [])

    def shop_prices(self, seed, settings):
        spoiler, plan = self.generate(seed, SHOPS_SHUFFLED | settings)
        self.assertEqual({key: spoiler["settings"][key] for key in settings}, settings)
        prices = {row["location"]: entry["price"] for row, entry in zip(shop_rows(sales(plan)), sales(plan))}
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
            sold.update(entry["item"] for entry in sales(plan))
        self.assertFalse(sold.keys() & (KEPT_FROM_SHOPS | KEYS | MAPS_AND_COMPASSES), sold)

    def test_gifts_shuffled(self):
        spoiler, plan = self.generate("21", CHESTS_ALONE | GIFTS_SHUFFLED)
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

    def test_shuffled_is_every_gift_group_of_its_release_and_vanilla_is_none(self):
        # No release shuffled an egg, a Deku Scrub or a Great Fairy.
        for seed in ("21", "22"):
            self.assertEqual(self.run_seedgen(seed, {"Gifts": RELEASED_GIFT_GROUPS}),
                             self.run_seedgen(seed, {"Gifts": "Shuffled"}))
            self.assertEqual(self.run_seedgen(seed, {"Gifts": []}), self.run_seedgen(seed, {"Gifts": "Vanilla"}))

    def test_a_gift_group_named_twice_is_named_once(self):
        self.assertEqual(self.run_seedgen("21", {"Gifts": ["Minigames", "Minigames"]}),
                         self.run_seedgen("21", {"Gifts": ["Minigames"]}))

    def test_with_characters_alone_no_place_gives_a_bigger_stick_or_nut_bag(self):
        # The Deku theater and the bag scrubs give a bag by what Link holds: the bigger to a Link without it, the
        # biggest to a Link with it. With the theater's prizes shuffled and the scrubs kept, a Link who bought from a
        # scrub first would get the bigger bag there and again from the theater's placed prize, and never the biggest.
        # So the theater and the scrubs share a group, and Characters keeps the theater as the game has it.
        bags = {"Item_StickBag_Medium", "Item_StickBag_Large", "Item_NutBag_Medium", "Item_NutBag_Large"}
        for seed in map(str, range(1, 11)):
            spoiler, plan = self.generate(seed, {"Gifts": ["Characters"]})
            self.assertFalse(bags & set(placed_remake_items(spoiler, plan).values()), seed)

    def test_a_shuffled_scrub_offers_what_it_sells(self):
        # The crater's scrub and the field's show one offer, so the crater's sale shows a copy of it under a label of
        # its own, at the event the table names.
        lines = [row for row in PRIZE_LINES if GIFT_GROUP.get(row["location"]) == DEKU_SCRUBS_SHUFFLED["Gifts"][0]]
        self.assertEqual({row["location"] for row in lines}, {"LW Deku Scrub Near Bridge", "LW Deku Scrub Grotto Front",
                                                              "HF Deku Scrub Grotto", "DMC Deku Scrub"})
        copies = [row for row in lines if row["copy_of"]]
        self.assertEqual([row["location"] for row in copies], ["DMC Deku Scrub"])
        for seed in ("33", "34"):
            spoiler, plan = self.generate(seed, DEKU_SCRUBS_SHUFFLED)
            held = placed_remake_items(spoiler, plan)
            written = {(entry["file"], entry["label"]): entry for entry in plan["message_texts"]}
            for row in lines:
                entry = written[(row["file"], row["label"])]
                self.assertEqual(entry.get("copy_of", ""), row["copy_of"], row["label"])
                self.assertEqual(plain(entry["text"]),
                                 plain(row["sentence"].replace("[[1]]", ITEM_NAMES[held[row["location"]]])), seed)
            for row in copies:
                flow, event, param = row["shown_at"].split(":")
                self.assertIn({"flow": flow, "event": event, "param": param,
                               "text": row["file"].removesuffix(".msbt") + ":" + row["label"]}, plan["event_texts"])
            # With the scrubs as the game has them, their offers are the game's.
            _, kept = self.generate(seed, {"Gifts": RELEASED_GIFT_GROUPS})
            self.assertFalse({(row["file"], row["label"]) for row in lines}
                             & {(entry["file"], entry["label"]) for entry in kept["message_texts"]})
            self.assertFalse([entry for entry in kept["event_texts"] if entry["flow"] == SCRUB_FLOW])

    def test_one_gift_group_shuffles_its_places_alone(self):
        kept_on_route = 0
        for group in GIFT_GROUPS:
            spoiler, plan = self.generate("21", CHESTS_ALONE | {"Gifts": [group]})
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
                             POOL + collections.Counter(POOLED_AS_SOH.get(location, vanilla[location])
                                                        for location in places), group)
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
            sold.update(entry["item"] for entry in sales(plan))
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
        # Seed 10's route takes the Iron Boots from the crater's scrub, a Hookshot from the frogs in the rain and the
        # Goron Tunic from Talon's chickens, among other gifts.
        log, plan = self.generate("10", GIFTS_SHUFFLED)
        vanilla = {row["location"]: row["item"] for row in GIFT_SITES}
        self.assertTrue(any(location in playthrough_steps(log) and item != vanilla[location]
                            for location, item in gift_sites(plan)))
        self.assertEqual(list(log["playthrough"].values())[-1], {"Ganon": "Triforce"})
        self.assertEqual(self.route_out_of_reach(log), [])

    def test_hints_name_gifts_truly(self):
        # Seed 26 hints at three gifts by their sentences: the Colossus's Great Fairy, the Deku Theater's Skull Mask
        # prize and Zelda's scene in the Temple of Time.
        spoiler, plan = self.assert_hints_are_true("26", CHESTS_ALONE | GIFTS_SHUFFLED)
        sentences = {plain(CHEST_SENTENCES[location].replace("[[1]]", ITEM_NAMES[item]))
                     for location, item in gift_sites(plan)}
        self.assertTrue(sentences & set(spoiler["hints"].values()))

    def test_each_bag_prize_gives_its_placed_item_at_every_site(self):
        spoiler, plan = self.generate("31", GIFTS_SHUFFLED)
        sites = collections.defaultdict(list)
        for location, item in gift_sites(plan):
            sites[location].append(item)
        for location in BAG_PRIZES:
            self.assertEqual(len(sites[location]), list(GIFT_LOCATIONS.values()).count(location), location)
            self.assertGreater(len(sites[location]), 1, location)
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
            sold.update(entry["item"] for entry in sales(plan))
            landed += sum(spoiler_item(placed) in BIGGER_BAGS for placed in spoiler["locations"].values())
        self.assertFalse(sold.keys() & set(BIGGER_BAGS.values()), sold)
        self.assertEqual(landed, 10 * len(BIGGER_BAGS))

    def assert_bags_count_for_nothing(self, item, bags, target, others=()):
        """Some seed puts one of the bags in reach before any copy of the item. With every copy held back, and every
        copy of the others that also reach the target, the target stays out of reach, and the item where that bag lies
        would reach it."""
        for seed in map(str, range(1420, 1560)):
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

    def test_the_eggs_the_receipt_and_the_sword_go_only_to_a_chest_a_gift_or_a_reward_place(self):
        # With its group ticked each is an item of the pool. No copy of one on the ground or on a Gold Skulltula, and
        # no shelf, has been seen to give one, so each goes to a chest, a gift or a reward's place, as the Gerudo Pass
        # does.
        settings = ({"Gifts": sorted({group for group, _ in KEPT_IN_CHESTS_AND_CALLS.values()})} | REWARDS_SHUFFLED
                    | SHOPS_SHUFFLED | SKULLTULAS_SHUFFLED | LOOSE_SHUFFLED)
        chests = {row["location"] for row in SHUFFLED}
        kinds, moved = collections.defaultdict(set), set()
        for seed in map(str, range(660, 680)):
            spoiler, plan = self.generate(seed, settings)
            sites = dict(gift_sites(plan))
            placed = placed_remake_items(spoiler, plan)
            for item, (_, place) in KEPT_IN_CHESTS_AND_CALLS.items():
                self.assertIn((sites[place], spoiler["locations"][place]), ITEM_PAIRS, (seed, place))
                at = [location for location, held in placed.items() if held == item]
                self.assertEqual(len(at), 1, (seed, item))
                kinds[item].add("chest" if at[0] in chests else "gift" if at[0] in GIFT_LOCATIONS.values()
                                else "reward place" if at[0] in REWARD_AREAS else at[0])
                if at[0] != place:
                    moved.add(item)
        self.assertEqual(moved, KEPT_IN_CHESTS_AND_CALLS.keys())
        for item, found in kinds.items():
            self.assertLessEqual(found, {"chest", "gift", "reward place"}, item)

    def test_biggoron_quest_shuffles_its_places_as_soh_does_with_adult_trade_off(self):
        # SoH's pool with adult trade off holds the Claim Check and Biggoron's sword and no Pocket Egg, and Biggoron's
        # place asks for the receipt, so no route takes a step of the chain. With his place shuffled, the plan has him
        # give once without completing the trade quest and starts the record his wait counts from.
        # Mido's hint sends an event of his own flow on, so these seeds leave his hint out.
        on_route = 0
        for seed in map(str, range(2000, 2020)):
            spoiler, plan = self.generate(seed, {"Gifts": ["Biggoron Quest"], **NO_CHARACTER_HINTS})
            placed = list(placed_remake_items(spoiler, plan).values())
            self.assertEqual([placed.count(item) for item in ("Item_ClaimCheck", "Item_Sword_Biggoron")], [1, 1])
            self.assertNotIn("Item_PocketEgg", placed)
            steps = playthrough_steps(spoiler)
            self.assertFalse([step for step in steps if "Trade" in step and step != "DMT Trade Claim Check"], seed)
            on_route += "DMT Trade Claim Check" in steps
            # His own expression reads his claim.
            own = [entry for entry in plan["expressions"] if entry["all_of"][0] in plan["claims"]]
            self.assertTrue([entry for entry in plan["event_nexts"] if entry not in DAMPES_RACE_NEXTS] and own, seed)
            self.assertEqual(plan["start_values"]["GetSwordBigGoron_ElapsedTime"], 1)
            self.assertEqual(self.route_out_of_reach(spoiler), [])
        self.assertTrue(on_route)
        _, plan = self.generate("2000", {"Gifts": [group for group in GIFT_GROUPS if group != "Biggoron Quest"],
                                         **NO_CHARACTER_HINTS})
        _, no_gifts = self.generate("2000", {"Gifts": [], **NO_CHARACTER_HINTS})
        self.assertEqual((plan["event_nexts"], plan["expressions"]),
                         (no_gifts["event_nexts"], [DANCE_EXPRESSION, STONE_CHECK, *SON_EXPRESSIONS]))
        self.assertNotIn("GetSwordBigGoron_ElapsedTime", plan["start_values"])

    def test_with_malons_place_shuffled_the_vine_hatch_asks_for_the_weird_egg(self):
        # The area at the top of the vine climb hatches the Chicken for any Link while the castle quest is at Step03,
        # the step Malon's talk writes. With her place shuffled, it stands only for a Link whose save says he got the
        # egg.
        for gifts in (["Malon's Egg"], [group for group in GIFT_GROUPS if group != "Malon's Egg"]):
            _, plan = self.generate("21", {"Gifts": gifts})
            self.assertEqual(VINE_HATCH in plan["presences"], "Malon's Egg" in gifts, gifts)

    def test_a_route_with_the_weird_egg_elsewhere_still_meets_zelda(self):
        # Talon at the castle wakes for the Chicken, and the way to Zelda's courtyard climbs past him. The moat's two
        # places wait for Zelda met, so every route takes the Weird Egg's place and Malon's in a sphere before them.
        for seed in map(str, range(40, 50)):
            log, _ = self.generate(seed, CHESTS_ALONE | {"Gifts": ["Malon's Egg"]})
            sphere = {step: number for number, steps in enumerate(log["playthrough"].values()) for step in steps}
            egg = next(place for place, item in playthrough_steps(log).items() if item == "Weird Egg")
            for earlier in (egg, "HC Malon Egg"):
                for moat in ("HF Ocarina of Time Item", "Song from Ocarina of Time"):
                    self.assertLess(sphere[earlier], sphere[moat], (seed, earlier, moat))
            self.assertEqual(self.route_out_of_reach(log), [])

    def test_a_child_meets_zelda_without_what_the_lost_dog_holds(self):
        # The lost dog comes out only once Zelda's Letter is shown at Kakariko's gate. Without that rule, seed 3 puts
        # the Weird Egg there, and the child needs it to wake Talon on the way to Zelda.
        log, _ = self.generate("3", GIFTS_SHUFFLED)
        without_the_dog = {**log["locations"], "Market Lost Dog": "Green Rupee"}
        self.assertTrue(reach.reachable("Song from Impa", *without_the_dog.items(), settings=log["settings"]))

    def assert_gifts_give_what_is_placed(self, gifts, seeds):
        """Each place of gifts writes the item placed there at its one event, and each item, wherever it lands, goes by
        its one name. Returns the kinds of place each item landed at, every item having left its own place in at least one
        seed."""
        settings = GIFTS_SHUFFLED | REWARDS_SHUFFLED | SHOPS_SHUFFLED | SKULLTULAS_SHUFFLED | LOOSE_SHUFFLED
        chests = {row["location"] for row in SHUFFLED}
        shops = {row["location"] for row in SHOP_SLOTS}
        skulltulas = {row["location"] for row in SKULLTULAS}
        kinds, moved = collections.defaultdict(set), set()
        for seed in seeds:
            spoiler, plan = self.generate(seed, settings)
            sites = gift_sites(plan)
            for location in gifts:
                given = [item for at, item in sites if at == location]
                self.assertEqual(len(given), 1, (seed, location))
                self.assertIn((given[0], spoiler["locations"][location]), ITEM_PAIRS, (seed, location))
            placed = placed_remake_items(spoiler, plan)
            for location, item in gifts.items():
                at = [place for place, held in placed.items() if held == item]
                self.assertEqual(len(at), 1, (seed, item))
                kinds[item].add("chest" if at[0] in chests else "gift" if at[0] in GIFT_LOCATIONS.values()
                                else "reward place" if at[0] in REWARD_AREAS else "shop" if at[0] in shops
                                else "skulltula" if at[0] in skulltulas else "loose")
                if at[0] != location:
                    moved.add(item)
        self.assertEqual(moved, set(gifts.values()))
        return kinds

    def test_the_gerudo_pass_the_zora_tunic_and_the_light_arrows_are_gifts(self):
        # The pass goes only to a chest, a gift or a reward's place, as a stone does. A shop refuses a tunic to a child,
        # so none sells the Zora Tunic.
        kinds = self.assert_gifts_give_what_is_placed(STORY_GIFTS, map(str, range(640, 660)))
        self.assertLessEqual(kinds["Item_Token_Gerudo"], {"chest", "gift", "reward place"}, kinds)
        self.assertNotIn("shop", kinds["Obj_Armor_Zora"], kinds)

    def test_the_goron_gifts_give_what_is_placed(self):
        # The bracelet goes wherever the gauntlets go, a shop shelf included. A shop refuses a tunic to a child, so none
        # sells the Goron Tunic.
        kinds = self.assert_gifts_give_what_is_placed(GORON_GIFTS, map(str, range(680, 700)))
        self.assertIn("shop", kinds["Item_Gauntlet_Goron"], kinds)
        self.assertNotIn("shop", kinds["Obj_Armor_Goron"], kinds)

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
        # Seed 2 puts the Gerudo Pass in the heart piece's chest of Dampé's race, the Zora Tunic at Kakariko's
        # 50-token reward and the Light Arrows in the Bottom of the Well's map chest, and its route takes the
        # carpenters' rescue and Zelda's scene for what they open.
        log, _ = self.generate("2", CHESTS_ALONE | GIFTS_SHUFFLED)
        for location, item in STORY_GIFTS.items():
            self.assertNotIn((item, log["locations"][location]), ITEM_PAIRS, location)
        self.assertLessEqual({"GF Freed All Carpenters", "Market ToT Light Arrow Cutscene"},
                             playthrough_steps(log).keys())
        self.assertEqual(list(log["playthrough"].values())[-1], {"Ganon": "Triforce"})
        self.assertEqual(self.route_out_of_reach(log), [])

    def test_each_great_fairy_gives_her_placed_item_and_asks_her_own_bool_on_a_return(self):
        # Her paid path writes a saved Bool of the mod's own and her return question asks it, so she gives her placed
        # item once. With her group left out the plan names neither, and adds no Bool.
        spoiler, plan = self.generate("31", {"Gifts": ["Great Fairies"]})
        texts = {(entry["flow"], entry["event"], entry["param"]): entry["text"] for entry in plan["event_texts"]}
        given = dict(gift_sites(plan))
        self.assertEqual(plan["claims"], [row["claim"] for row in FOUNTAIN_CLAIMS])
        for row in FOUNTAIN_CLAIMS:
            self.assertIn((given[row["location"]], spoiler["locations"][row["location"]]), ITEM_PAIRS, row["location"])
            for event in (row["paid"], row["question"]):
                self.assertEqual(texts[row["flow"], event, "GameDataName"], row["claim"], (row["location"], event))
        _, plan = self.generate("31", {"Gifts": [group for group in GIFT_GROUPS if group != "Great Fairies"]})
        self.assertFalse({row["claim"] for row in FOUNTAIN_CLAIMS} & set(plan["claims"]))
        self.assertFalse({row["flow"] for row in FOUNTAIN_CLAIMS} & {entry["flow"] for entry in plan["event_texts"]})

    def test_a_great_fairy_may_hold_an_item_another_place_gives(self):
        # She asks her own Bool, not the item's record, so what she holds may come from another place as well.
        for seed in map(str, range(1460, 1480)):
            spoiler, plan = self.generate(seed, GIFTS_SHUFFLED)
            placed = placed_remake_items(spoiler, plan)
            names = collections.Counter(placed.values())
            texts = {(entry["flow"], entry["event"], entry["param"]): entry["text"] for entry in plan["event_texts"]}
            for row in FOUNTAIN_CLAIMS:
                if names[placed[row["location"]]] > 1:
                    self.assertEqual(texts[row["flow"], row["question"], "GameDataName"], row["claim"], seed)
                    return
        self.fail("no fountain holds an item another place gives")

    def test_hints_name_the_fairy_outside_ganons_castle_at_ganondorfs_castle(self):
        # Her mark stands in the scene the game names Ganondorf's Castle, where the stones place that scene's
        # skulltula, and not in the child's Hyrule Castle, which has a fairy of its own. Seed 55's route takes the Lens
        # of Truth from her, and seed 3's the Iron Boots, and a stone of each says Ganondorf's Castle is on the way.
        for seed in ("55", "3"):
            spoiler, _ = self.assert_hints_are_true(seed, {"Gifts": ["Great Fairies"]})
            self.assertIn("OGC Great Fairy Reward", playthrough_steps(spoiler))
            self.assertIn("They say that Ganondorf's Castle is on the way of the hero.", spoiler["hints"].values())

    def test_the_summit_passes_her_meter_scene_with_an_item_that_brings_none(self):
        # For a Link with no meter her entry plays the meter's birth and waits for its end. With an item that brings no
        # meter there is none to be born, so the two questions of that scene ask her claim, false until her paid path
        # writes it after the gift. Seed 1 puts a Purple Rupee at the summit, and seed 14 the Ice Arrows.
        summit = next(row for row in FOUNTAIN_CLAIMS if row["location"] == "DMT Great Fairy Din's Fire")
        scene = [("Event/EventFlow/Dm_SY_GetInEvent.bfevfl.zs", event, "GameDataName")
                 for event in ("Event555", "Event554")]
        for seed, brings in (("1", False), ("14", True)):
            spoiler, plan = self.generate(seed, CHESTS_ALONE | {"Gifts": ["Great Fairies"]})
            self.assertEqual(spoiler_item(spoiler["locations"][summit["location"]]) in METER_ITEMS, brings, seed)
            texts = {(e["flow"], e["event"], e["param"]): e["text"] for e in plan["event_texts"]}
            self.assertEqual([texts.get(site) for site in scene], [None] * 2 if brings else [summit["claim"]] * 2, seed)

    def test_dungeon_rewards_shuffled(self):
        # With the gifts at Vanilla the nine places join the chests' shuffle. Link's Pocket, where the patcher writes
        # nothing, holds no reward: the Light Medallion is one of the nine.
        spoiler, plan = self.generate("31", CHESTS_ALONE | REWARDS_SHUFFLED)
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

    def test_dungeon_rewards_shuffled_among_themselves(self):
        # As SoH's "End of Dungeons": the nine rewards go to the nine places that hand one over, and nowhere else.
        moved = 0
        for seed in map(str, range(860, 870)):
            spoiler, plan = self.generate(seed, REWARDS_AMONG_THEMSELVES)
            self.assertEqual(spoiler["settings"]["Dungeon rewards"], "Shuffled among themselves")
            sites = dict(reward_sites(plan))
            self.assertEqual(sorted(sites), sorted(REWARD_LOCATIONS.values()), seed)
            self.assertEqual(sorted(sites.values()), sorted(REWARDS), seed)
            self.assertEqual({location for location, item in placed_remake_items(spoiler, plan).items()
                              if item in REWARDS}, set(sites), seed)
            moved += sum(sites[row["location"]] != row["item"] for row in REWARD_SITES)
        self.assertTrue(moved)

    def test_a_dungeon_left_out_keeps_its_boss_reward(self):
        for rewards in (REWARDS_SHUFFLED, REWARDS_AMONG_THEMSELVES):
            settings = rewards | GIFTS_SHUFFLED | {"Shuffled dungeons": [name for name in DUNGEONS
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
            spoiler, plan = self.generate("21", CHESTS_ALONE | {"Gold Skulltulas": choice})
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
        log, _ = self.generate("23", CHESTS_ALONE | SKULLTULAS_SHUFFLED)
        skulltulas = {row["location"] for row in SKULLTULAS}
        self.assertEqual(len(skulltulas & playthrough_steps(log).keys()), 8)
        self.assertEqual(self.route_out_of_reach(log), [])

    def test_songs_shuffled_among_themselves(self):
        spoiler, plan = self.generate("21", CHESTS_ALONE | SONGS_SHUFFLED)
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
        self.assertEqual(plan["presences"], [KING_ZORA_ICE] + RIVER_WALLS + COLOSSUS_GATES)

    def test_a_route_that_learns_songs_at_other_places_is_in_reach_in_its_sphere(self):
        # Seed 22's route learns songs at places that teach others in the game.
        log, _ = self.generate("22", SONGS_SHUFFLED)
        places = {row["location"] for row in SONG_PLACES}
        self.assertTrue(any(location in places and SONGS[item]["location"] != location
                            for location, item in playthrough_steps(log).items()))
        self.assertEqual(self.route_out_of_reach(log), [])

    def test_the_moat_waits_for_zelda_and_the_stone_bosses_do_not(self):
        # With the bracelet, a scale and the Bomb Bag in Mido's chests and no Weird Egg, a child opens both stone
        # dungeons and never reaches the castle's courtyard.
        early = [("KF Mido Top Left Chest", "Goron's Bracelet"), ("KF Mido Top Right Chest", "Progressive Scale"),
                 ("KF Mido Bottom Left Chest", "Progressive Bomb Bag"), ("HC Malon Egg", "Green Rupee")]
        for boss in ("King Dodongo", "Barinade"):
            self.assertTrue(reach.reachable(boss, *early), boss)
        for place in ("HF Ocarina of Time Item", "Song from Ocarina of Time"):
            self.assertFalse(reach.reachable(place, *early), place)

    def test_with_songs_shuffled_the_courtyard_scene_starts_played(self):
        # With the songs shuffled, a child can learn a warp song and, grown up, warp out of the Temple of Time before he
        # walks through its door. The scene there plays only while AllSage stands at Step01, and the adult's world map
        # waits for it.
        _, shuffled = self.generate("21", CHESTS_ALONE | SONGS_SHUFFLED)
        _, vanilla = self.generate("21", CHESTS_ALONE)
        self.assertIs(shuffled["start_values"]["HavePlayedEvent.Dm_TimeTemple_0065"], True)
        self.assertNotIn("HavePlayedEvent.Dm_TimeTemple_0065", vanilla["start_values"])

    def test_loose_items_shuffled(self):
        spoiler, plan = self.generate("21", CHESTS_ALONE | LOOSE_SHUFFLED)
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
        for seed in range(300, 333):
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
        # Seed 5 hints at the Bottom of the Well's loose key, in a coffin, by its sentence.
        spoiler, plan = self.assert_hints_are_true("5", CHESTS_ALONE | LOOSE_SHUFFLED)
        loose = {row["location"] for row in LOOSE_ITEMS}
        said = {plain(CHEST_SENTENCES[location].replace("[[1]]", ITEM_NAMES[item]))
                for location, item in placed_remake_items(spoiler, plan).items() if location in loose}
        self.assertTrue(said & set(spoiler["hints"].values()))

    def test_the_letter_and_the_fairies_gifts_lie_only_in_chests(self):
        # King Zora was seen to take the letter a chest gave, and chests were seen to give Farore's Wind and the three
        # blessings. Whether a gift, a shop, a skulltula or a loose spot gives any of them is unseen.
        settings = LOOSE_SHUFFLED | GIFTS_SHUFFLED | SHOPS_SHUFFLED | SKULLTULAS_SHUFFLED | REWARDS_SHUFFLED
        chests = {row["location"] for row in SHUFFLED}
        kept = FAIRY_ITEMS | {"Item_Bottle_Letter"}
        for seed in range(830, 840):
            spoiler, plan = self.generate(str(seed), settings)
            held = sorted((item, location in chests) for location, item in placed_remake_items(spoiler, plan).items()
                          if item in kept)
            self.assertEqual(held, [(item, True) for item in sorted(kept)], seed)

    def test_a_route_reaches_the_colossus_only_with_a_way_out(self):
        # The Desert Colossus's way into the Wasteland stands only once an adult has walked into the Wasteland from the
        # fortress, a walk no route names, so Link leaves the Colossus by another warp song or comes on foot. Seed 1's
        # route takes a step there before the carpenters' rescue, and seed 9's, with gifts shuffled, after it.
        for seed, settings, before in (("1", SONGS_SHUFFLED, True), ("9", GIFTS_SHUFFLED | SONGS_SHUFFLED, False)):
            log, _ = self.generate(seed, settings)
            spheres = list(log["playthrough"].values())
            there = next(number for number, steps in enumerate(spheres)
                         if any(step.startswith(("Colossus", "Spirit Temple")) or step == "Sheik at Colossus"
                                for step in steps))
            rescue = next(number for number, steps in enumerate(spheres) if "GF Freed All Carpenters" in steps)
            self.assertEqual(there < rescue, before, seed)
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

    def test_a_place_called_foolish_holds_nothing_the_seed_needs(self):
        # Seed 27's route takes nothing in Kokiri Forest but the Deku Shield on the Kokiri Shop's kept shelf.
        spoiler, _ = self.assert_hints_are_true("27", GIFTS_SHUFFLED | SKULLTULAS_SHUFFLED | SONGS_SHUFFLED
                                                | LOOSE_SHUFFLED | REWARDS_SHUFFLED)
        self.assertEqual([step for step in playthrough_steps(spoiler) if step.startswith("KF ")], ["KF Shop Item 1"])

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
                    # No shuffled place of the area holds a major item, as SoH asks, and the seed can be finished
                    # with every place of the area emptied at once, the places the seed keeps as the game has them
                    # included: a dungeon's small keys, a kept shop shelf, or tokens a reward counts only together.
                    area = places[found[1]]
                    chests = [location for location in spoiler["locations"] if place_of(location) == area]
                    self.assertTrue(chests)
                    self.assertFalse(SOH_MAJOR_ITEMS
                                     & {spoiler_item(spoiler["locations"][location]) for location in chests})
                    emptied = dict(spoiler["locations"]) | {location: "Green Rupee" for location in
                                                            EVERY_PLACE | spoiler["locations"].keys()
                                                            if place_of(location) == area}
                    self.assertTrue(reach.reachable("Ganon", *emptied.items(), settings=spoiler["settings"]))
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
            spoiler, plan = self.generate(str(seed), CHESTS_ALONE)
            self.assert_character_hints_are_true(spoiler, plan)
            self.assertEqual(len(spoiler["hints from characters"]["KF Mido Hint"]), 3)
        # Seed 811 puts the sword in a chest of Mido's House.
        spoiler, _ = self.generate("811", CHESTS_ALONE)
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
        spoiler, plan = self.generate("31", CHESTS_ALONE | {"Start with": chosen[::-1]})
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

    def test_a_seed_that_starts_with_the_kokiri_sword_starts_the_forest_visited(self):
        visited = {"IsVisitLocation.KokiriForest": True, "HavePlayedEvent.Dm_Kokiri_0050": True}
        greeting_played = {"Skip": [name for name in SKIPS if name != "Saria's greeting"]}
        _, sword = self.generate("38", {"Start with": ["Kokiri Sword"]} | greeting_played)
        _, shield = self.generate("38", {"Start with": ["Deku Shield"]} | greeting_played)
        self.assertLessEqual(visited.items(), sword["start_values"].items())
        self.assertFalse(visited.keys() & shield["start_values"].keys())

    def test_a_seed_starts_with_each_item_that_comes_in_steps_at_its_highest_step(self):
        chosen = list(START_STEPS)
        spoiler, plan = self.generate("32", {"Start with": chosen[::-1]} | GIFTS_SHUFFLED)
        self.assertEqual(spoiler["settings"]["Start with"], chosen)
        self.assertEqual(plan["start_items"], [actor for name in chosen for actor in START_STEPS[name]])
        placed = placed_remake_items(spoiler, plan)
        self.assertFalse(set(STEP_COPIES) & set(placed.values()))
        unstarted = placed_remake_items(*self.generate("32", GIFTS_SHUFFLED))
        self.assertEqual(list(placed.values()).count("TBox_Rupee_Red"),
                         list(unstarted.values()).count("TBox_Rupee_Red") + len(STEP_COPIES))
        self.assertEqual(list(spoiler["playthrough"].values())[-1], {"Ganon": "Triforce"})
        self.assertEqual(self.route_out_of_reach(spoiler), [])

    def test_a_started_strength_step_leaves_darunias_bracelet_out(self):
        # Each step of strength includes the bracelet, so a seed that starts with any leaves Darunia's out of the pool,
        # as it leaves the chest copy of each gauntlet step it holds, each with a Red Rupee in its stead.
        unstarted = placed_remake_items(*self.generate("1", {"Gifts": ["Story gifts"]}))
        self.assertIn("Item_Gauntlet_Goron", unstarted.values())
        for held, step in enumerate(("Goron Bracelet", "Silver Gauntlets", "Golden Gauntlets"), 1):
            placed = placed_remake_items(*self.generate("1", {"Gifts": ["Story gifts"], "Start with": [step]}))
            self.assertNotIn("Item_Gauntlet_Goron", placed.values(), step)
            self.assertEqual(list(placed.values()).count("TBox_Rupee_Red"),
                             list(unstarted.values()).count("TBox_Rupee_Red") + held, step)

    def test_a_started_tunic_leaves_the_gifts(self):
        # With the gifts shuffled, King Zora's place adds a Zora Tunic to the pool and Darunia's son's a Goron Tunic. A
        # seed that starts with one leaves it out, with a Red Rupee in its stead.
        for tunic, copy in (("Zora Tunic", "Obj_Armor_Zora"), ("Goron Tunic", "Obj_Armor_Goron")):
            for seed in ("1", "2", "3"):
                placed = placed_remake_items(*self.generate(seed, GIFTS_SHUFFLED | {"Start with": [tunic]}))
                unstarted = placed_remake_items(*self.generate(seed, GIFTS_SHUFFLED))
                self.assertIn(copy, unstarted.values(), (tunic, seed))
                self.assertNotIn(copy, placed.values(), (tunic, seed))
                self.assertEqual(list(placed.values()).count("TBox_Rupee_Red"),
                                 list(unstarted.values()).count("TBox_Rupee_Red") + 1, (tunic, seed))

    def test_a_place_that_gives_a_started_step_again_counts_for_nothing(self):
        # Saria, the diving game, the two wallet rewards and the Bomb Bag's chest of a dungeon left out each give one
        # named step, which a seed that starts with it already holds. The logic would count the next step there.
        again = {"LW Gift From Saria", "ZD Diving Minigame", "Kak 10 Gold Skulltula Reward",
                 "Kak 30 Gold Skulltula Reward"}
        started = ["Hookshot", "Silver Scale", "Giant's Wallet", "Fairy Ocarina"]
        # With the Giant's Wallet started, both rewards would make four wallets, more than the logic's count holds.
        cases = [(seed, {"Start with": started}, again) for seed in ("9", "33", "34", "35", "36")]
        cases.append(("37", {"Start with": [*started, "Bomb Bag (20 bombs)"],
                             "Shuffled dungeons": [name for name in DUNGEONS if name != "Dodongo's Cavern"]},
                      again | {"Dodongos Cavern Bomb Bag Chest"}))
        for seed, settings, given_again in cases:
            spoiler, _ = self.generate(seed, CHESTS_ALONE | settings)
            self.assertFalse(given_again & set(playthrough_steps(spoiler)), seed)
            self.assertEqual(list(spoiler["playthrough"].values())[-1], {"Ganon": "Triforce"}, seed)
            self.assertEqual(self.route_out_of_reach(spoiler), [], seed)

    def test_characters_give_no_hints_at_vanilla(self):
        # Seed 811 puts the Kokiri Sword in a chest of Mido's House, and with Mido silent a stone names it there.
        spoiler, plan = self.generate("811", CHESTS_ALONE | NO_CHARACTER_HINTS)
        given, given_plan = self.generate("811", CHESTS_ALONE)
        prize_lines = [(row["file"], row["label"]) for row in PRIZE_LINES if row["location"] in spoiler["locations"]]
        self.assertTrue(prize_lines)
        self.assertEqual(spoiler["settings"]["Hints from characters"], "Vanilla")
        self.assertEqual(spoiler["hints from characters"], {})
        self.assertEqual([(entry["file"], entry["label"]) for entry in plan["message_texts"]], prize_lines)
        self.assertEqual(plan["message_texts"], given_plan["message_texts"][:len(prize_lines)])
        sent_on = [dict(zip(("flow", "event", "next"), row["sent_on"].split(":"))) for row in CHARACTER_HINTS
                   if row["sent_on"]]
        race = len(DAMPES_RACE_NEXTS)
        self.assertEqual(plan["event_nexts"][:race], DAMPES_RACE_NEXTS)
        self.assertEqual(given_plan["event_nexts"], DAMPES_RACE_NEXTS + sent_on + plan["event_nexts"][race:])
        told = ("stone_texts", "message_texts", "event_nexts")
        self.assertEqual({key: value for key, value in plan.items() if key not in told},
                         {key: value for key, value in given_plan.items() if key not in told})
        self.assertEqual((spoiler["locations"], spoiler["playthrough"]), (given["locations"], given["playthrough"]))
        self.assertIn("They say that the Kokiri Sword can be found at Kokiri Forest.", spoiler["hints"].values())
        self.assertNotIn("They say that the Kokiri Sword can be found at Kokiri Forest.", given["hints"].values())

    def test_a_place_whose_kept_shelf_the_route_needs_is_not_foolish(self):
        # Seed 70's route buys the Deku Shield on the Kokiri Shop's kept shelf in its first sphere, and takes nothing
        # else in Kokiri Forest.
        spoiler, _ = self.assert_hints_are_true("70", SEVEN_PER_SHOP | {"Kokiri Forest": "Closed Deku"})
        self.assertEqual(list(spoiler["playthrough"].values())[0].get("KF Shop Item 2"), "Buy Deku Shield")
        self.assertEqual([step for step in playthrough_steps(spoiler) if step.startswith("KF ")], ["KF Shop Item 2"])

    def test_hints_name_shop_goods_truly(self):
        # Seed 4 hints at a shuffled shop slot by its sentence.
        self.assert_hints_are_true("4", SHOPS_SHUFFLED)

    def test_hints_name_the_goods_only_five_per_shop_shuffle(self):
        # Seed HINT_SEED_AT_FIVE hints at an item 3 by its slot's sentence.
        seed, settings = HINT_SEED_AT_FIVE, CHESTS_ALONE | {"Shop goods": "Five per shop shuffled"}
        self.assert_hints_are_true(seed, settings)
        spoiler, plan = self.generate(seed, settings)
        held = placed_remake_items(spoiler, plan)
        sentences = {plain(CHEST_SENTENCES[row["location"]].replace("[[1]]", ITEM_NAMES[held[row["location"]]]))
                     for row in SHOP_SLOTS if row["number"] == "3"}
        self.assertTrue(sentences & set(spoiler["hints"].values()))

    def test_hints_name_the_goods_only_six_and_seven_per_shop_shuffle(self):
        # A stone of seed HINT_SEED_AT_SEVEN says that a Goron shopkeeper sells a Large Magic Jar, at the Goron Shop's
        # item 4.
        self.assert_hints_are_true(HINT_SEED_AT_SEVEN, CHESTS_ALONE | SEVEN_PER_SHOP)
        spoiler, plan = self.generate(HINT_SEED_AT_SEVEN, CHESTS_ALONE | SEVEN_PER_SHOP)
        held = placed_remake_items(spoiler, plan)
        new = {row["location"] for row in shuffled_shop_slots(7)} - {row["location"] for row in shuffled_shop_slots(5)}
        sentences = {plain(CHEST_SENTENCES[location].replace("[[1]]", ITEM_NAMES[held[location]])) for location in new}
        self.assertTrue(sentences & set(spoiler["hints"].values()))
        self.assert_stone_texts_fit_their_boxes(plan)

    def test_the_chest_game_owner_names_the_prize(self):
        row = next(row for row in PRIZE_LINES if row["location"] == CHEST_GAME_PRIZE)
        named = set()
        for seed in map(str, range(700, 710)):
            spoiler, plan = self.generate(seed)
            item = placed_remake_items(spoiler, plan)[CHEST_GAME_PRIZE]
            [entry] = [entry for entry in plan["message_texts"] if (entry["file"], entry["label"])
                       == (row["file"], row["label"])]
            self.assertEqual(plain(entry["text"]), plain(row["sentence"].replace("[[1]]", ITEM_NAMES[item])), seed)
            named.add(item)
        self.assertGreater(len(named), 2, named)
        # Without the Lens the prize stays the game's own, a Piece of Heart, which the game's own line names.
        spoiler, plan = self.generate("710", LENS_NEVER_NEEDED)
        self.assertNotIn(CHEST_GAME_PRIZE, placed_remake_items(spoiler, plan))
        self.assertNotIn((row["file"], row["label"]),
                         [(entry["file"], entry["label"]) for entry in plan["message_texts"]])

    def test_every_message_fits_its_box(self):
        sites = {(row["file"], row["label"]): row for row in PRIZE_LINES + CHARACTER_HINTS}
        for seed, settings in [(str(seed), None) for seed in range(720, 730)] + [("730", SHOPS_SHUFFLED),
                                                                                  ("731", GIFTS_SHUFFLED)]:
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
        # Seed 407's route takes the Bomb Bag from the Hyrule Field tree skulltula that SoH calls LLR GS Back Wall, and
        # a hint says Hyrule Field is on the way of the hero. Seed 402's route takes the Fairy Bow from HC OGC GS, in
        # the scene the game calls Ganondorf's Castle, and a hint says the Fairy Bow is at Ganondorf's Castle.
        for seed, skulltula, hint in (
                ("407", "LLR GS Back Wall", "They say that Hyrule Field is on the way of the hero."),
                ("402", "HC OGC GS", "They say that the Fairy Bow can be found at Ganondorf's Castle.")):
            spoiler, _ = self.assert_hints_are_true(seed, {"Gold Skulltulas": "In the overworld"})
            self.assertIn(skulltula, playthrough_steps(spoiler))
            self.assertIn(hint, spoiler["hints"].values())

    def test_hints_name_gold_skulltulas_truly(self):
        self.assert_hints_are_true("23", CHESTS_ALONE | SKULLTULAS_SHUFFLED)
        spoiler, plan = self.generate("23", CHESTS_ALONE | SKULLTULAS_SHUFFLED)
        skulltulas = {row["location"] for row in SKULLTULAS}
        said = {plain(CHEST_SENTENCES[location].replace("[[1]]", ITEM_NAMES[item]))
                for location, item in placed_remake_items(spoiler, plan).items() if location in skulltulas}
        self.assertTrue(said & set(spoiler["hints"].values()))

    def test_hints_name_song_places_truly(self):
        # Seed 24 hints at three song places by their sentences.
        spoiler, plan = self.generate("24", SONGS_SHUFFLED)
        held = placed_remake_items(spoiler, plan)
        sentences = {plain(CHEST_SENTENCES[row["location"]].replace("[[1]]", ITEM_NAMES[held[row["location"]]]))
                     for row in SONG_PLACES}
        self.assertTrue(sentences & set(spoiler["hints"].values()))
        self.assert_hints_are_true("24", SONGS_SHUFFLED)

    def test_every_stone_has_a_hint_that_fits_its_boxes(self):
        for seed in (FIXED_SEED, *map(str, range(620, 640))):
            _, plan = self.generate(seed)
            self.assert_stone_texts_fit_their_boxes(plan)

    def assert_stone_texts_fit_their_boxes(self, plan):
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
        spoiler, plan = self.run_seedgen(FIXED_SEED, CHESTS_ALONE)
        self.assertEqual(without_version(spoiler),
                         without_version((FIXTURES / f"spoiler-{FIXED_SEED}.json").read_bytes()))
        self.assertEqual(plan, (FIXTURES / f"plan-{FIXED_SEED}.json").read_bytes())


if __name__ == "__main__":
    SEEDGEN, reach.PROGRAM, VERSION = sys.argv[1:4]
    unittest.main(argv=[sys.argv[0], *sys.argv[4:]])
