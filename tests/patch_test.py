"""End-to-end tests of kaepora-patch on the game's own placement files, event flows and game data list.

Usage: python3 -I tests/patch_test.py tested-mod <kaepora-patch> <game.nup> <tested mod's placement file>
       python3 -I tests/patch_test.py tested-start-mod <kaepora-patch> <game.nup> <tested mod's game data list>
       python3 -I tests/patch_test.py tested-substitute-mod|tested-added-item-mod|tested-gift-mod <kaepora-patch> \
           <game.nup> <tested mod's romfs folder>
       python3 -I tests/patch_test.py seed-plan <kaepora-patch> <game.nup> <a plan kaepora-seedgen wrote>
       python3 -I tests/patch_test.py larger-plan|bad-input|size-table|size-table-unchanged|together|gift \
           <kaepora-patch> <game.nup>
       python3 -I tests/patch_test.py gift-size-table <kaepora-patch> <game.nup>

The game's data is read from the container at test time. These readers are written apart from the patcher's, so a
reader bug in either shows as a difference.
"""

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
BOOL = 0xD0
INT64 = 0xD4
UINT64 = 0xD5

# Saved values by name, and the MurmurHash3 x86_32 hash of each name that keys it in the game data list.
FIRE_LOCK = "IsChainLockDoorOpen.4189300454150217955"
WATER_LOCK = "IsChainLockDoorOpen.2202807660383340401"
MIDO_CHEST = "IsTreasureBoxOpen.12786193094758345086"
WATER_KEYS = "DungeonInfo.WaterTemple.SmallKeyTotalNum"
IS_CHILD = "Sequence_IsChildhood"
HASHES = {FIRE_LOCK: 749909151, WATER_LOCK: 400117133, WATER_KEYS: 3683958665, IS_CHILD: 3466731731}


class Game:
    """The update package's RomFS, which holds every placement file, the game data list, the dictionary pack and the
    size table."""

    def __init__(self, nup):
        self.nup = nup
        with open(nup, "rb") as f:
            toc_offset, toc_size = struct.unpack_from("<QQ", f.read(0x18), 8)
            f.seek(toc_offset)
            toc = f.read(toc_size)
        _, package_count, dir_count, file_count, pool = struct.unpack_from("<IIQQQ", toc)
        dirs = 0x30 + 0x40 * package_count
        files = dirs + 0x18 * dir_count
        children = files + 0x50 * file_count

        def name(record):
            offset, length = struct.unpack_from("<II", toc, record)
            return toc[pool + offset:pool + offset + length].decode()

        self.files = {}

        def walk(d, prefix):
            first, count = struct.unpack_from("<II", toc, dirs + 0x18 * d + 8)
            for child in struct.unpack_from(f"<{count}I", toc, children + 4 * first):
                if child < dir_count:
                    walk(child, prefix + name(dirs + 0x18 * child) + "/")
                else:
                    record = files + 0x50 * (child - dir_count)
                    self.files[prefix + name(record)] = struct.unpack_from("<QQ", toc, record + 8)

        update = next(0x30 + 0x40 * i for i in range(package_count) if toc[0x30 + 0x40 * i + 0x18] == 1)
        walk(struct.unpack_from("<I", toc, update + 0x10)[0], "")
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


def gift_parameter(flow, event, param):
    _, _, (names, values), *_ = next(e for e in flow["events"] if e[0] == event)
    return values[[name for _, name in names[1:]].index(param)]


def actor(placement, hash):
    return next(a for a in placement["Actors"] if a["Hash"] == (UINT64, hash))


def plan(chests, start_flags=(), gifts=()):
    return {"seed": "1", "chests": [{"hash": str(h), "files": files, "drop_actor": d} for h, files, d in chests],
            "gifts": [{"flow": flow, "event": event, "param": param, "item": item}
                      for flow, event, param, item in gifts],
            "start_flags": list(start_flags)}


def run(patch, game, plan, tmp):
    plan_path = tmp / "plan.json"
    plan_path.write_text(json.dumps(plan))
    return subprocess.run([patch, game.nup, plan_path, tmp / "mod"], capture_output=True, text=True)


def written(tmp):
    romfs = tmp / "mod" / "romfs"
    return sorted(p.relative_to(romfs).as_posix() for p in romfs.rglob("*") if p.is_file())


