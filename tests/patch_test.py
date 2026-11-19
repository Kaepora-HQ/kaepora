"""End-to-end tests of kaepora-patch on the game's own placement files, event flows, actor packs and game data list.

Usage: python3 -I tests/patch_test.py tested-mod <kaepora-patch> <game.nup> <tested mod's placement file>
       python3 -I tests/patch_test.py tested-start-mod <kaepora-patch> <game.nup> <tested mod's game data list>
       python3 -I tests/patch_test.py tested-hammer-room-mod <kaepora-patch> <game.nup> <tested mod's placement file>
       python3 -I tests/patch_test.py tested-substitute-mod|tested-added-item-mod|tested-gift-mod \
           |tested-stone-text-mod <kaepora-patch> <game.nup> <tested mod's romfs folder>
       python3 -I tests/patch_test.py tested-event-text-mods <kaepora-patch> <game.nup> <a plan kaepora-seedgen wrote> \
           <tested mod's romfs folder>...
       python3 -I tests/patch_test.py tested-program-mod|tested-bomb-mod <kaepora-patch> <game.nup> \
           <tested mod's folder>
       python3 -I tests/patch_test.py tested-new-actor-mod <kaepora-patch> <game.nup> <tested mod's romfs folder>
       python3 -I tests/patch_test.py seed-plan <kaepora-patch> <game.nup> <a plan kaepora-seedgen wrote>
       python3 -I tests/patch_test.py larger-plan|bad-input|size-table|size-table-unchanged|together|gift| \
           other-version|start-values|shop|shop-table|new-actors <kaepora-patch> <game.nup>
       python3 -I tests/patch_test.py gift-size-table|stone-texts <kaepora-patch> <game.nup>

The game's data is read from the container at test time. These readers are written apart from the patcher's, so a
reader bug in either shows as a difference. Expected message texts are built from the control codes' bytes, apart
from the patcher's tags.
"""

import csv
import itertools
import json
import pathlib
import re
import struct
import subprocess
import sys
import tempfile
import zlib
from compression import zstd

DEKU_STATIC = "Banc/InsideTheDekuTree/InsideTheDekuTree_Static.bcett.byml.zs"
DODONGO_DYNAMIC = "Banc/DodongoCavern/DodongoCavern_Dynamic.bcett.byml.zs"
FOREST_DYNAMIC = "Banc/ForestTemple/ForestTemple_Dynamic.bcett.byml.zs"
TRAINING_GROUND_DYNAMIC = "Banc/GerudoTrainingGround/GerudoTrainingGround_Dynamic.bcett.byml.zs"
TRAINING_GROUND_STATIC = "Banc/GerudoTrainingGround/GerudoTrainingGround_Static.bcett.byml.zs"
WATER_STATIC = "Banc/WaterTemple/WaterTemple_Static.bcett.byml.zs"
SIZE_TABLE = "System/Resource/ResourceSizeTable.Product.111.Nin_Ounce_NVN2.rsizetable.zs"
MIDO_SCHEDULE = "Component/AIScheduleParam/Npc_Kokiri_Mido_Child.game__component__AIScheduleParam.bgyml"
BOMB_LIFT_PACKS = [f"Pack/Actor/{actor}.pack.zs" for actor in (
    "Item_Bomb", "Npc_Hylia_ImperialSoldier_Bomb", "Obj_BombFlower_A_01", "Obj_BombFlower_A_04",
    "Obj_BombFlower_Fruit_For_Break_Obj_DeathMtTrail_BrokenRock_A_01")]
GAME_DATA_LIST = "GameData/GameDataList.Product.110.byml.zs"
SLINGSHOT_CHEST = 821372075822700622
KOKIRI_SWORD_CHEST = 16757992849254467386
LONGSHOT_CHEST = 18059711820131847165
# Dodongo's Cavern chests that give their Drop__DropActor_Substitute to a Link who owns their Drop__CheckActor.
BRIDGE_CHEST = 7524527082521156886
PLATFORM_CHEST = 11582649900854566065
# Chests whose actor has no Dynamic.Drop__DropActor, in the Forest Temple and the Training Ground lobby.
FOREST_CHEST_WITHOUT_DROP = 15117164073907033527
LOBBY_CHEST_WITHOUT_DROP = 11182647876438515984
# The Training Ground hammer room chest names no Drop__DropActor, only a Drop__DropActor_Substitute for a Link who
# owns its Drop__CheckActor.
HAMMER_ROOM_CHEST = 4961588221597286817
# Gifts as the flow, the event whose call gives the item, and the call's parameter that names it. The Market potion
# shop child's bottle fits the Boomerang's name in its flow's size table entry. The Great Fairy's Din's Fire flow is a
# multiple of 32 bytes, so a longer name outgrows its entry.
SHOP_CHILD_FLOW = "Event/EventFlow/Npc_Hylia_020_Shop_Child.bfevfl.zs"
BOTTLE_GIFT = (SHOP_CHILD_FLOW, "Event171", "ActorName")
DINS_FIRE_GIFT = ("Event/EventFlow/Dm_FairyFountain_Din.bfevfl.zs", "Event212", "ActorName")
# Its item's name, Item_NayruLove, ends on an 8-byte boundary with its length and zero byte.
NAYRUS_LOVE_GIFT = ("Event/EventFlow/Dm_FairyFountain_Nayru.bfevfl.zs", "Event163", "ActorName")
# Two gifts in one flow.
ANJU_FLOW = "Event/EventFlow/Npc_Hylia_049_Adult.bfevfl.zs"
COJIRO_GIFT = (ANJU_FLOW, "Event234", "ReveiveItem")
POCKET_EGG_GIFT = (ANJU_FLOW, "Event264", "ReveiveItem")
BASE_PACKAGE = 0
UPDATE_PACKAGE = 1
NSO_HEADER_SIZE = 0x100
OTHER_VERSION = "This game file is another version of the game. Kaepora works only with version 2.0.0."
STONE_MESSAGES = "EventFlowMsg/GossipStone.msbt"
ENGLISH_PACKS = ["Mals/EUen.Product.110.sarc.zs", "Mals/USen.Product.110.sarc.zs"]
KOKIRI_STONE = "GOSSIP_KOKIRI_FOREST_01"
# The stone whose English rumour is the shortest, so a long text grows the English pack past its size table entry.
SHORT_RUMOUR_STONE = "GOSSIP_SACRED_FOREST_MEADOW_03"
# A label no stone of the game has.
NO_STONE = "GOSSIP_NOWHERE"
STONE_FLOW = "Event/EventFlow/GossipStone.bfevfl.zs"
TEXT_PACK_OVERHEAD = 312
BOOL = 0xD0
INT = 0xD1
INT64 = 0xD4
UINT64 = 0xD5

# Saved values by name, and the MurmurHash3 x86_32 hash of each name that keys it in the game data list.
FIRE_LOCK = "IsChainLockDoorOpen.4189300454150217955"
WATER_LOCK = "IsChainLockDoorOpen.2202807660383340401"
MIDO_CHEST = "IsTreasureBoxOpen.12786193094758345086"
WATER_KEYS = "DungeonInfo.WaterTemple.SmallKeyTotalNum"
IS_CHILD = "Sequence_IsChildhood"
FOREST_EXIT_SHUT = "KokiriForest_Npc_Kokiri_007_Child_Blocker_00"
TRIALS_LEFT = "InsideGanonCastle_Dungeon_Barrier_Count"
HASHES = {FIRE_LOCK: 749909151, WATER_LOCK: 400117133, WATER_KEYS: 3683958665, IS_CHILD: 3466731731,
          FOREST_EXIT_SHUT: 3856422516, TRIALS_LEFT: 2698978896}

GORON_SHOPKEEPERS = [("Npc_Goron_011_Shop_Child", "Npc_Goron_011_Shop_Child"),
                     ("Npc_Goron_011_Shop_Adult", "Npc_Goron_011_Shop_Child")]
KOKIRI_SHOPKEEPERS = [("Npc_Kokiri_001_Shop_Child_ShortStock", "Npc_Kokiri_001_Shop_Child_ShortStock"),
                      ("Npc_Kokiri_001_Shop_Child", "Npc_Kokiri_001_Shop_Child"),
                      ("Npc_Kokiri_001_Shop_Adult", "Npc_Kokiri_001_Shop_Child")]
GORON_ADULT_STATIC = "Banc/GoronCity/GoronCity_Adult_Static.bcett.byml.zs"
GORON_CHILD_STATIC = "Banc/GoronCity/GoronCity_Child_Static.bcett.byml.zs"
KOKIRI_ADULT_STATIC = "Banc/KokiriForest/KokiriForest_Adult_Static.bcett.byml.zs"
KOKIRI_CHILD_STATIC = "Banc/KokiriForest/KokiriForest_Child_Static.bcett.byml.zs"
GORON_TUNIC_SLOT = (GORON_SHOPKEEPERS, 3, [(GORON_ADULT_STATIC, 15075156905719609791),
                                           (GORON_CHILD_STATIC, 9081723785715040392)])
GORON_HEART_SLOT = (GORON_SHOPKEEPERS, 0, [(GORON_ADULT_STATIC, 17336515988275376869),
                                           (GORON_CHILD_STATIC, 3512162152062029293)])
STORMS_GROTTO_CHEST = 14818888396401495482
# All three Kokiri shopkeepers gate this slot on meeting the Deku Tree.
KOKIRI_STICKS_SLOT = (KOKIRI_SHOPKEEPERS, 5, [(KOKIRI_ADULT_STATIC, 4359156404841200094),
                                              (KOKIRI_CHILD_STATIC, 15329846556357339265),
                                              (KOKIRI_CHILD_STATIC, 1748192465494650957)])
