"""End-to-end tests of kaepora-patch on the game's own placement files and game data list.

Usage: python3 -I tests/patch_test.py tested-mod <kaepora-patch> <game.nup> <tested mod's placement file>
       python3 -I tests/patch_test.py tested-start-mod <kaepora-patch> <game.nup> <tested mod's game data list>
       python3 -I tests/patch_test.py larger-plan|bad-input|size-table|size-table-unchanged|start-flags \
           <kaepora-patch> <game.nup>

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
FOREST_DYNAMIC = "Banc/ForestTemple/ForestTemple_Dynamic.bcett.byml.zs"
WATER_STATIC = "Banc/WaterTemple/WaterTemple_Static.bcett.byml.zs"
SIZE_TABLE = "System/Resource/ResourceSizeTable.Product.111.Nin_Ounce_NVN2.rsizetable.zs"
GAME_DATA_LIST = "GameData/GameDataList.Product.110.byml.zs"
SLINGSHOT_CHEST = 821372075822700622
KOKIRI_SWORD_CHEST = 16757992849254467386
LONGSHOT_CHEST = 18059711820131847165
# A Forest Temple chest whose actor has no Dynamic.Drop__DropActor.
FOREST_CHEST_WITHOUT_DROP = 15117164073907033527
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
        self.list_dictionary = zstd.ZstdDict(sarc_member(dictionaries, b"zs.zsdic"))

    def read(self, path):
        size, offset = self.files[path]
        with open(self.nup, "rb") as f:
            f.seek(offset)
            return f.read(size)

    def placement(self, data):
        return byml(zstd.decompress(data, zstd_dict=self.dictionary))

    def game_data_list(self, data):
        return byml(zstd.decompress(data, zstd_dict=self.list_dictionary))


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


def actor(placement, hash):
    return next(a for a in placement["Actors"] if a["Hash"] == (UINT64, hash))


def plan(chests, start_flags=()):
    return {"seed": "1", "chests": [{"hash": str(h), "files": files, "drop_actor": d} for h, files, d in chests],
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
    assert zstd.decompress(ours, zstd_dict=game.list_dictionary) == zstd.decompress(
        tested, zstd_dict=game.list_dictionary)


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


def start_flags_with_chests(patch, game):
    flags = [FIRE_LOCK, WATER_LOCK]
    chests = rotated(game)
    with tempfile.TemporaryDirectory() as without, tempfile.TemporaryDirectory() as tmp:
        without, tmp = pathlib.Path(without), pathlib.Path(tmp)
        assert run(patch, game, plan(chests), without).returncode == 0
        result = run(patch, game, plan(chests, flags), tmp)
        assert result.returncode == 0, result.stderr
        changed = written(without)
        assert written(tmp) == sorted(changed + [GAME_DATA_LIST]), written(tmp)
        # The placement files are the ones the same chests give without flags.
        for path in changed:
            assert (tmp / "mod" / "romfs" / path).read_bytes() == (without / "mod" / "romfs" / path).read_bytes(), path
        ours = game.game_data_list((tmp / "mod" / "romfs" / GAME_DATA_LIST).read_bytes())
    expected = game.game_data_list(game.read(GAME_DATA_LIST))
    entries = [e for e in expected["Data"]["Bool"] if e["Hash"][1] in {HASHES[f] for f in flags}]
    assert [e["DefaultValue"] for e in entries] == [(BOOL, 0)] * len(flags), entries
    for entry in entries:
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


def load_size(size):
    """The size table entry the game gives each of its placement files, from the file's decompressed size."""
    return (size + 31) // 32 * 32 + 264


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
        "no Drop__DropActor": (
            {"hash": str(FOREST_CHEST_WITHOUT_DROP), "files": [FOREST_DYNAMIC], "drop_actor": "Item_Hookshot"},
            [str(FOREST_CHEST_WITHOUT_DROP), FOREST_DYNAMIC]),
    }
    plans = {name: ({"seed": "1", "chests": [valid, chest], "start_flags": []}, named)
             for name, (chest, named) in cases.items()}
    plans["no start_flags"] = ({"seed": "1", "chests": [valid]}, ["plan.json", "start_flags"])
    values = game.game_data_list(game.read(GAME_DATA_LIST))["Data"]
    assert any(e["Hash"][1] == HASHES[WATER_KEYS] for e in values["Int"])
    assert next(e for e in values["Bool"] if e["Hash"][1] == HASHES[IS_CHILD])["DefaultValue"] == (BOOL, 1)
    flags = {
        "flag not in the list": ("IsChainLockDoorOpen.1", "not a true-or-false value"),
        "flag not true-or-false": (WATER_KEYS, "not a true-or-false value"),
        "flag already true": (IS_CHILD, "already true"),
    }
    for name, (flag, reason) in flags.items():
        plans[name] = ({"seed": "1", "chests": [valid], "start_flags": [FIRE_LOCK, flag]}, [flag, reason])
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
        result = run(patch, game, {"seed": "1", "chests": [valid], "start_flags": []}, tmp)
        assert result.returncode != 0, "mod folder exists"
        assert str(tmp / "mod") in result.stderr, result.stderr
        assert not any((tmp / "mod").iterdir())


def main():
    test, patch, nup, *rest = sys.argv[1:]
    game = Game(nup)
    tests = {
        "tested-mod": tested_mod,
        "tested-start-mod": tested_start_mod,
        "larger-plan": larger_plan,
        "bad-input": bad_input,
        "size-table": size_table_entries,
        "size-table-unchanged": size_table_unchanged,
        "start-flags": start_flags_with_chests,
    }
    tests[test](patch, game, *rest)
    print(f"{test} passed")


if __name__ == "__main__":
    main()