def tested_mod(patch, game, mod_file):
    """The one-chest change the game accepted: the Deku Tree Slingshot chest gives the Hookshot."""
    tested = pathlib.Path(mod_file).read_bytes()
    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        result = run(patch, game, plan([(SLINGSHOT_CHEST, [DEKU_STATIC], "Item_Hookshot")]), tmp)
        assert result.returncode == 0, result.stderr
        assert written(tmp) == [DEKU_STATIC], written(tmp)
        ours = (tmp / "mod" / "romfs" / DEKU_STATIC).read_bytes()
    # The frame header descriptor and dictionary ID match: content size in the header, one segment, no checksum.
    assert ours[4] == tested[4], (ours[4], tested[4])
    assert zstd.get_frame_info(ours).dictionary_id == zstd.get_frame_info(tested).dictionary_id
    # The tested mod wrote Item_Hookshot over Item_Range_SlingShot in the string table and padded it with zero bytes.
    # The game's own files never pad a string, so the bytes differ and the parsed content must match.
    assert game.placement(ours) == game.placement(tested)


def tested_start_mod(patch, game, mod_file):
    """The start state the game accepted: two lock records and one Mido's House chest record start set."""
    tested = pathlib.Path(mod_file).read_bytes()
    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        result = run(patch, game, plan([], [FIRE_LOCK, WATER_LOCK, MIDO_CHEST]), tmp)
        assert result.returncode == 0, result.stderr
        assert written(tmp) == [GAME_DATA_LIST], written(tmp)
        ours = (tmp / "mod" / "romfs" / GAME_DATA_LIST).read_bytes()
    game_frame = game.read(GAME_DATA_LIST)
    # Single segment, content size in the header, no checksum, and the same dictionary, as in the game's own list.
    assert ours[4] == game_frame[4], (ours[4], game_frame[4])
    info, game_info = zstd.get_frame_info(ours), zstd.get_frame_info(game_frame)
    assert (info.decompressed_size, info.dictionary_id) == (game_info.decompressed_size, game_info.dictionary_id)
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
    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        result = run(patch, game, plan([(BRIDGE_CHEST, [DODONGO_DYNAMIC], "Item_Boomerang")]), tmp)
        assert result.returncode == 0, result.stderr
        assert DODONGO_DYNAMIC in written(tmp), written(tmp)
        ours = game.placement((tmp / "mod" / "romfs" / DODONGO_DYNAMIC).read_bytes())
    assert ordered(ours) == ordered(tested)


def tested_gift_mod(patch, game, romfs):
    """The gift the game accepted: with the Boomerang in the call that gives the Market potion shop child's bottle, she
    gave the Boomerang."""
    tested = (pathlib.Path(romfs) / SHOP_CHILD_FLOW).read_bytes()
    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        result = run(patch, game, plan([], gifts=[(*BOTTLE_GIFT, "Item_Boomerang")]), tmp)
        assert result.returncode == 0, result.stderr
        assert written(tmp) == [SHOP_CHILD_FLOW], written(tmp)
        ours = (tmp / "mod" / "romfs" / SHOP_CHILD_FLOW).read_bytes()
    # Single segment, content size in the header, no checksum, and the same dictionary, as in the tested file.
    assert ours[4] == tested[4], (ours[4], tested[4])
    assert zstd.get_frame_info(ours).dictionary_id == zstd.get_frame_info(tested).dictionary_id
    assert zstd.decompress(ours, zstd_dict=game.zs_dictionary) == zstd.decompress(tested, zstd_dict=game.zs_dictionary)