# Each shopkeeper's count of a slot sold, Shop_SoldNum.<shopkeeper>.<slot>, by the hash of its name.
SOLD_NUM_HASHES = {("Npc_Goron_011_Shop_Child", 3): 3884178440, ("Npc_Goron_011_Shop_Adult", 3): 3983252854,
                   ("Npc_Goron_011_Shop_Child", 0): 1081991991, ("Npc_Goron_011_Shop_Adult", 0): 2258296172,
                   ("Npc_Kokiri_001_Shop_Child_ShortStock", 5): 2755930899,
                   ("Npc_Kokiri_001_Shop_Child", 5): 3688367492, ("Npc_Kokiri_001_Shop_Adult", 5): 138333967}
# The game's size table entry for an actor pack, over its decompressed size rounded up to 32 bytes.
PACK_OVERHEAD = 312
ACTOR_INFO = "RSDB/ActorInfo.Product.111.rstbl.byml.zs"
GAME_ACTOR_INFO = "RSDB/GameActorInfo.Product.111.rstbl.byml.zs"
POUCH_ACTOR_INFO = "RSDB/PouchActorInfo.Product.111.rstbl.byml.zs"
# The game's ActorInfo entry leaves this much over the file's size rounded up to 32 bytes.
ACTOR_INFO_OVERHEAD = 3064


class Game:
    """The update package's RomFS, which holds every placement file, the game data list, the dictionary pack and the
    size table."""

    def __init__(self, nup):
        self.nup = nup
        with open(nup, "rb") as f:
            toc_offset, toc_size = struct.unpack_from("<QQ", f.read(0x18), 8)
            f.seek(toc_offset)
            toc = f.read(toc_size)
        self.toc_end = toc_offset + toc_size
        _, package_count, dir_count, file_count, pool = struct.unpack_from("<IIQQQ", toc)
        dirs = 0x30 + 0x40 * package_count
        files = dirs + 0x18 * dir_count
        children = files + 0x50 * file_count

        def name(record):
            offset, length = struct.unpack_from("<II", toc, record)
            return toc[pool + offset:pool + offset + length].decode()

        def walk(d, prefix, into):
            first, count = struct.unpack_from("<II", toc, dirs + 0x18 * d + 8)
            for child in struct.unpack_from(f"<{count}I", toc, children + 4 * first):
                if child < dir_count:
                    walk(child, prefix + name(dirs + 0x18 * child) + "/", into)
                else:
                    record = files + 0x50 * (child - dir_count)
                    into[prefix + name(record)] = struct.unpack_from("<QQ", toc, record + 8)

        packages = [0x30 + 0x40 * i for i in range(package_count)]
        self.files = {}
        update = next(p for p in packages if toc[p + 0x18] == UPDATE_PACKAGE)
        walk(struct.unpack_from("<I", toc, update + 0x10)[0], "", self.files)
        self.program_offsets = {}
        for package in packages:
            exefs = {}
            walk(struct.unpack_from("<I", toc, package + 0xC)[0], "", exefs)
            self.program_offsets[toc[package + 0x18]] = exefs["main"][1]
        dictionaries = sarc(zstd.decompress(self.read("Pack/ZsDic.pack.zs")))
        self.dictionary = zstd.ZstdDict(dictionaries["bcett.byml.zsdic"])
        self.zs_dictionary = zstd.ZstdDict(dictionaries["zs.zsdic"])
        self.pack_dictionary = zstd.ZstdDict(dictionaries["pack.zsdic"])

    def read(self, path):
        size, offset = self.files[path]
        with open(self.nup, "rb") as f:
            f.seek(offset)
            return f.read(size)

    def placement(self, data):
        return byml(zstd.decompress(data, zstd_dict=self.dictionary))

    def game_data_list(self, data):
        return byml(zstd.decompress(data, zstd_dict=self.zs_dictionary))

    def event_flow(self, data):
        return event_flow(zstd.decompress(data, zstd_dict=self.zs_dictionary))

    def text_pack(self, data):
        return sarc(zstd.decompress(data, zstd_dict=self.zs_dictionary))

    def pack(self, data):
        return sarc(zstd.decompress(data, zstd_dict=self.pack_dictionary))


def sarc(data):
    """The files of a SARC archive by name."""
    header_size = struct.unpack_from("<H", data, 4)[0]
    data_offset = struct.unpack_from("<I", data, 0xC)[0]
    count = struct.unpack_from("<H", data, header_size + 6)[0]
    nodes = header_size + 12
    names = nodes + 16 * count + 8
    files = {}
    for i in range(count):
        _, attributes, start, end = struct.unpack_from("<IIII", data, nodes + 16 * i)
        name = data[names + 4 * (attributes & 0xFFFFFF):].split(b"\0", 1)[0].decode()
        files[name] = data[data_offset + start:data_offset + end]
    return files


def msbt(data):
    """An MSBT message file's label section as stored, and each message's text by its label, in message order."""
    assert data[:8] == b"MsgStdBn" and len(data) == struct.unpack_from("<I", data, 0x12)[0], data[:8]
    sections, at = {}, 0x20
    while at < len(data):
        magic, size = struct.unpack_from("<4sI", data, at)
        sections[magic] = data[at + 16:at + 16 + size]
        at += 16 + (size + 15) // 16 * 16
    labels, texts = sections[b"LBL1"], sections[b"TXT2"]
    count = struct.unpack_from("<I", texts)[0]
    starts = list(struct.unpack_from(f"<{count}I", texts, 4)) + [len(texts)]
    names = {}
    for slot in range(struct.unpack_from("<I", labels)[0]):
        label_count, at = struct.unpack_from("<II", labels, 4 + 8 * slot)
        for _ in range(label_count):
            size = labels[at]
            names[struct.unpack_from("<I", labels, at + 1 + size)[0]] = labels[at + 1:at + 1 + size].decode()
            at += 5 + size
    assert all(texts[end - 1] == 0 for end in starts[1:])
    return labels, {names[i]: texts[starts[i]:starts[i + 1] - 1] for i in range(count)}


def control_code(group, kind, params=b""):
    return b"\x0e" + struct.pack("<HHH", group, kind, len(params)) + params


def color(index):
    return control_code(0, 3, struct.pack("<H", index))


SIZE_80 = control_code(0, 2, struct.pack("<H", 80))
PAGE = control_code(0, 4)
RUMOUR_OPENING = color(1) + SIZE_80 + control_code(3, 0, bytes.fromhex("0f0000"))
RUMOUR_PAGE = PAGE + SIZE_80 + color(1)
TESTED_STONE_TEXT = ("They say this rumor was written by {color:0}Kaepora{color:65535}.\n"
                     "If you can read this, the mod loaded.")
TESTED_STONE_MESSAGE = (RUMOUR_OPENING + b"They say this rumor was written by " + color(0) + b"Kaepora" + color(65535)
                        + b".\nIf you can read this, the mod loaded.")
# The game's own rumours use it, and the game draws it.
LONG_DASH = "\u2014"
LONG_STONE_TEXT = ("They say that {color:0}Saria's Song{color:65535} is a friend's tune,\n"
                   "heard best in the Sacred Forest Meadow.{page}"
                   "They also say a stone speaks to anyone\nwho talks to it" + LONG_DASH + "with or without a mask.")
LONG_STONE_MESSAGE = (RUMOUR_OPENING + b"They say that " + color(0) + b"Saria's Song" + color(65535)
                      + b" is a friend's tune,\nheard best in the Sacred Forest Meadow." + RUMOUR_PAGE
                      + b"They also say a stone speaks to anyone\nwho talks to it" + LONG_DASH.encode()
                      + b"with or without a mask.")


def byml(data):
    """Parses BYML v7 in either byte order. Strings, arrays and dicts become Python values; other values become
    (type, value)."""
    order = {b"YB": "little", b"BY": "big"}[data[:2]]
    assert int.from_bytes(data[2:4], order) == 7, data[:4]

    def u24(offset):
        return int.from_bytes(data[offset:offset + 3], order)

    def u32(offset):
        return int.from_bytes(data[offset:offset + 4], order)

    def table(offset):
        starts = [offset + u32(offset + 4 + 4 * i) for i in range(u24(offset + 1))]
        return [data[start:data.index(b"\0", start)].decode() for start in starts]

    keys, strings = table(u32(4)), table(u32(8))

    def value(kind, raw):
        if kind == 0xA0:
            return strings[raw]
        if kind in (BOOL, 0xD1, 0xD2, 0xD3):
            return (kind, raw)
        if kind in (INT64, UINT64):
            return (kind, int.from_bytes(data[raw:raw + 8], order))
        count = u24(raw + 1)
        if kind == 0xC0:
            values = raw + 4 + (count + 3) // 4 * 4
            return [value(data[raw + 4 + i], u32(values + 4 * i)) for i in range(count)]
        if kind == 0xC1:
            entries = [(u24(raw + 4 + 8 * i), data[raw + 7 + 8 * i], u32(raw + 8 + 8 * i)) for i in range(count)]
            return {keys[key]: value(entry_kind, v) for key, entry_kind, v in entries}
        raise ValueError(f"node type {kind:#x}")

    return value(data[u32(12)], u32(12))


