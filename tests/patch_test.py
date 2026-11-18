"""End-to-end tests of kaepora-patch on the game's own placement files, event flows and game data list.

Usage: python3 -I tests/patch_test.py tested-mod <kaepora-patch> <game.nup> <tested mod's placement file>
       python3 -I tests/patch_test.py tested-start-mod <kaepora-patch> <game.nup> <tested mod's game data list>
       python3 -I tests/patch_test.py tested-hammer-room-mod <kaepora-patch> <game.nup> <tested mod's placement file>
       python3 -I tests/patch_test.py tested-substitute-mod|tested-added-item-mod|tested-gift-mod <kaepora-patch> \
           <game.nup> <tested mod's romfs folder>
       python3 -I tests/patch_test.py tested-event-text-mods <kaepora-patch> <game.nup> <a plan kaepora-seedgen wrote> \
           <tested mod's romfs folder>...
       python3 -I tests/patch_test.py tested-strength-mod <kaepora-patch> <game.nup> <tested mod's folder>
       python3 -I tests/patch_test.py seed-plan <kaepora-patch> <game.nup> <a plan kaepora-seedgen wrote>
       python3 -I tests/patch_test.py larger-plan|bad-input|size-table|size-table-unchanged|together|gift| \
           other-version|start-values <kaepora-patch> <game.nup>
       python3 -I tests/patch_test.py gift-size-table <kaepora-patch> <game.nup>

The game's data is read from the container at test time. These readers are written apart from the patcher's, so a
reader bug in either shows as a difference.
"""

import itertools
import json
import pathlib
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
        dictionaries = zstd.decompress(self.read("Pack/ZsDic.pack.zs"))
        self.dictionary = zstd.ZstdDict(sarc_member(dictionaries, b"bcett.byml.zsdic"))
        self.zs_dictionary = zstd.ZstdDict(sarc_member(dictionaries, b"zs.zsdic"))

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


def sarc_member(data, wanted):
    header_size = struct.unpack_from("<H", data, 4)[0]
    data_offset = struct.unpack_from("<I", data, 0xC)[0]
    count = struct.unpack_from("<H", data, header_size + 6)[0]
    nodes = header_size + 12
    names = nodes + 16 * count + 8
    for i in range(count):
        _, attributes, start, end = struct.unpack_from("<IIII", data, nodes + 16 * i)
        if data[names + 4 * (attributes & 0xFFFF):].split(b"\0", 1)[0] == wanted:
            return data[data_offset + start:data_offset + end]
    raise KeyError(wanted)


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


def plan(chests, start_values=None, event_texts=()):
    return {"seed": "1", "chests": [{"hash": str(h), "files": files, "drop_actor": d} for h, files, d in chests],
            "event_texts": [{"flow": flow, "event": event, "param": param, "text": text}
                            for flow, event, param, text in event_texts],
            "start_values": start_values or {}}


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
        return files(tmp / "mod" / part)


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


def tested_strength_mod(patch, game, mod):
    """The patch to the game's program that the game ran: with it, a weaker strength item left Link's lift level where
    it was."""
    tested = files(pathlib.Path(mod) / "exefs")
    ours = patched(patch, game, plan([]), part="exefs")
    assert ours == tested, (sorted(ours), sorted(tested))


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
    """Chests, gifts and start values in one plan give the files each gives alone. The chests and the gifts each
    raise a size table entry, and the one table holds both."""
    values = {FIRE_LOCK: True, WATER_LOCK: True}
    parts = [plan(rotated(game) + [(LONGSHOT_CHEST, [WATER_STATIC], "Item_DekuStickBundle_Small")]),
             plan([], event_texts=[(*BOTTLE_GIFT, "Item_Boomerang"), (*DINS_FIRE_GIFT, "Item_HeavyBoot")]),
             plan([], values)]
    whole = {key: [entry for part in parts for entry in part[key]] for key in ("chests", "event_texts")}
    whole |= {"seed": "1", "start_values": values}
    *alone, combined = (patched(patch, game, part) for part in parts + [whole])
    chest_table, gift_table = (read_size_table(zstd.decompress(part.pop(SIZE_TABLE))) for part in alone[:2])
    header, entries, names = read_size_table(zstd.decompress(combined.pop(SIZE_TABLE)))
    assert (header, names) == (chest_table[0], chest_table[2])
    assert entries == [(key, max(a, b)) for (key, a), (_, b) in zip(chest_table[1], gift_table[1])]
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


def seed_plan(patch, game, plan_file):
    """A seed's plan, applied: each written file is the game's with every planned chest giving its item, also as its
    substitute where it has one, and as an added value where it names none. Each fits the size table entry the game
    reads."""
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
    mod = patched(patch, game, seed)
    assert sorted(set(mod) - {SIZE_TABLE, GAME_DATA_LIST}) == sorted(changed + list(flows)), sorted(mod)
    for path in changed:
        assert ordered(game.placement(mod[path])) == ordered(expected[path]), path
    for path, flow in flows.items():
        assert game.event_flow(mod[path]) == flow, path
    sizes = placement_sizes(game, mod, changed)
    table = mod[SIZE_TABLE] if SIZE_TABLE in mod else game.read(SIZE_TABLE)
    entries = dict(read_size_table(zstd.decompress(table))[1])
    assert all(load_size(size) <= entries[key] for key, size in sizes.items()), sizes


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
    plans["no event_texts"] = ({"seed": "1", "chests": [valid], "start_values": {}}, ["plan.json", "event_texts"])
    plans["no start_values"] = ({"seed": "1", "chests": [valid], "event_texts": []}, ["plan.json", "start_values"])
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
        "tested-strength-mod": tested_strength_mod,
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
    }
    tests[test](patch, game, *rest)
    print(f"{test} passed")


if __name__ == "__main__":
    main()