def tested_added_item_mod(patch, game, romfs):
    """The chests that name no item, as the game accepted them: with a Drop__DropActor added, the Forest Temple falling
    ceiling chest and the Training Ground lobby left chest each gave the Boomerang. Both files outgrew their size table
    entries."""
    romfs = pathlib.Path(romfs)
    chests = [(FOREST_CHEST_WITHOUT_DROP, FOREST_DYNAMIC), (LOBBY_CHEST_WITHOUT_DROP, TRAINING_GROUND_DYNAMIC)]
    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        result = run(patch, game, plan([(h, [path], "Item_Boomerang") for h, path in chests]), tmp)
        assert result.returncode == 0, result.stderr
        assert written(tmp) == sorted([FOREST_DYNAMIC, TRAINING_GROUND_DYNAMIC, SIZE_TABLE]), written(tmp)
        ours = {path: (tmp / "mod" / "romfs" / path).read_bytes() for path in [p for _, p in chests] + [SIZE_TABLE]}
    # The tested files were written with the patcher's BYML writer, so the decompressed files match byte for byte.
    for _, path in chests:
        assert zstd.decompress(ours[path], zstd_dict=game.dictionary) == zstd.decompress(
            (romfs / path).read_bytes(), zstd_dict=game.dictionary), path
    ours_sizes = dict(read_size_table(zstd.decompress(ours[SIZE_TABLE]))[1])
    tested_sizes = dict(read_size_table(zstd.decompress((romfs / SIZE_TABLE).read_bytes()))[1])
    for _, path in chests:
        assert ours_sizes[size_key(path)] == tested_sizes[size_key(path)], path


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
    """The ROTATED chests, each given the next one's item."""
    items = [actor(game.placement(game.read(files[0])), h)["Dynamic"]["Drop__DropActor"] for h, files in ROTATED]
    assert len(set(items)) == len(items), items
    return [(h, files, items[(i + 1) % len(items)]) for i, (h, files) in enumerate(ROTATED)]


def larger_plan(patch, game):
    originals = {path: game.placement(game.read(path)) for _, files in ROTATED + [KEPT] for path in files}
    chests = rotated(game)
    kept_hash, kept_files = KEPT
    chests.append((kept_hash, kept_files, actor(originals[kept_files[0]], kept_hash)["Dynamic"]["Drop__DropActor"]))

    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        result = run(patch, game, plan(chests), tmp)
        assert result.returncode == 0, result.stderr
        changed = sorted({path for _, files in ROTATED for path in files})
        assert written(tmp) == changed, written(tmp)
        for path in changed:
            expected = originals[path]
            for h, files, drop_actor in chests:
                if path in files:
                    actor(expected, h)["Dynamic"]["Drop__DropActor"] = drop_actor
            assert game.placement((tmp / "mod" / "romfs" / path).read_bytes()) == expected, path


def together(patch, game):
    """Chests, gifts and start flags in one plan give the files each gives alone. The chests and the gifts each raise
    a size table entry, and the one table holds both."""
    flags = [FIRE_LOCK, WATER_LOCK]
    parts = [plan(rotated(game) + [(LONGSHOT_CHEST, [WATER_STATIC], "Item_DekuStickBundle_Small")]),
             plan([], gifts=[(*BOTTLE_GIFT, "Item_Boomerang"), (*DINS_FIRE_GIFT, "Item_HeavyBoot")]),
             plan([], flags)]
    whole = {key: [entry for part in parts for entry in part[key]] for key in ("chests", "gifts", "start_flags")}
    whole["seed"] = "1"
    files = []
    with tempfile.TemporaryDirectory() as tmp:
        for i, part in enumerate(parts + [whole]):
            folder = pathlib.Path(tmp) / str(i)
            folder.mkdir()
            result = run(patch, game, part, folder)
            assert result.returncode == 0, result.stderr
            files.append({path: (folder / "mod" / "romfs" / path).read_bytes() for path in written(folder)})
    *alone, combined = files
    chest_table, gift_table = (read_size_table(zstd.decompress(part.pop(SIZE_TABLE))) for part in alone[:2])
    header, entries, names = read_size_table(zstd.decompress(combined.pop(SIZE_TABLE)))
    assert (header, names) == (chest_table[0], chest_table[2])
    assert entries == [(key, max(a, b)) for (key, a), (_, b) in zip(chest_table[1], gift_table[1])]
    assert combined == {path: data for part in alone for path, data in part.items()}, sorted(combined)

    ours = game.game_data_list(combined[GAME_DATA_LIST])
    expected = game.game_data_list(game.read(GAME_DATA_LIST))
    flagged = [e for e in expected["Data"]["Bool"] if e["Hash"][1] in {HASHES[f] for f in flags}]
    assert [e["DefaultValue"] for e in flagged] == [(BOOL, 0)] * len(flags), flagged
    for entry in flagged:
        entry["DefaultValue"] = (BOOL, 1)
    assert ours == expected


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