def event_flow(data):
    """Parses a BFEVFL version 4 flowchart: the file's name, the flowchart's actors, events and entry points, and the
    string pool, with each pointer followed and each DIC with its tree. Asserts that the relocation table lists every
    pointer field that holds an offset and no field that was not read, that the header's offsets and sizes agree with
    the file, and that every byte not read is zero."""
    seen = bytearray(len(data))
    fields = set()

    def take(offset, size):
        assert offset + size <= len(data), offset
        seen[offset:offset + size] = b"\1" * size
        return data[offset:offset + size]

    def u16(offset):
        return int.from_bytes(take(offset, 2), "little")

    def u32(offset):
        return int.from_bytes(take(offset, 4), "little")

    def pointer(offset):
        fields.add(offset)
        return int.from_bytes(take(offset, 8), "little")

    def text(offset):
        size = u16(offset)
        assert take(offset + 2 + size, 1) == b"\0", offset
        return take(offset + 2, size).decode()

    def name(offset):
        return text(pointer(offset))

    def dic(offset):
        """The root and each name, with its bit and branches."""
        assert take(offset, 4) == b"DIC ", offset
        return [[take(node, 8), name(node + 8)] for node in range(offset + 8, offset + 24 + 16 * u32(offset + 4), 16)]

    def value(offset):
        kind, count, at = take(offset, 1)[0], u16(offset + 2), offset + 16
        assert pointer(offset + 8) == 0, offset
        if kind in (0, 5):  # an argument or a string, its text right after the pointer to it
            assert pointer(at) == at + 8, offset
            return [kind, text(at + 8)]
        if kind in (2, 3, 4, 9):  # int, bool, float, float array
            return [kind, take(at, 4 * count)]
        if kind == 12:  # an actor's name and sub-name
            return [kind, text(pointer(at)), text(pointer(at + 8))]
        raise ValueError(f"value type {kind} at {offset:#x}")

    def container(offset):
        if offset == 0:
            return None
        assert take(offset, 1) == b"\1", offset
        return [dic(pointer(offset + 8)), [value(pointer(offset + 16 + 8 * i)) for i in range(u16(offset + 2))]]

    def actor(offset):
        actions, queries = pointer(offset + 0x18), pointer(offset + 0x20)
        return [name(offset), name(offset + 8), name(offset + 16), container(pointer(offset + 0x28)),
                take(offset + 0x30, 8), [name(actions + 8 * i) for i in range(u16(offset + 0x30))],
                [name(queries + 8 * i) for i in range(u16(offset + 0x32))]]

    def event(offset):
        """The name, the type and its 16-bit fields, then what each type points to."""
        kind = take(offset + 8, 1)[0]
        out = [name(offset), take(offset + 8, 8)]
        if kind in (0, 1, 4, 5):  # action, switch, sub-flow, sub-flow switch: parameters
            out.append(container(pointer(offset + 0x10)))
        if kind in (1, 5):  # switch, sub-flow switch: each case's value and event
            cases = pointer(offset + 0x18)
            out.append(take(cases, 8 * u16(offset + 0xA)) if cases else None)
        if kind == 2:  # fork: the forked events
            out.append(take(pointer(offset + 0x10), 2 * u16(offset + 0xA)))
        if kind in (4, 5):  # sub-flow, sub-flow switch: the flowchart and entry point called
            at = offset + (0x18 if kind == 4 else 0x20)
            out += [name(at), name(at + 8)]
        return out

    def entry_point(offset):
        subflows, names, variables = pointer(offset), pointer(offset + 8), pointer(offset + 0x10)
        variable_count = u16(offset + 0x1A)
        return [take(offset + 0x18, 6), take(subflows, 2 * u16(offset + 0x18)),
                dic(names) if variable_count else None, take(variables, 16 * variable_count)]

    assert take(0, 0x10) == b"BFEVFL\0\0\x00\x04\0\0\xff\xfe\x03\0", data[:0x10]
    assert u16(0x20) == 1 and u16(0x22) == 0, "a flowchart and no timeline"
    chart = pointer(pointer(0x28))
    assert u16(0x16) == chart and take(chart, 4) == b"EVFL", chart
    actors, events, entry_points = pointer(chart + 0x28), pointer(chart + 0x30), pointer(chart + 0x40)
    flow = {
        "name": text(u32(0x10) - 2),
        "flowcharts": dic(pointer(0x30)),
        "timelines": [pointer(0x38), dic(pointer(0x40))],
        "flowchart": name(chart + 0x20),
        "counts": take(chart + 0x10, 10),  # actors, actions, queries, events, entry points
        "actors": [actor(actors + 0x38 * i) for i in range(u16(chart + 0x10))],
        "events": [event(events + 0x30 * i) for i in range(u16(chart + 0x16))],
        "entry points": [dic(pointer(chart + 0x38)), [entry_point(entry_points + 0x20 * i)
                                                      for i in range(u16(chart + 0x18))]],
        "strings": [],
    }
    pool = chart + u32(chart + 4)
    assert take(pool, 4) == b"STR ", pool
    end = pool + 0x14
    for _ in range(u32(pool + 0x10) + 1):
        size = u16(end)
        flow["strings"].append(text(end))
        end += (size + 4) & ~1

    relocation = u32(0x18)
    assert u32(0x1C) == len(data) and take(relocation, 4) == b"RELT" and u32(relocation + 4) == relocation
    assert u32(relocation + 8) == 1 and u32(relocation + 0x1C) == end, "one section, up to the string pool's end"
    assert relocation + 0x28 + 8 * u32(relocation + 0x24) == len(data)
    listed = set()
    for entry in range(relocation + 0x28, len(data), 8):
        base, mask = u32(entry), u32(entry + 4)
        listed |= {base + 8 * k for k in range(32) if mask >> k & 1}
    holding = {field for field in fields if int.from_bytes(data[field:field + 8], "little")}
    assert holding <= listed <= fields, sorted(listed ^ fields)[:5]
    assert not any(byte for byte, read in zip(data, seen) if not read), "a byte not read is not zero"
    return flow


def string_parameter(flow, event, param):
    """The named event's parameter in a parsed flow, as [type, text], to read or to change."""
    _, _, (names, values), *_ = next(e for e in flow["events"] if e[0] == event)
    parameter = values[[name for _, name in names[1:]].index(param)]
    assert parameter[0] == 5, parameter
    return parameter


def give(flow, gift, old, new):
    """Changes the item that the gift's call names in a parsed flow from old to new."""
    _, event, param = gift
    parameter = string_parameter(flow, event, param)
    assert parameter[1] == old, parameter
    parameter[1] = new


def actor(placement, hash):
    return next(a for a in placement["Actors"] if a["Hash"] == (UINT64, hash))


def plan(chests, start_values=None, event_texts=(), stone_texts=(), shops=(), mido_castle_entry_removed=False):
    return {"seed": "1", "chests": [{"hash": str(h), "files": files, "drop_actor": d} for h, files, d in chests],
            "event_texts": [{"flow": flow, "event": event, "param": param, "text": text}
                            for flow, event, param, text in event_texts],
            "start_values": start_values or {},
            "stone_texts": [{"label": label, "text": text} for label, text in stone_texts],
            "shops": [shop_slot(slot, item, price) for slot, item, price in shops],
            "mido_castle_entry_removed": mido_castle_entry_removed}


def shop_slot(slot, item, price):
    shopkeepers, index, shelves = slot
    return {"shopkeepers": [{"actor": actor, "goods_list": goods_list} for actor, goods_list in shopkeepers],
            "slot": index, "shelves": [{"file": path, "hash": str(h)} for path, h in shelves], "item": item,
            "price": price}


def run(patch, game, plan, tmp):
    plan_path = tmp / "plan.json"
    plan_path.write_text(json.dumps(plan))
    return subprocess.run([patch, game.nup, plan_path, tmp / "mod"], capture_output=True, text=True)


def files(folder):
    return {p.relative_to(folder).as_posix(): p.read_bytes() for p in folder.rglob("*") if p.is_file()}


def patched(patch, game, plan, part="romfs"):
    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        result = run(patch, game, plan, tmp)
        assert result.returncode == 0, result.stderr
        written = files(tmp / "mod" / part)
    # Every mod carries the bomb's packs, which tested-bomb-mod checks.
    return {path: data for path, data in written.items() if path not in BOMB_LIFT_PACKS}


def assert_same_frame_header(ours, theirs):
    """The frame header descriptor and dictionary ID match: single segment, content size in the header, no
    checksum."""
    assert ours[4] == theirs[4], (ours[4], theirs[4])
    assert zstd.get_frame_info(ours).dictionary_id == zstd.get_frame_info(theirs).dictionary_id


def tested_mod(patch, game, mod_file):
    """The one-chest change the game accepted: the Deku Tree Slingshot chest gives the Hookshot."""
    tested = pathlib.Path(mod_file).read_bytes()
    mod = patched(patch, game, plan([(SLINGSHOT_CHEST, [DEKU_STATIC], "Item_Hookshot")]))
    assert sorted(mod) == [DEKU_STATIC], sorted(mod)
    assert_same_frame_header(mod[DEKU_STATIC], tested)
    # The tested mod wrote Item_Hookshot over Item_Range_SlingShot in the string table and padded it with zero bytes.
    # The game's own files never pad a string, so the bytes differ and the parsed content must match.
    assert game.placement(mod[DEKU_STATIC]) == game.placement(tested)


def tested_start_mod(patch, game, mod_file):
    """The start state the game accepted: two lock records and one Mido's House chest record start set."""
    tested = pathlib.Path(mod_file).read_bytes()
    mod = patched(patch, game, plan([], {FIRE_LOCK: True, WATER_LOCK: True, MIDO_CHEST: True}))
    assert sorted(mod) == [GAME_DATA_LIST], sorted(mod)
    ours, game_frame = mod[GAME_DATA_LIST], game.read(GAME_DATA_LIST)
    assert_same_frame_header(ours, game_frame)
    assert zstd.get_frame_info(ours).decompressed_size == zstd.get_frame_info(game_frame).decompressed_size
    assert zstd.decompress(ours, zstd_dict=game.zs_dictionary) == zstd.decompress(
        tested, zstd_dict=game.zs_dictionary)


def ordered(node):
    """A parsed BYML value with each dict as the list of its entries in file order, so the key order counts."""
    if isinstance(node, dict):
        return [(key, ordered(value)) for key, value in node.items()]
    if isinstance(node, list):
        return [ordered(item) for item in node]
    return node


def tested_substitute_mod(patch, game, romfs):
    """The chest that checks what Link owns, as the game accepted it: with the Boomerang in both its item and its
    substitute values, the Dodongo's Cavern bridge chest gave the Boomerang to Link with and without the Deku Shield."""
    tested = game.placement((pathlib.Path(romfs) / DODONGO_DYNAMIC).read_bytes())
    # The tested file also changed the platform chest in a way the patcher does not, so that actor is the game's.
    game_platform = actor(game.placement(game.read(DODONGO_DYNAMIC)), PLATFORM_CHEST)
    tested["Actors"] = [game_platform if a["Hash"] == (UINT64, PLATFORM_CHEST) else a for a in tested["Actors"]]
    mod = patched(patch, game, plan([(BRIDGE_CHEST, [DODONGO_DYNAMIC], "Item_Boomerang")]))
    assert DODONGO_DYNAMIC in mod, sorted(mod)
    assert ordered(game.placement(mod[DODONGO_DYNAMIC])) == ordered(tested)


def tested_gift_mod(patch, game, romfs):
    """The gift the game accepted: with the Boomerang in the call that gives the Market potion shop child's bottle, she
    gave the Boomerang."""
    tested = (pathlib.Path(romfs) / SHOP_CHILD_FLOW).read_bytes()
    mod = patched(patch, game, plan([], event_texts=[(*BOTTLE_GIFT, "Item_Boomerang")]))
    assert sorted(mod) == [SHOP_CHILD_FLOW], sorted(mod)
    ours = mod[SHOP_CHILD_FLOW]
    assert_same_frame_header(ours, tested)
    assert zstd.decompress(ours, zstd_dict=game.zs_dictionary) == zstd.decompress(tested, zstd_dict=game.zs_dictionary)


def tested_event_text_mods(patch, game, plan_file, *romfs):
    """Every event flow a plan edits, as the patcher writes it, is the flow of a mod the game accepted: after Gohma
    the warp left the castle quest at Step04 and Zelda's scene played; after Ruto's letter Kaepora Gaebora left the
    Water quest at Step07 and King Zora stood aside; after a lost bet race adult Malon left Epona's quest at
    ReadyForGetEponaSong and child Malon then taught her song."""
    texts = json.loads(pathlib.Path(plan_file).read_text())["event_texts"]
    tested = {path.relative_to(folder).as_posix(): path.read_bytes()
              for folder in map(pathlib.Path, romfs) for path in folder.rglob("*.bfevfl.zs")}
    mod = patched(patch, game, plan([], event_texts=[(t["flow"], t["event"], t["param"], t["text"]) for t in texts]))
    # The list may name played mods for texts this plan does not carry.
    assert set(mod) <= set(tested), sorted(set(mod) - set(tested))
    dictionary = game.zs_dictionary
    for path, ours in mod.items():
        theirs = tested[path]
        assert_same_frame_header(ours, theirs)
        assert zstd.decompress(ours, zstd_dict=dictionary) == zstd.decompress(theirs, zstd_dict=dictionary), path


def tested_stone_text_mod(patch, game, romfs):
    """The stone text the game showed: with it in the English pack, the Kokiri Forest stone said "They say this rumor
    was written by Kaepora." with the name in colour and the rest of the text white, then the second line."""
    path = "Mals/USen.Product.110.sarc.zs"
    tested = (pathlib.Path(romfs) / path).read_bytes()
    mod = patched(patch, game, plan([], stone_texts=[(KOKIRI_STONE, TESTED_STONE_TEXT)]))
    assert path in mod, sorted(mod)
    assert_same_frame_header(mod[path], tested)
    assert zstd.decompress(mod[path], zstd_dict=game.zs_dictionary) == zstd.decompress(
        tested, zstd_dict=game.zs_dictionary)


def tested_added_item_mod(patch, game, romfs):
    """The chests that name no item, as the game accepted them: with a Drop__DropActor added, the Forest Temple falling
    ceiling chest and the Training Ground lobby left chest each gave the Boomerang. Both files outgrew their size table
    entries."""
    romfs = pathlib.Path(romfs)
    chests = [(FOREST_CHEST_WITHOUT_DROP, FOREST_DYNAMIC), (LOBBY_CHEST_WITHOUT_DROP, TRAINING_GROUND_DYNAMIC)]
    ours = patched(patch, game, plan([(h, [path], "Item_Boomerang") for h, path in chests]))
    assert sorted(ours) == sorted([FOREST_DYNAMIC, TRAINING_GROUND_DYNAMIC, SIZE_TABLE]), sorted(ours)
    # The tested files were written with the patcher's BYML writer, so the decompressed files match byte for byte.
    for _, path in chests:
        assert zstd.decompress(ours[path], zstd_dict=game.dictionary) == zstd.decompress(
            (romfs / path).read_bytes(), zstd_dict=game.dictionary), path
    ours_sizes = dict(read_size_table(zstd.decompress(ours[SIZE_TABLE]))[1])
    tested_sizes = dict(read_size_table(zstd.decompress((romfs / SIZE_TABLE).read_bytes()))[1])
    for _, path in chests:
        assert ours_sizes[size_key(path)] == tested_sizes[size_key(path)], path


def tested_hammer_room_mod(patch, game, mod_file):
    """The chest with a substitute and no item of its own, as the game accepted it: with the red rupee added as its item
    and written over its substitute, the Training Ground hammer room chest gave a red rupee with and without the bow."""
    tested = pathlib.Path(mod_file).read_bytes()
    mod = patched(patch, game, plan([(HAMMER_ROOM_CHEST, [TRAINING_GROUND_STATIC], "TBox_Rupee_Red")]))
    assert sorted(mod) == [TRAINING_GROUND_STATIC], sorted(mod)
    ours = mod[TRAINING_GROUND_STATIC]
    assert_same_frame_header(ours, tested)
    assert zstd.decompress(ours, zstd_dict=game.dictionary) == zstd.decompress(tested, zstd_dict=game.dictionary)


def tested_program_mod(patch, game, mod):
    """The patch to the game's program that the game ran: with it, a weaker strength item left Link's lift level where
    it was, and a boss key or map from a small chest counted for its dungeon."""
    tested = files(pathlib.Path(mod) / "exefs")
    ours = patched(patch, game, plan([]), part="exefs")
    assert ours == tested, (sorted(ours), sorted(tested))


def tested_bomb_mod(patch, game, mod):
    """The bomb's lift grade at None that the game ran, in each actor pack that holds a copy of its lift file: with it,
    a child at lift level None took a bomb out of the bag and broke a wall with it, and bomb flowers still stayed in
    the ground for him."""
    tested = files(pathlib.Path(mod) / "romfs" / "Pack" / "Actor")
    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        result = run(patch, game, plan([]), tmp)
        assert result.returncode == 0, result.stderr
        ours = files(tmp / "mod" / "romfs" / "Pack" / "Actor")
    assert sorted(ours) == sorted(tested) == sorted(path.removeprefix("Pack/Actor/") for path in BOMB_LIFT_PACKS), \
        (sorted(ours), sorted(tested))
    for name, data in ours.items():
        assert_same_frame_header(data, tested[name])
        assert zstd.decompress(data, zstd_dict=game.pack_dictionary) == zstd.decompress(
            tested[name], zstd_dict=game.pack_dictionary), name


def tested_mido_mod(patch, game, mod):
    """Mido's schedule without his castle entry, as the game ran it: with it, a Link who had met Zelda found Mido at his
    post, and Mido let him through with the sword and the shield."""
    tested = files(pathlib.Path(mod) / "romfs")
    assert sorted(tested) == [MIDO_SCHEDULE], sorted(tested)
    assert patched(patch, game, plan([], mido_castle_entry_removed=True)).get(MIDO_SCHEDULE) == tested[MIDO_SCHEDULE]
    assert MIDO_SCHEDULE not in patched(patch, game, plan([]))


# Chests whose items rotate: each takes the next one's item. The Kokiri Sword chest sits in the child and adult files.
ROTATED = [
    (KOKIRI_SWORD_CHEST, ["Banc/KokiriForest/KokiriForest_Adult_Dynamic.bcett.byml.zs",
                          "Banc/KokiriForest/KokiriForest_Child_Dynamic.bcett.byml.zs"]),
    (SLINGSHOT_CHEST, [DEKU_STATIC]),
    (8579518566955184384, [DEKU_STATIC]),
    (12161709650443144121, ["Banc/DodongoCavern/DodongoCavern_Static.bcett.byml.zs"]),
    (11718708984125260282, ["Banc/BottomOfTheWell/BottomOfTheWell_Dynamic.bcett.byml.zs"]),
]
# A chest planned to keep its item. Its file has no other planned chest, so it must not be written.
KEPT = (9038700938965170329, [FOREST_DYNAMIC])


def rotated(game):
    items = [actor(game.placement(game.read(files[0])), h)["Dynamic"]["Drop__DropActor"] for h, files in ROTATED]
    assert len(set(items)) == len(items), items
    return [(h, files, items[(i + 1) % len(items)]) for i, (h, files) in enumerate(ROTATED)]


def larger_plan(patch, game):
    originals = {path: game.placement(game.read(path)) for _, files in ROTATED + [KEPT] for path in files}
    chests = rotated(game)
    kept_hash, kept_files = KEPT
    chests.append((kept_hash, kept_files, actor(originals[kept_files[0]], kept_hash)["Dynamic"]["Drop__DropActor"]))

    mod = patched(patch, game, plan(chests))
    changed = sorted({path for _, files in ROTATED for path in files})
    assert sorted(mod) == changed, sorted(mod)
    for path in changed:
        expected = originals[path]
        for h, files, drop_actor in chests:
            if path in files:
                actor(expected, h)["Dynamic"]["Drop__DropActor"] = drop_actor
        assert game.placement(mod[path]) == expected, path