def placement_sizes(game, tmp, paths):
    """The decompressed size of each placement file in the mod, by its size table key."""
    romfs = tmp / "mod" / "romfs"
    return {size_key(p): len(zstd.decompress((romfs / p).read_bytes(), zstd_dict=game.dictionary)) for p in paths}


def size_table_entries(patch, game):
    # The rotated chests keep their files' sizes rounded up to 32 bytes. The new item grows the Water Temple file past
    # what its entry lets the game load.
    chests = rotated(game) + [(LONGSHOT_CHEST, [WATER_STATIC], "Item_DekuStickBundle_Small")]
    game_frame = game.read(SIZE_TABLE)
    header, entries, names = read_size_table(zstd.decompress(game_frame))
    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        result = run(patch, game, plan(chests), tmp)
        assert result.returncode == 0, result.stderr
        changed = sorted({path for _, files, _ in chests for path in files})
        assert written(tmp) == changed + [SIZE_TABLE], written(tmp)
        sizes = placement_sizes(game, tmp, changed)
        frame = (tmp / "mod" / "romfs" / SIZE_TABLE).read_bytes()
    game_sizes = dict(entries)
    assert [p for p in changed if load_size(sizes[size_key(p)]) > game_sizes[size_key(p)]] == [WATER_STATIC]
    # Each written file's entry is large enough for its size, and every other byte of the table is the game's.
    expected = [(key, max(size, load_size(sizes[key])) if key in sizes else size) for key, size in entries]
    assert read_size_table(zstd.decompress(frame)) == (header, expected, names)
    # Single segment, content size in the header, no checksum, and no dictionary, as in the game's own table.
    assert frame[4] == game_frame[4], (frame[4], game_frame[4])
    assert zstd.get_frame_info(frame).dictionary_id == zstd.get_frame_info(game_frame).dictionary_id


def size_table_unchanged(patch, game):
    _, entries, _ = read_size_table(zstd.decompress(game.read(SIZE_TABLE)))
    game_sizes = dict(entries)
    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        result = run(patch, game, plan(rotated(game)), tmp)
        assert result.returncode == 0, result.stderr
        changed = sorted({path for _, files in ROTATED for path in files})
        sizes = placement_sizes(game, tmp, changed)
        # Every written file still fits its entry, so the table is left out.
        assert all(load_size(size) <= game_sizes[key] for key, size in sizes.items()), sizes
        assert written(tmp) == changed, written(tmp)


def gift(patch, game):
    """A gift written into its flow: read apart from the patcher, the flow is the game's with the one parameter
    changed. The Boomerang's name keeps the flow inside its size table entry, so the table is left out."""
    _, entries, _ = read_size_table(zstd.decompress(game.read(SIZE_TABLE)))
    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        result = run(patch, game, plan([], gifts=[(*BOTTLE_GIFT, "Item_Boomerang")]), tmp)
        assert result.returncode == 0, result.stderr
        assert written(tmp) == [SHOP_CHILD_FLOW], written(tmp)
        data = zstd.decompress((tmp / "mod" / "romfs" / SHOP_CHILD_FLOW).read_bytes(), zstd_dict=game.zs_dictionary)
    assert load_size(len(data), EVENT_FLOW_OVERHEAD) <= dict(entries)[size_key(SHOP_CHILD_FLOW)]
    expected = game.event_flow(game.read(SHOP_CHILD_FLOW))
    parameter = gift_parameter(expected, *BOTTLE_GIFT[1:])
    assert parameter == [5, "Item_Bottle"], parameter
    parameter[1] = "Item_Boomerang"
    assert event_flow(data) == expected

    # The item the flow already gives changes nothing, so nothing is written.
    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        result = run(patch, game, plan([], gifts=[(*BOTTLE_GIFT, "Item_Bottle")]), tmp)
        assert result.returncode == 0, result.stderr
        assert written(tmp) == [], written(tmp)

    # A shorter name leaves the flow no larger, so the size table is left out, and the flow reads as the game's with
    # that name. The same holds for a name whose text ends on an 8-byte boundary.
    for site, old, new in ((DINS_FIRE_GIFT, "Item_DinFire", "Item_Hammer"),
                           (NAYRUS_LOVE_GIFT, "Item_NayruLove", "Item_Hammer")):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = pathlib.Path(tmp)
            result = run(patch, game, plan([], gifts=[(*site, new)]), tmp)
            assert result.returncode == 0, result.stderr
            assert written(tmp) == [site[0]], written(tmp)
            data = zstd.decompress((tmp / "mod" / "romfs" / site[0]).read_bytes(), zstd_dict=game.zs_dictionary)
        expected = game.event_flow(game.read(site[0]))
        parameter = gift_parameter(expected, *site[1:])
        assert parameter == [5, old], parameter
        parameter[1] = new
        assert event_flow(data) == expected

    # Two gifts in one flow, in either order of the plan: both are written.
    gifts = [(*COJIRO_GIFT, "Item_Boomerang"), (*POCKET_EGG_GIFT, "Item_Gauntlet_Silver")]
    expected = game.event_flow(game.read(ANJU_FLOW))
    for site, old, new in ((COJIRO_GIFT, "Item_CojiroCucco", "Item_Boomerang"),
                           (POCKET_EGG_GIFT, "Item_PocketEgg", "Item_Gauntlet_Silver")):
        parameter = gift_parameter(expected, *site[1:])
        assert parameter == [5, old], parameter
        parameter[1] = new
    for order in (gifts, gifts[::-1]):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = pathlib.Path(tmp)
            result = run(patch, game, plan([], gifts=order), tmp)
            assert result.returncode == 0, result.stderr
            assert ANJU_FLOW in written(tmp), written(tmp)
            data = zstd.decompress((tmp / "mod" / "romfs" / ANJU_FLOW).read_bytes(), zstd_dict=game.zs_dictionary)
        assert event_flow(data) == expected