def with_defaults(game, values):
    data = game.game_data_list(game.read(GAME_DATA_LIST))
    for name, value in values.items():
        kind, tag = ("Bool", BOOL) if isinstance(value, bool) else ("Int", INT)
        entry = next(e for e in data["Data"][kind] if e["Hash"][1] == HASHES[name])
        assert entry["DefaultValue"][0] == tag and entry["DefaultValue"] != (tag, int(value)), (name, entry)
        entry["DefaultValue"] = (tag, int(value))
    return data


def together(patch, game):
    """Chests, gifts, start values and stone texts in one plan give the files each gives alone. The chests, the gifts
    and the stone texts each raise size table entries, and the one table holds them all."""
    values = {FIRE_LOCK: True, WATER_LOCK: True}
    parts = [plan(rotated(game) + [(LONGSHOT_CHEST, [WATER_STATIC], "Item_DekuStickBundle_Small")]),
             plan([], event_texts=[(*BOTTLE_GIFT, "Item_Boomerang"), (*DINS_FIRE_GIFT, "Item_HeavyBoot")]),
             plan([], values),
             plan([], stone_texts=[(SHORT_RUMOUR_STONE, LONG_STONE_TEXT)])]
    whole = {key: [entry for part in parts for entry in part[key]]
             for key in ("chests", "event_texts", "stone_texts", "shops")}
    whole |= {"seed": "1", "start_values": values, "mido_castle_entry_removed": False}
    *alone, combined = (patched(patch, game, part) for part in parts + [whole])
    tables = [read_size_table(zstd.decompress(part.pop(SIZE_TABLE))) for part in alone if SIZE_TABLE in part]
    assert len(tables) == 3, len(tables)
    header, entries, names = read_size_table(zstd.decompress(combined.pop(SIZE_TABLE)))
    assert all((header, names) == (table[0], table[2]) for table in tables)
    assert entries == [(key, max(table[1][i][1] for table in tables)) for i, (key, _) in enumerate(entries)]
    assert combined == {path: data for part in alone for path, data in part.items()}, sorted(combined)

    assert game.game_data_list(combined[GAME_DATA_LIST]) == with_defaults(game, values)


def start_values(patch, game):
    """A value set, a value cleared and a whole number changed. The list keeps its size, so it still fits its size
    table entry."""
    values = {FIRE_LOCK: True, FOREST_EXIT_SHUT: False, TRIALS_LEFT: 0}
    mod = patched(patch, game, plan([], values))
    assert sorted(mod) == [GAME_DATA_LIST], sorted(mod)
    ours = zstd.decompress(mod[GAME_DATA_LIST], zstd_dict=game.zs_dictionary)
    assert len(ours) == len(zstd.decompress(game.read(GAME_DATA_LIST), zstd_dict=game.zs_dictionary))
    assert game.game_data_list(mod[GAME_DATA_LIST]) == with_defaults(game, values)


def goods(actor):
    return f"Work/Actor/{actor}.engine__actor__ActorParam.gyml"


def shop(patch, game):
    def each(shopkeepers, entry):
        return {keeper: entry for keeper, _ in shopkeepers}

    # The goods' base prices in PouchActorInfo: the Hylian Shield 80 rupees, 5 bombs 35 and 10 arrows 20. The
    # short-stock Kokiri shopkeeper's copy has every key of a slot, and the other two leave out the defaults.
    released = {"IsCheckReleaseGameData": (BOOL, 0), "ReleaseGameData": ""}
    sales = [
        (GORON_TUNIC_SLOT, "TBox_Shield_Hylian", 35, "Shop_Weapon_Shield_Hylian", each(GORON_SHOPKEEPERS, {
            "Actor": goods("Shop_Weapon_Shield_Hylian"), "PriceOffset": (INT, 2**32 - 45), "StockNum": (INT, 1)})),
        (GORON_HEART_SLOT, "Item_BombBundle_Small", 0, "Shop_BombBundle_01", each(GORON_SHOPKEEPERS, {
            "Actor": goods("Shop_BombBundle_01"), "PriceOffset": (INT, 2**32 - 35), "StockNum": (INT, 1)})),
        (KOKIRI_STICKS_SLOT, "TBox_ArrowBundle_Large", 95, "Shop_ArrowBundle_01", each(KOKIRI_SHOPKEEPERS, {
            "Actor": goods("Shop_ArrowBundle_01"), "PriceOffset": (INT, 75), "Release": released,
            "StockNum": (INT, 1)}) | {"Npc_Kokiri_001_Shop_Child_ShortStock": {
                "Actor": goods("Shop_ArrowBundle_01"), "ConditionalPriceOffsetList": [], "IsDefaultFocus": (BOOL, 0),
                "PriceOffset": (INT, 75), "Release": released, "StockNum": (INT, 1)}}),
    ]
    chest = (STORMS_GROTTO_CHEST, [KOKIRI_ADULT_STATIC, KOKIRI_CHILD_STATIC], "Item_Hookshot")
    mod = patched(patch, game, plan([chest], {FIRE_LOCK: True}, shops=[sale[:3] for sale in sales]))

    packs, goods_lists, placements = {}, {}, {}
    data = with_defaults(game, {FIRE_LOCK: True})
    for (shopkeepers, index, shelves), _, _, goods_actor, entries in sales:
        for keeper, goods_list in shopkeepers:
            path = f"Pack/Actor/{keeper}.pack.zs"
            member = f"Component/ShopParam/{goods_list}.game__component__ShopParam.bgyml"
            pack = packs.setdefault(path, game.pack(game.read(path)))
            goods_lists.setdefault((path, member), byml(pack[member]))["GoodsList"][index] = entries[keeper]
            sold = next(e for e in data["Data"]["Int"] if e["Hash"][1] == SOLD_NUM_HASHES[keeper, index])
            assert sold["ResetTypeValue"] == (INT, 2), sold
            sold["ResetTypeValue"] = (INT, 0)
        for path, h in shelves:
            actor(placements.setdefault(path, game.placement(game.read(path))), h)["Gyaml"] = goods_actor
    for path in chest[1]:
        actor(placements[path], STORMS_GROTTO_CHEST)["Dynamic"]["Drop__DropActor"] = chest[2]

    _, entries, _ = read_size_table(zstd.decompress(game.read(SIZE_TABLE)))
    def needs(path, dictionary, overhead=264):
        return size_key(path), load_size(len(zstd.decompress(mod[path], zstd_dict=dictionary)), overhead)

    needed = dict([needs(p, game.dictionary) for p in placements] + [needs(GAME_DATA_LIST, game.zs_dictionary)] +
                  [needs(p, game.pack_dictionary, PACK_OVERHEAD) for p in packs])
    expected_entries = [(key, max(size, needed.get(key, 0))) for key, size in entries]
    raised = expected_entries != entries
    assert sorted(mod) == sorted([*packs, *placements, GAME_DATA_LIST] + [SIZE_TABLE] * raised), sorted(mod)
    if raised:
        assert read_size_table(zstd.decompress(mod[SIZE_TABLE]))[1] == expected_entries
    for path, files in packs.items():
        ours = game.pack(mod[path])
        assert ours.keys() == files.keys(), path
        for name, member in files.items():
            if (path, name) in goods_lists:
                assert ordered(byml(ours[name])) == ordered(goods_lists[path, name]), (path, name)
            else:
                assert ours[name] == member, (path, name)
        assert_same_frame_header(mod[path], game.read(path))
    for path, placement in placements.items():
        assert ordered(game.placement(mod[path])) == ordered(placement), path
    assert game.game_data_list(mod[GAME_DATA_LIST]) == data


def shop_table(patch, game):
    with open(pathlib.Path(__file__).resolve().parent.parent / "remake" / "shops.tsv", newline="") as f:
        rows = list(csv.DictReader(f, delimiter="\t"))
    slots = []
    for row in rows:
        shopkeepers = [tuple(cell.split(":")) for cell in row["shopkeepers"].split(",")]
        shelves = [(path, int(h)) for path, h in (cell.rsplit(":", 1) for cell in row["shelves"].split(","))]
        slots.append((shopkeepers, int(row["slot"]), shelves))
    mod = patched(patch, game, plan([], shops=[(slot, "TBox_Shield_Hylian", 5) for slot in slots]))
    sold = {}
    for row, (shopkeepers, index, shelves) in zip(rows, slots):
        for keeper, goods_list in shopkeepers:
            path = f"Pack/Actor/{keeper}.pack.zs"
            member = f"Component/ShopParam/{goods_list}.game__component__ShopParam.bgyml"
            assert byml(game.pack(game.read(path))[member])["GoodsList"][index]["Actor"] == goods(row["goods"]), row
            assert byml(game.pack(mod[path])[member])["GoodsList"][index]["Actor"] == goods("Shop_Weapon_Shield_Hylian")
            sold[keeper, index] = row["location"]
        for path, h in shelves:
            assert actor(game.placement(game.read(path)), h)["Gyaml"] == row["goods"], (row["location"], h)
            assert actor(game.placement(mod[path]), h)["Gyaml"] == "Shop_Weapon_Shield_Hylian", (row["location"], h)
    assert len(rows) == 64 and len(sold) == 12 * 8, (len(rows), len(sold))
def tested_new_actor_mod(patch, game, romfs):
    """A slot that sells the Megaton Hammer adds the goods actor the game showed and sold: its pack, its rows in the
    three actor tables and their size table entries are the tested mod's."""
    romfs = pathlib.Path(romfs)
    mod = patched(patch, game, plan([], shops=[(GORON_TUNIC_SLOT, "Item_Hammer", 9)]))
    pack = "Pack/Actor/Shop_Item_Hammer.pack.zs"
    assert zstd.decompress(mod[pack], zstd_dict=game.pack_dictionary) == \
        zstd.decompress((romfs / pack).read_bytes(), zstd_dict=game.pack_dictionary)
    assert_same_frame_header(mod[pack], game.read("Pack/Actor/Shop_Armor_Zora.pack.zs"))
    for path in (ACTOR_INFO, GAME_ACTOR_INFO, POUCH_ACTOR_INFO):
        assert zstd.decompress(mod[path]) == zstd.decompress((romfs / path).read_bytes()), path
    ours = dict(read_size_table(zstd.decompress(mod[SIZE_TABLE]))[1])
    theirs = dict(read_size_table(zstd.decompress((romfs / SIZE_TABLE).read_bytes()))[1])
    for path in (pack, ACTOR_INFO, GAME_ACTOR_INFO, POUCH_ACTOR_INFO):
        assert ours[size_key(path)] == theirs[size_key(path)], path


def resolved(files, member):
    """A pack member's values with its $parent chain merged in, as far as the pack holds the chain."""
    data = byml(files[member])
    parent = data.pop("$parent", None)
    path = parent and parent.removeprefix("Work/").removesuffix(".gyml") + ".bgyml"
    if path not in files:
        return data

    def merge(base, over):
        if isinstance(base, dict) and isinstance(over, dict):
            return base | {key: merge(base[key], value) if key in base else value for key, value in over.items()}
        return over

    return merge(resolved(files, path), data)


def new_actors(patch, game):
    """Items with no goods actor in the game each get one, which every slot that sells the item names, at the slot's
    price over a base of 0. Each shows and names the item as the item's own actor does, and gives what that actor
    gives. The three actor tables take each new row in name order, and the size table takes each new pack."""
    sales = [(GORON_TUNIC_SLOT, "TBox_Rupee_Blue", 10), (GORON_HEART_SLOT, "Item_Shield_Mirror", 20),
             (KOKIRI_STICKS_SLOT, "TBox_Rupee_Blue", 30)]
    mod = patched(patch, game, plan([], shops=sales))
    new = {"TBox_Rupee_Blue": "Shop_TBox_Rupee_Blue", "Item_Shield_Mirror": "Shop_Item_Shield_Mirror"}
    assert sorted(p for p in mod if p.startswith("Pack/Actor/Shop_")) == \
        sorted(f"Pack/Actor/{actor}.pack.zs" for actor in new.values()), sorted(mod)

    for (shopkeepers, index, _), item, price in sales:
        for keeper, goods_list in shopkeepers:
            member = f"Component/ShopParam/{goods_list}.game__component__ShopParam.bgyml"
            entry = byml(game.pack(mod[f"Pack/Actor/{keeper}.pack.zs"])[member])["GoodsList"][index]
            assert (entry["Actor"], entry["PriceOffset"]) == (goods(new[item]), (INT, price)), (keeper, entry)

    zero_price = game.pack(game.read("Pack/Actor/Shop_Mask_Gerudo.pack.zs"))[
        "GameParameter/PriceParam/Item_Mask_Gerudo.game__pouchcontent__PriceParam.bgyml"]
    for item, actor in new.items():
        ours = game.pack(mod[f"Pack/Actor/{actor}.pack.zs"])
        theirs = game.pack(game.read(f"Pack/Actor/{item}.pack.zs"))
        components = resolved(ours, f"Actor/{actor}.engine__actor__ActorParam.bgyml")["Components"]
        own = resolved(theirs, f"Actor/{item}.engine__actor__ActorParam.bgyml")["Components"]
        for ref in ("ActorNameRef", "ModelInfoRef", "AnimationRef"):
            assert components[ref] == own[ref], (item, ref)
        for ref in ("ModelInfoRef", "AnimationRef"):
            member = own[ref].lstrip("?")
            assert ours[member] == theirs[member], (item, member)
        pouch = resolved(ours, components["PouchContentRef"].lstrip("?"))
        item_pouch = resolved(theirs, own["PouchContentRef"].lstrip("?"))
        gives = ("PouchActor", "BundleActor", "BundleActorNum")
        assert {k: pouch.get(k) for k in gives} == ({k: item_pouch.get(k) for k in gives} if any(
            k in item_pouch for k in gives) else {"PouchActor": goods(item), "BundleActor": None,
                                                    "BundleActorNum": None}), (item, pouch)
        table = resolved(ours, components["GameParameterTableRef"].lstrip("?"))
        assert ours[table["Components"]["PriceParam"].lstrip("?")] == zero_price, item

    rows = {path: byml(zstd.decompress(game.read(path))) for path in (ACTOR_INFO, GAME_ACTOR_INFO, POUCH_ACTOR_INFO)}
    by_id = {path: {row["__RowId"]: row for row in table} for path, table in rows.items()}
    for path, table in rows.items():
        ours = byml(zstd.decompress(mod[path]))
        assert [row["__RowId"] for row in ours] == sorted([row["__RowId"] for row in table] + list(new.values()))
        assert [row for row in ours if row["__RowId"] not in new.values()] == table, path
        for item, actor in new.items():
            row = next(row for row in ours if row["__RowId"] == actor)
            if path == ACTOR_INFO:
                for key in ("ActorName", "FmdbName", "ModelProjectName", "AnimationResources"):
                    assert row[key] == by_id[path][item][key], (item, key)
            elif path == GAME_ACTOR_INFO:
                assert row == by_id[path]["Shop_Armor_Zora"] | {"__RowId": actor}, (item, row)
            else:
                expected = {key: value for key, value in by_id[path][item].items() if key != "BuyingPrice"}
                assert row == expected | {"__RowId": actor}, (item, row)

    header, entries, names = read_size_table(zstd.decompress(game.read(SIZE_TABLE)))
    ours_header, ours_entries, ours_names = read_size_table(zstd.decompress(mod[SIZE_TABLE]))
    keys = [key for key, _ in ours_entries]
    assert keys == sorted(keys) and names == ours_names and ours_header[6:] != header[6:]
    ours_entries = dict(ours_entries)
    for actor in new.values():
        path = f"Pack/Actor/{actor}.pack.zs"
        size = len(zstd.decompress(mod[path], zstd_dict=game.pack_dictionary))
        assert ours_entries[size_key(path)] == load_size(size, PACK_OVERHEAD), path
    for path, overhead in ((ACTOR_INFO, ACTOR_INFO_OVERHEAD), (GAME_ACTOR_INFO, 264), (POUCH_ACTOR_INFO, 264)):
        assert ours_entries[size_key(path)] >= load_size(len(zstd.decompress(mod[path])), overhead), path
    assert len(ours_entries) == len(entries) + len(new)


def read_size_table(data):
    """A decompressed RESTBL size table: its header, its (CRC-32, size) entries in order, and the name entries."""
    assert data[:6] == b"RESTBL" and struct.unpack_from("<I", data, 6)[0] == 1, data[:10]
    end = 22 + 8 * struct.unpack_from("<I", data, 14)[0]
    return data[:22], list(struct.iter_unpack("<II", data[22:end])), data[end:]


def size_key(path):
    """The size table keys a file by the CRC-32 of its path without the .zs."""
    return zlib.crc32(path.removesuffix(".zs").encode())


def load_size(size, overhead=264):
    """The size table entry the game gives one of its files, from the file's decompressed size. The overhead is 264
    for a placement file and EVENT_FLOW_OVERHEAD for an event flow."""
    return (size + 31) // 32 * 32 + overhead


EVENT_FLOW_OVERHEAD = 272


def placement_sizes(game, mod, paths):
    """The decompressed size of each placement file in the mod, by its size table key."""
    return {size_key(p): len(zstd.decompress(mod[p], zstd_dict=game.dictionary)) for p in paths}


def size_table_entries(patch, game):
    # The rotated chests keep their files' sizes rounded up to 32 bytes. The new item grows the Water Temple file past
    # what its entry lets the game load.
    chests = rotated(game) + [(LONGSHOT_CHEST, [WATER_STATIC], "Item_DekuStickBundle_Small")]
    game_frame = game.read(SIZE_TABLE)
    header, entries, names = read_size_table(zstd.decompress(game_frame))
    mod = patched(patch, game, plan(chests))
    changed = sorted({path for _, files, _ in chests for path in files})
    assert sorted(mod) == changed + [SIZE_TABLE], sorted(mod)
    sizes = placement_sizes(game, mod, changed)
    game_sizes = dict(entries)
    assert [p for p in changed if load_size(sizes[size_key(p)]) > game_sizes[size_key(p)]] == [WATER_STATIC]
    # Each written file's entry is large enough for its size, and every other byte of the table is the game's.
    expected = [(key, max(size, load_size(sizes[key])) if key in sizes else size) for key, size in entries]
    assert read_size_table(zstd.decompress(mod[SIZE_TABLE])) == (header, expected, names)
    assert_same_frame_header(mod[SIZE_TABLE], game_frame)


def size_table_unchanged(patch, game):
    _, entries, _ = read_size_table(zstd.decompress(game.read(SIZE_TABLE)))
    game_sizes = dict(entries)
    mod = patched(patch, game, plan(rotated(game)))
    changed = sorted({path for _, files in ROTATED for path in files})
    sizes = placement_sizes(game, mod, changed)
    # Every written file still fits its entry, so the table is left out.
    assert all(load_size(size) <= game_sizes[key] for key, size in sizes.items()), sizes
    assert sorted(mod) == changed, sorted(mod)