def gift_size_table(patch, game):
    """A gift that grows its flow past its size table entry: the entry is raised to what the flow needs, every other
    byte of the table is the game's, and the flow, read apart from the patcher, is the game's with the one parameter
    changed."""
    path = DINS_FIRE_GIFT[0]
    header, entries, names = read_size_table(zstd.decompress(game.read(SIZE_TABLE)))
    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        result = run(patch, game, plan([], gifts=[(*DINS_FIRE_GIFT, "Item_HeavyBoot")]), tmp)
        assert result.returncode == 0, result.stderr
        assert written(tmp) == sorted([path, SIZE_TABLE]), written(tmp)
        data = zstd.decompress((tmp / "mod" / "romfs" / path).read_bytes(), zstd_dict=game.zs_dictionary)
        table = zstd.decompress((tmp / "mod" / "romfs" / SIZE_TABLE).read_bytes())
    needed = load_size(len(data), EVENT_FLOW_OVERHEAD)
    assert needed > dict(entries)[size_key(path)], needed
    assert read_size_table(table) == (header, [(k, needed if k == size_key(path) else v) for k, v in entries], names)
    expected = game.event_flow(game.read(path))
    parameter = gift_parameter(expected, *DINS_FIRE_GIFT[1:])
    assert parameter == [5, "Item_DinFire"], parameter
    parameter[1] = "Item_HeavyBoot"
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
            if "Drop__DropActor" not in dynamic:
                kinds.add("no item")
            if "Drop__DropActor_Substitute" in dynamic:
                kinds.add("substitute")
                dynamic["Drop__DropActor_Substitute"] = chest["drop_actor"]
            dynamic["Drop__DropActor"] = chest["drop_actor"]
            # The game's dicts are in key order, and an added value takes its place in it.
            entries = sorted(dynamic.items())
            dynamic.clear()
            dynamic.update(entries)
    assert kinds == {"no item", "substitute"}, kinds
    changed = sorted(path for path, placement in expected.items()
                     if ordered(placement) != ordered(game.placement(game.read(path))))
    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        result = run(patch, game, seed, tmp)
        assert result.returncode == 0, result.stderr
        assert sorted(set(written(tmp)) - {SIZE_TABLE, GAME_DATA_LIST}) == changed, written(tmp)
        for path in changed:
            ours = game.placement((tmp / "mod" / "romfs" / path).read_bytes())
            assert ordered(ours) == ordered(expected[path]), path
        sizes = placement_sizes(game, tmp, changed)
        mod_table = tmp / "mod" / "romfs" / SIZE_TABLE
        table = mod_table.read_bytes() if mod_table.exists() else game.read(SIZE_TABLE)
    entries = dict(read_size_table(zstd.decompress(table))[1])
    assert all(load_size(size) <= entries[key] for key, size in sizes.items()), sizes