def gift(patch, game):
    """A gift written into its flow: read apart from the patcher, the flow is the game's with the one parameter
    changed. The Boomerang's name keeps the flow inside its size table entry, so the table is left out."""
    _, entries, _ = read_size_table(zstd.decompress(game.read(SIZE_TABLE)))
    mod = patched(patch, game, plan([], event_texts=[(*BOTTLE_GIFT, "Item_Boomerang")]))
    assert sorted(mod) == [SHOP_CHILD_FLOW], sorted(mod)
    data = zstd.decompress(mod[SHOP_CHILD_FLOW], zstd_dict=game.zs_dictionary)
    assert load_size(len(data), EVENT_FLOW_OVERHEAD) <= dict(entries)[size_key(SHOP_CHILD_FLOW)]
    expected = game.event_flow(game.read(SHOP_CHILD_FLOW))
    give(expected, BOTTLE_GIFT, "Item_Bottle", "Item_Boomerang")
    assert event_flow(data) == expected

    # The item the flow already gives changes nothing, so nothing is written.
    mod = patched(patch, game, plan([], event_texts=[(*BOTTLE_GIFT, "Item_Bottle")]))
    assert sorted(mod) == [], sorted(mod)

    # A shorter name leaves the flow no larger, so the size table is left out, and the flow reads as the game's with
    # that name. The same holds for a name whose text ends on an 8-byte boundary.
    for site, old, new in ((DINS_FIRE_GIFT, "Item_DinFire", "Item_Hammer"),
                           (NAYRUS_LOVE_GIFT, "Item_NayruLove", "Item_Hammer")):
        mod = patched(patch, game, plan([], event_texts=[(*site, new)]))
        assert sorted(mod) == [site[0]], sorted(mod)
        expected = game.event_flow(game.read(site[0]))
        give(expected, site, old, new)
        assert game.event_flow(mod[site[0]]) == expected

    # Two gifts in one flow, in either order of the plan: both are written.
    gifts = [(*COJIRO_GIFT, "Item_Boomerang"), (*POCKET_EGG_GIFT, "Item_Gauntlet_Silver")]
    expected = game.event_flow(game.read(ANJU_FLOW))
    give(expected, COJIRO_GIFT, "Item_CojiroCucco", "Item_Boomerang")
    give(expected, POCKET_EGG_GIFT, "Item_PocketEgg", "Item_Gauntlet_Silver")
    for order in (gifts, gifts[::-1]):
        mod = patched(patch, game, plan([], event_texts=order))
        assert ANJU_FLOW in mod, sorted(mod)
        assert game.event_flow(mod[ANJU_FLOW]) == expected


def gift_size_table(patch, game):
    """A gift that grows its flow past its size table entry: the entry is raised to what the flow needs, every other
    byte of the table is the game's, and the flow, read apart from the patcher, is the game's with the one parameter
    changed."""
    path = DINS_FIRE_GIFT[0]
    header, entries, names = read_size_table(zstd.decompress(game.read(SIZE_TABLE)))
    mod = patched(patch, game, plan([], event_texts=[(*DINS_FIRE_GIFT, "Item_HeavyBoot")]))
    assert sorted(mod) == sorted([path, SIZE_TABLE]), sorted(mod)
    data = zstd.decompress(mod[path], zstd_dict=game.zs_dictionary)
    table = zstd.decompress(mod[SIZE_TABLE])
    needed = load_size(len(data), EVENT_FLOW_OVERHEAD)
    assert needed > dict(entries)[size_key(path)], needed
    assert read_size_table(table) == (header, [(k, needed if k == size_key(path) else v) for k, v in entries], names)
    expected = game.event_flow(game.read(path))
    give(expected, DINS_FIRE_GIFT, "Item_DinFire", "Item_HeavyBoot")
    assert event_flow(data) == expected


def without_mask(flow):
    """A parsed GossipStone flow in which the mask question goes on to the stone's talk on both answers, and the talk
    straight on to the stone's message."""
    events = flow["events"]
    index = {event[0]: i for i, event in enumerate(events)}
    mask_held, mask_worn, navi = index["Event112"], index["Event25"], index["Event39"]
    talk, mask_lines, message = index["Event6"], index["Event90"], index["Event18"]
    query, opening = events[mask_held], events[talk]
    assert query[3] == struct.pack("<IHxxIHxx", 0, mask_worn, 1, navi), query[3]
    query[3] = struct.pack("<IHxxIHxx", 0, talk, 1, talk)
    assert struct.unpack_from("<H", opening[1], 2)[0] == mask_lines, opening[1]
    opening[1] = opening[1][:2] + struct.pack("<H", message) + opening[1][4:]
    return flow


def stone_texts(patch, game):
    """Stone texts written into the two English text packs and no other: read apart from the patcher, each is the
    game's pack with the two messages changed. Both grow past their size table entries, and the entries are raised.
    The stones' flow, read apart from the patcher, sends the mask question on to the stone's message, and keeps its
    size."""
    header, entries, names = read_size_table(zstd.decompress(game.read(SIZE_TABLE)))
    packs = ENGLISH_PACKS
    planned = [(KOKIRI_STONE, TESTED_STONE_TEXT), (SHORT_RUMOUR_STONE, LONG_STONE_TEXT)]
    mod = patched(patch, game, plan([], stone_texts=planned))
    assert sorted(mod) == sorted(packs + [STONE_FLOW, SIZE_TABLE]), sorted(mod)
    expected_flow = without_mask(game.event_flow(game.read(STONE_FLOW)))
    assert event_flow(zstd.decompress(mod[STONE_FLOW], zstd_dict=game.zs_dictionary)) == expected_flow
    sizes = {}
    for path in packs:
        assert_same_frame_header(mod[path], game.read(path))
        ours, theirs = game.text_pack(mod[path]), game.text_pack(game.read(path))
        assert list(ours) == list(theirs), path
        assert {name for name in ours if ours[name] != theirs[name]} == {STONE_MESSAGES}, path
        labels, texts = msbt(ours[STONE_MESSAGES])
        game_labels, expected = msbt(theirs[STONE_MESSAGES])
        expected |= {KOKIRI_STONE: TESTED_STONE_MESSAGE, SHORT_RUMOUR_STONE: LONG_STONE_MESSAGE}
        assert (labels, list(texts.items())) == (game_labels, list(expected.items())), path
        sizes[size_key(path)] = len(zstd.decompress(mod[path], zstd_dict=game.zs_dictionary))
    assert all(load_size(size, TEXT_PACK_OVERHEAD) > dict(entries)[key] for key, size in sizes.items()), sizes
    expected = [(key, max(size, load_size(sizes[key], TEXT_PACK_OVERHEAD)) if key in sizes else size)
                for key, size in entries]
    assert read_size_table(zstd.decompress(mod[SIZE_TABLE])) == (header, expected, names)


def message_bytes(text):
    data = b""
    for part in re.split(r"(\{color:\d+\}|\{page\})", text):
        if part == "{page}":
            data += RUMOUR_PAGE
        elif part.startswith("{color:"):
            data += color(int(part.removeprefix("{color:").removesuffix("}")))
        else:
            data += part.encode()
    return data


def seed_plan(patch, game, plan_file):
    """A seed's plan, applied: each written file is the game's with every planned chest giving its item, also as its
    substitute where it has one, and as an added value where it names none, and every planned stone saying its text in
    both English text packs. Each fits the size table entry the game reads."""
    seed = json.loads(pathlib.Path(plan_file).read_text())
    expected, kinds = {}, set()
    for chest in seed["chests"]:
        for path in chest["files"]:
            if path not in expected:
                expected[path] = game.placement(game.read(path))
            dynamic = actor(expected[path], int(chest["hash"]))["Dynamic"]
            kinds.add(("Drop__DropActor" in dynamic, "Drop__DropActor_Substitute" in dynamic))
            if "Drop__DropActor_Substitute" in dynamic:
                dynamic["Drop__DropActor_Substitute"] = chest["drop_actor"]
            dynamic["Drop__DropActor"] = chest["drop_actor"]
            # The game's dicts are in key order, and an added value takes its place in it.
            entries = sorted(dynamic.items())
            dynamic.clear()
            dynamic.update(entries)
    assert kinds == set(itertools.product((True, False), repeat=2)), kinds
    changed = sorted(path for path, placement in expected.items()
                     if ordered(placement) != ordered(game.placement(game.read(path))))
    flows = {path: game.event_flow(game.read(path)) for path in {text["flow"] for text in seed["event_texts"]}}
    for text in seed["event_texts"]:
        string_parameter(flows[text["flow"]], text["event"], text["param"])[1] = text["text"]
    assert seed["stone_texts"]
    mod = patched(patch, game, seed)
    written = sorted(changed + list(flows) + ENGLISH_PACKS + [STONE_FLOW])
    assert sorted(set(mod) - {SIZE_TABLE, GAME_DATA_LIST}) == written, sorted(mod)
    for path in changed:
        assert ordered(game.placement(mod[path])) == ordered(expected[path]), path
    for path, flow in flows.items():
        assert game.event_flow(mod[path]) == flow, path
    needed = {key: load_size(size) for key, size in placement_sizes(game, mod, changed).items()}
    planned = {entry["label"]: RUMOUR_OPENING + message_bytes(entry["text"]) for entry in seed["stone_texts"]}
    for path in ENGLISH_PACKS:
        ours, theirs = game.text_pack(mod[path]), game.text_pack(game.read(path))
        assert {name for name in ours if ours[name] != theirs[name]} == {STONE_MESSAGES}, path
        assert msbt(ours[STONE_MESSAGES])[1] == msbt(theirs[STONE_MESSAGES])[1] | planned, path
        size = len(zstd.decompress(mod[path], zstd_dict=game.zs_dictionary))
        needed[size_key(path)] = load_size(size, TEXT_PACK_OVERHEAD)
    entries = dict(read_size_table(zstd.decompress(mod[SIZE_TABLE]))[1])
    assert all(size <= entries[key] for key, size in needed.items()), needed


def other_version(patch, game):
    """The game file with the base game's program where the update's is: a program of another build, as a game file of
    another version holds."""
    with open(game.nup, "rb") as f:
        head = f.read(game.toc_end)
        f.seek(game.program_offsets[BASE_PACKAGE])
        base_program = f.read(NSO_HEADER_SIZE)
    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        other = tmp / "other.nup"
        with open(other, "wb") as f:
            f.write(head)
            f.seek(game.program_offsets[UPDATE_PACKAGE])
            f.write(base_program)
        plan_path = tmp / "plan.json"
        plan_path.write_text(json.dumps(plan([])))
        result = subprocess.run([patch, other, plan_path, tmp / "mod"], capture_output=True, text=True)
        assert result.returncode == 1, result.stderr
        assert result.stderr == OTHER_VERSION + "\n", result.stderr
        assert not (tmp / "mod").exists()