def bad_input(patch, game):
    # Each plan starts with a valid change, so a program that wrote as it went would leave a file behind.
    valid = {"hash": str(SLINGSHOT_CHEST), "files": [DEKU_STATIC], "drop_actor": "Item_Hookshot"}
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
    plans = {name: ({"seed": "1", "chests": [valid, chest], "gifts": [], "start_flags": []}, named)
             for name, (chest, named) in cases.items()}
    plans["no gifts"] = ({"seed": "1", "chests": [valid], "start_flags": []}, ["plan.json", "gifts"])
    plans["no start_flags"] = ({"seed": "1", "chests": [valid], "gifts": []}, ["plan.json", "start_flags"])
    valid_gift = {"flow": SHOP_CHILD_FLOW, "event": "Event171", "param": "ActorName", "item": "Item_Boomerang"}
    nowhere = "Event/EventFlow/Nowhere.bfevfl.zs"
    timeline = "Event/EventFlow/Dm_Awake_Fire.bfevfl.zs"
    # Dm_LonLon_0030's Event40 gives 20 rupees, a number.
    rupees = {"flow": "Event/EventFlow/Dm_LonLon_0030.bfevfl.zs", "event": "Event40", "param": "Value"}
    gifts = {
        "gift not this shape": (
            {key: valid_gift[key] for key in ("flow", "event", "param")}, ["plan.json", "gifts[1]"]),
        "gift without an item": (
            dict(zip(("flow", "event", "param"), DINS_FIRE_GIFT), item=""), ["plan.json", "gifts[1]", "item"]),
        "gift listed twice": (
            dict(valid_gift, item="Item_Bottle"),
            ["plan.json", "gifts[1]", "Event171 ActorName", SHOP_CHILD_FLOW, "twice"]),
        "flow not in the game": (dict(valid_gift, flow=nowhere), ["Event171 ActorName", nowhere, "no such file"]),
        "flow a timeline": (dict(valid_gift, flow=timeline), ["Event171 ActorName", timeline, "timeline"]),
        "event not in the flow": (
            dict(valid_gift, event="Event9999"), ["Event9999 ActorName", SHOP_CHILD_FLOW, "no event"]),
        "parameter not in the event": (
            dict(valid_gift, param="Actor"), ["Event171 Actor", SHOP_CHILD_FLOW, "no parameter"]),
        "parameter not a string": (
            dict(rupees, item="Item_Boomerang"), ["Event40 Value", rupees["flow"], "not a string"]),
    }
    for name, (entry, named) in gifts.items():
        plans[name] = ({"seed": "1", "chests": [valid], "gifts": [valid_gift, entry], "start_flags": []}, named)
    values = game.game_data_list(game.read(GAME_DATA_LIST))["Data"]
    assert any(e["Hash"][1] == HASHES[WATER_KEYS] for e in values["Int"])
    assert next(e for e in values["Bool"] if e["Hash"][1] == HASHES[IS_CHILD])["DefaultValue"] == (BOOL, 1)
    flags = {
        "flag not in the list": ("IsChainLockDoorOpen.1", "not a true-or-false value"),
        "flag not true-or-false": (WATER_KEYS, "not a true-or-false value"),
        "flag already true": (IS_CHILD, "already true"),
    }
    for name, (flag, reason) in flags.items():
        plans[name] = ({"seed": "1", "chests": [valid], "gifts": [], "start_flags": [FIRE_LOCK, flag]}, [flag, reason])
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
        result = run(patch, game, {"seed": "1", "chests": [valid], "gifts": [], "start_flags": []}, tmp)
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
        "tested-gift-mod": tested_gift_mod,
        "seed-plan": seed_plan,
        "larger-plan": larger_plan,
        "bad-input": bad_input,
        "size-table": size_table_entries,
        "size-table-unchanged": size_table_unchanged,
        "together": together,
        "gift": gift,
        "gift-size-table": gift_size_table,
    }
    tests[test](patch, game, *rest)
    print(f"{test} passed")


if __name__ == "__main__":
    main()