def bad_input(patch, game):
    # Each plan starts with a valid change, so a program that wrote as it went would leave a file behind.
    good = plan([(SLINGSHOT_CHEST, [DEKU_STATIC], "Item_Hookshot")])
    valid = good["chests"][0]
    missing = "Banc/Nowhere/Nowhere_Static.bcett.byml.zs"
    cases = {
        "not this shape": (
            {"hash": KOKIRI_SWORD_CHEST, "files": [FOREST_DYNAMIC], "drop_actor": "Item_Hookshot"},
            ["plan.json", "chests[1]"]),
        "no files": (
            {"hash": str(KOKIRI_SWORD_CHEST), "files": [], "drop_actor": "Item_Hookshot"},
            ["plan.json", "chests[1]"]),
        "no item": (
            {"hash": str(KOKIRI_SWORD_CHEST), "files": [FOREST_DYNAMIC], "drop_actor": ""},
            ["plan.json", "chests[1]"]),
        "file not in the game": (
            {"hash": str(KOKIRI_SWORD_CHEST), "files": [missing], "drop_actor": "Item_Hookshot"},
            [str(KOKIRI_SWORD_CHEST), missing]),
        "hash not in the file": (
            {"hash": str(KOKIRI_SWORD_CHEST), "files": [FOREST_DYNAMIC], "drop_actor": "Item_Hookshot"},
            [str(KOKIRI_SWORD_CHEST), FOREST_DYNAMIC]),
    }
    plans = {name: (good | {"chests": [valid, chest]}, named) for name, (chest, named) in cases.items()}
    plans["no event_texts"] = (
        {"seed": "1", "chests": [valid], "start_values": {}, "stone_texts": []}, ["plan.json", "event_texts"])
    plans["no start_values"] = (
        {"seed": "1", "chests": [valid], "event_texts": [], "stone_texts": []}, ["plan.json", "start_values"])
    plans["no stone_texts"] = (
        {"seed": "1", "chests": [valid], "event_texts": [], "start_values": {}}, ["plan.json", "stone_texts"])
    plans["no mido_castle_entry_removed"] = (
        {key: value for key, value in good.items() if key != "mido_castle_entry_removed"},
        ["plan.json", "mido_castle_entry_removed"])
    valid_stone = {"label": KOKIRI_STONE, "text": "They say."}
    other = SHORT_RUMOUR_STONE
    stones = {
        "stone text not this shape": ({"label": other}, ["plan.json", "stone_texts[1]"]),
        "stone text without a label": ({"label": "", "text": "They say."}, ["plan.json", "stone_texts[1]"]),
        "stone text without a text": ({"label": other, "text": ""}, ["plan.json", "stone_texts[1]"]),
        "stone text listed twice": (dict(valid_stone, text="Again."), ["stone_texts[1]", KOKIRI_STONE, "twice"]),
        "unknown tag": ({"label": other, "text": "{colour:0}Saria"}, ["stone_texts[1]", "{colour:0}"]),
        "tag not closed": ({"label": other, "text": "{color:0 Saria"}, ["stone_texts[1]", "{color:0 Saria"]),
        "colour out of range": ({"label": other, "text": "{color:65536}"}, ["stone_texts[1]", "{color:65536}"]),
        "code byte outside a tag": ({"label": other, "text": "Sa\u000eria"}, ["stone_texts[1]", "code"]),
        "stone not in the table": (
            {"label": NO_STONE, "text": "They say."},
            ["plan.json", "stone_texts[1]", NO_STONE, "remake/stones.tsv"]),
    }
    for name, (entry, named) in stones.items():
        plans[name] = (good | {"stone_texts": [valid_stone, entry]}, named)
    plans["no shops"] = (
        {"seed": "1", "chests": [valid], "event_texts": [], "start_values": {}, "stone_texts": []}, ["plan.json", "shops"])
    valid_shop = shop_slot(GORON_TUNIC_SLOT, "TBox_Shield_Hylian", 35)
    elsewhere = {"file": GORON_ADULT_STATIC, "hash": str(KOKIRI_STICKS_SLOT[2][0][1])}
    shops = {
        "shop slot not this shape": (dict(valid_shop, price=-1), ["plan.json", "shops[1]"]),
        "item with no actor": (dict(valid_shop, item="Item_Nowhere"), ["Item_Nowhere", "no such file"]),
        "shelf not in the file": (dict(valid_shop, shelves=[elsewhere]), [elsewhere["hash"], GORON_ADULT_STATIC]),
    }
    for name, (entry, named) in shops.items():
        plans[name] = (good | {"shops": [valid_shop, entry]}, named)
    valid_gift = {"flow": SHOP_CHILD_FLOW, "event": "Event171", "param": "ActorName", "text": "Item_Boomerang"}
    nowhere = "Event/EventFlow/Nowhere.bfevfl.zs"
    timeline = "Event/EventFlow/Dm_Awake_Fire.bfevfl.zs"
    # Dm_LonLon_0030's Event40 gives 20 rupees, a number.
    rupees = {"flow": "Event/EventFlow/Dm_LonLon_0030.bfevfl.zs", "event": "Event40", "param": "Value"}
    texts = {
        "event text not this shape": (
            {key: valid_gift[key] for key in ("flow", "event", "param")}, ["plan.json", "event_texts[1]"]),
        "event text empty": (
            dict(zip(("flow", "event", "param"), DINS_FIRE_GIFT), text=""), ["plan.json", "event_texts[1]", "text"]),
        "event text listed twice": (
            dict(valid_gift, text="Item_Bottle"),
            ["plan.json", "event_texts[1]", "Event171 ActorName", SHOP_CHILD_FLOW, "twice"]),
        "flow not in the game": (dict(valid_gift, flow=nowhere), ["Event171 ActorName", nowhere, "no such file"]),
        "flow a timeline": (dict(valid_gift, flow=timeline), ["Event171 ActorName", timeline, "timeline"]),
        "event not in the flow": (
            dict(valid_gift, event="Event9999"), ["Event9999 ActorName", SHOP_CHILD_FLOW, "no event"]),
        "parameter not in the event": (
            dict(valid_gift, param="Actor"), ["Event171 Actor", SHOP_CHILD_FLOW, "no parameter"]),
        "parameter not a string": (
            dict(rupees, text="Item_Boomerang"), ["Event40 Value", rupees["flow"], "not a string"]),
    }
    for name, (entry, named) in texts.items():
        plans[name] = (good | {"event_texts": [valid_gift, entry]}, named)
    values = game.game_data_list(game.read(GAME_DATA_LIST))["Data"]
    assert any(e["Hash"][1] == HASHES[WATER_KEYS] for e in values["Int"])
    assert next(e for e in values["Bool"] if e["Hash"][1] == HASHES[IS_CHILD])["DefaultValue"] == (BOOL, 1)
    values = {
        "value not in the list": ("IsChainLockDoorOpen.1", True, ["not a true-or-false value"]),
        "value not true-or-false in the list": (WATER_KEYS, True, ["not a true-or-false value"]),
        "value already true": (IS_CHILD, True, ["already true"]),
        "value already false": (FIRE_LOCK, False, ["already false"]),
        "number not in the list": (FIRE_LOCK, 1, ["not a whole number"]),
        "number already the default": (TRIALS_LEFT, 6, ["already 6"]),
        "value neither": (FOREST_EXIT_SHUT, "open", ["plan.json", "start_values", "true, false or a whole number"]),
        "number too large": (TRIALS_LEFT, 2**31, ["plan.json", "start_values", "true, false or a whole number"]),
    }
    for name, (value_name, value, reasons) in values.items():
        plans[name] = (good | {"start_values": {WATER_LOCK: True, value_name: value}}, [value_name, *reasons])
    for name, (bad_plan, named) in plans.items():
        with tempfile.TemporaryDirectory() as tmp:
            tmp = pathlib.Path(tmp)
            result = run(patch, game, bad_plan, tmp)
            assert result.returncode != 0, name
            assert all(text in result.stderr for text in named), (name, result.stderr)
            assert not (tmp / "mod").exists(), name

    # A file left by another plan would stay in the mod, so a folder that exists is refused and left alone.
    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        (tmp / "mod").mkdir()
        result = run(patch, game, good, tmp)
        assert result.returncode != 0, "mod folder exists"
        assert str(tmp / "mod") in result.stderr, result.stderr
        assert not any((tmp / "mod").iterdir())


def main():
    test, patch, nup, *rest = sys.argv[1:]
    game = Game(nup)
    tests = {
        "tested-mod": tested_mod,
        "tested-start-mod": tested_start_mod,
        "tested-substitute-mod": tested_substitute_mod,
        "tested-added-item-mod": tested_added_item_mod,
        "tested-hammer-room-mod": tested_hammer_room_mod,
        "tested-gift-mod": tested_gift_mod,
        "tested-event-text-mods": tested_event_text_mods,
        "tested-program-mod": tested_program_mod,
        "tested-bomb-mod": tested_bomb_mod,
        "tested-mido-mod": tested_mido_mod,
        "tested-stone-text-mod": tested_stone_text_mod,
        "seed-plan": seed_plan,
        "larger-plan": larger_plan,
        "bad-input": bad_input,
        "other-version": other_version,
        "size-table": size_table_entries,
        "size-table-unchanged": size_table_unchanged,
        "together": together,
        "start-values": start_values,
        "gift": gift,
        "gift-size-table": gift_size_table,
        "stone-texts": stone_texts,
        "shop": shop,
        "shop-table": shop_table,
        "new-actors": new_actors,
        "tested-new-actor-mod": tested_new_actor_mod,
    }
    tests[test](patch, game, *rest)
    print(f"{test} passed")


if __name__ == "__main__":
    main()
