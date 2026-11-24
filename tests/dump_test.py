"""End-to-end tests of kaepora-dump on the game's own files.

Usage: python3 -I -B tests/dump_test.py <kaepora-dump> <kaepora-patch> <game.nup> <a plan kaepora-seedgen wrote> \
           [test name ...]

What each file should print as comes from tests/patch_test.py's readers, written apart from the patcher's.
"""

import base64
import hashlib
import json
import pathlib
import re
import struct
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import patch_test  # noqa: E402
from compression import zstd  # noqa: E402

DUMP = PATCH = GAME = PLAN = None
GAME_DATA_LIST = "GameData/GameDataList.Product.110.byml.zs"
START_POS = "Banc/BancBasedResource/StartPos.byml.zs"
ITEM_TEXTS = ("Mals/USen.Product.110.sarc.zs", "ActorMsg/PouchContent.msbt")
NAME = re.compile(rb"[A-Za-z0-9_]{3,}")
# check-mod's exit status when it names a file too large. An error exits 1.
TOO_LARGE = 3
NO_COPY = "the game has no copy of it to measure it by"
NO_ENTRY = "the size table the game reads has no entry for it"
UNMEASURED = ("the game's own size table gives files of its kind no one overhead, "
              "so what a larger one needs is not known")
ACTOR_TABLE_KIND = ".Product.111.rstbl.byml.zs"
DARUNIA_SON_SCHEDULE = ("Component/AIScheduleParam/"
                        "Npc_Goron_DaruniaJr_Adult.game__component__AIScheduleParam.bgyml")


def dump(*args):
    result = run(*args)
    assert result.returncode == 0, result.stderr
    return [json.loads(line) for line in result.stdout.splitlines()]


def run(command, *args):
    return subprocess.run([DUMP, command, GAME.nup, *map(str, args)], capture_output=True, encoding="utf-8")


def content(path):
    data = GAME.read(path)
    return patch_test.decompressed(GAME, data) if path.endswith(".zs") else data


def member(pack, name):
    return patch_test.sarc(content(pack))[name]


def as_printed(node):
    if isinstance(node, dict):
        return {key: as_printed(value) for key, value in node.items()}
    if isinstance(node, list):
        return [as_printed(value) for value in node]
    if isinstance(node, str):
        return node
    kind, value = node
    if kind == patch_test.BINARY:
        return {"binary": base64.b64encode(value).decode()}
    if kind == patch_test.BOOL:
        return value != 0
    if kind == patch_test.INT:
        return value - (1 << 32) if value >= 1 << 31 else value
    if kind == patch_test.UINT:
        return value
    if kind == patch_test.FLOAT:
        return struct.unpack("<f", struct.pack("<I", value))[0]
    if kind == patch_test.INT64:
        return str(value - (1 << 64) if value >= 1 << 63 else value)
    assert kind == patch_test.UINT64, kind
    return str(value)


def tagged(text):
    parts, at = [], 0
    while at < len(text):
        if text[at] != 0x0E:
            end = text.find(b"\x0e", at)
            end = len(text) if end < 0 else end
            parts.append(text[at:end].decode())
            at = end
            continue
        group, kind, size = struct.unpack_from("<HHH", text, at + 1)
        params = text[at + 7:at + 7 + size]
        at += 7 + size
        if (group, kind, params) == (0, 4, b""):
            parts.append("{page}")
        elif group == 0 and kind in (2, 3) and size == 2:
            parts.append(f"{{{'size' if kind == 2 else 'color'}:{struct.unpack('<H', params)[0]}}}")
        else:
            parts.append(f"{{{group}:{kind}:{params.hex()}}}")
    return "".join(parts)


class DumpTest(unittest.TestCase):
    def test_the_tables_and_placements_print_as_their_trees(self):
        """The tag table among the update's tables holds binary nodes, and the start positions have no string
        table."""
        paths = sorted(p for p in GAME.files if p.startswith("RSDB/") or p.endswith(".bcett.byml.zs"))
        paths += [GAME_DATA_LIST, START_POS]
        printed = dump("files", *paths)
        self.assertEqual([line["path"] for line in printed], paths)
        for line in printed:
            path = line["path"]
            self.assertEqual(line["sha256"], hashlib.sha256(GAME.read(path)).hexdigest(), path)
            self.assertEqual(line["root"], as_printed(patch_test.byml(content(path))), path)

    def test_a_message_file_in_a_text_pack_prints_as_its_labels_and_tagged_texts(self):
        pack, name = ITEM_TEXTS
        [line] = dump("files", f"{pack}/{name}")
        _, texts = patch_test.msbt(member(pack, name))
        self.assertEqual(line["sha256"], hashlib.sha256(GAME.read(pack)).hexdigest())
        self.assertEqual(line["root"], [{"label": label, "text": tagged(text)} for label, text in texts.items()])

    def test_the_key_names_the_table_of_contents_the_program_and_the_format(self):
        with open(GAME.nup, "rb") as f:
            toc_offset, toc_size = struct.unpack_from("<QQ", f.read(0x18), 8)
            f.seek(toc_offset)
            toc = f.read(toc_size)
            f.seek(GAME.program_offsets[patch_test.UPDATE_PACKAGE] + 0x40)
            build = f.read(0x20).rstrip(b"\0")
        self.assertEqual(dump("key"), [{"key": hashlib.sha256(toc).hexdigest(), "build": build.hex().upper(),
                                        "format": 1}])

    def test_the_list_names_each_file_under_a_prefix(self):
        result = run("list", "RSDB/")
        self.assertEqual(result.returncode, 0, result.stderr)
        listed = result.stdout.splitlines()
        self.assertTrue(all(path.startswith("RSDB/") for path in listed), listed)
        self.assertLessEqual({p for p in GAME.files if p.startswith("RSDB/")}, set(listed))

    def test_the_list_names_the_members_of_a_pack_as_files_takes_them(self):
        pack = ITEM_TEXTS[0]
        result = run("list", f"{pack}/")
        self.assertEqual(result.returncode, 0, result.stderr)
        members = [f"{pack}/{name}" for name in sorted(patch_test.sarc(content(pack)))]
        self.assertEqual(result.stdout.splitlines(), members)

    def test_the_strings_of_a_file_are_its_runs_of_name_characters(self):
        pack = patch_test.BOMB_LIFT_PACKS[0]
        bgyml = next(name for name in sorted(patch_test.sarc(content(pack))) if name.endswith(".bgyml"))
        files = {patch_test.PEDESTAL_MODULE: content(patch_test.PEDESTAL_MODULE),
                 patch_test.STONE_FLOW: content(patch_test.STONE_FLOW), f"{pack}/{bgyml}": member(pack, bgyml)}
        printed = dump("strings", *files)
        self.assertEqual(printed, [{"path": path, "strings": sorted({name.decode() for name in NAME.findall(data)})}
                                   for path, data in files.items()])

    def test_the_whole_games_strings_hold_those_of_each_format_that_names_saved_values(self):
        """A logic module, an event flow, a placement, the game data list, a message file and a pack's members."""
        pack = patch_test.BOMB_LIFT_PACKS[0]
        members = [f"{pack}/{name}" for name in sorted(patch_test.sarc(content(pack)))
                   if name.endswith((".bgyml", ".ainb"))]
        result = run("strings")
        self.assertEqual(result.returncode, 0, result.stderr)
        every = set(result.stdout.splitlines())
        for line in dump("strings", patch_test.PEDESTAL_MODULE, patch_test.STONE_FLOW, patch_test.DEKU_STATIC,
                         GAME_DATA_LIST, "/".join(ITEM_TEXTS), *members):
            self.assertLessEqual(set(line["strings"]), every, line["path"])

    def check_mod(self, files, table=None):
        with tempfile.TemporaryDirectory() as tmp:
            romfs = pathlib.Path(tmp) / "romfs"
            if table is not None:
                files = {**files, patch_test.SIZE_TABLE: zstd.compress(table)}
            for path, data in files.items():
                (romfs / path).parent.mkdir(parents=True, exist_ok=True)
                (romfs / path).write_bytes(data)
            result = run("check-mod", tmp)
            return result.returncode, [json.loads(line) for line in result.stdout.splitlines()], result.stderr

    def placement_of(self, size):
        return zstd.compress(bytes(size), zstd_dict=GAME.dictionary)

    def games_table(self, changed):
        """The game's size table with each entry the mapping names at its new size, or left out where it maps to
        None."""
        header, entries, names = patch_test.read_size_table(content(patch_test.SIZE_TABLE))
        sizes = {patch_test.size_key(path): size for path, size in changed.items()}
        kept = [(crc, sizes.get(crc, size)) for crc, size in entries if sizes.get(crc, size) is not None]
        header = bytearray(header)
        struct.pack_into("<I", header, 14, len(kept))
        return bytes(header) + b"".join(struct.pack("<II", *entry) for entry in kept) + names

    def test_a_mod_with_no_romfs_folder_fits(self):
        """A mod of only a program patch, as the tested strength mod is."""
        with tempfile.TemporaryDirectory() as tmp:
            (pathlib.Path(tmp) / "exefs").mkdir()
            (pathlib.Path(tmp) / "exefs" / "patch.ips").write_bytes(b"PATCHEOF")
            result = run("check-mod", tmp)
        self.assertEqual((result.returncode, result.stdout, result.stderr), (0, "", ""))

    def test_an_error_exits_apart_from_a_file_too_large(self):
        """A size table that is not one, a mod folder that is not there, and a placement past its entry."""
        status, lines, error = self.check_mod({}, b"not a size table")
        self.assertEqual((status, lines), (1, []))
        self.assertIn("size table", error)
        with tempfile.TemporaryDirectory() as tmp:
            result = run("check-mod", pathlib.Path(tmp) / "missing")
        self.assertEqual((result.returncode, result.stdout), (1, ""))
        too_large = self.placement_of(patch_test.load_size(len(content(patch_test.DEKU_STATIC))))
        self.assertNotEqual(self.check_mod({patch_test.DEKU_STATIC: too_large})[0], status)

    def test_a_mods_own_table_that_lowers_the_entry_of_a_file_it_lacks_names_that_file(self):
        """Seen in the game: a mod of only size tables, which lowered the entries of two placement files, had both
        dropped."""
        entry = patch_test.load_size(len(content(patch_test.DEKU_STATIC)))
        table = self.games_table({patch_test.DEKU_STATIC: entry - 32})
        self.assertEqual(self.check_mod({}, table),
                         (TOO_LARGE, [{"path": patch_test.DEKU_STATIC, "needs": entry, "entry": entry - 32}], ""))

    def test_a_file_check_mod_cannot_measure_is_named_unchecked(self):
        """A file the game lacks, and one whose entry the mod's own table leaves out."""
        new = "Pack/Actor/Kaepora_Test.pack.zs"
        files = {new: zstd.compress(b"made up"), patch_test.DEKU_STATIC: self.placement_of(100)}
        self.assertEqual(self.check_mod(files, self.games_table({patch_test.DEKU_STATIC: None})),
                         (0, [{"path": patch_test.DEKU_STATIC, "unchecked": NO_ENTRY},
                              {"path": new, "unchecked": NO_COPY}], ""))

    def test_a_mod_file_past_its_size_table_entry_is_named(self):
        """Seen in the game: a placement file 176 bytes under its entry was dropped, and one 288 bytes under it
        loaded."""
        entry = patch_test.load_size(len(content(patch_test.DEKU_STATIC)))
        largest = entry - 264
        self.assertEqual(self.check_mod({patch_test.DEKU_STATIC: self.placement_of(largest)}), (0, [], ""))
        self.assertEqual(self.check_mod({patch_test.DEKU_STATIC: self.placement_of(largest + 1)}),
                         (TOO_LARGE, [{"path": patch_test.DEKU_STATIC, "needs": entry + 32, "entry": entry}], ""))

    def test_a_file_of_a_kind_whose_entries_share_no_overhead_is_named_unmeasured(self):
        """Seen in the game: the son's schedule, grown past its entry less its own overhead, loaded. The game's
        schedules have many overheads, so a larger one's need is not known."""
        schedule = content(DARUNIA_SON_SCHEDULE)
        self.assertEqual(self.check_mod({DARUNIA_SON_SCHEDULE: schedule + bytes(11_000)}),
                         (0, [{"path": DARUNIA_SON_SCHEDULE, "unchecked": UNMEASURED}], ""))

    def test_a_mods_own_size_table_is_the_one_its_files_must_fit(self):
        entry = patch_test.load_size(len(content(patch_test.DEKU_STATIC)))
        grown = self.placement_of(entry - 264 + 32)
        table = self.games_table({patch_test.DEKU_STATIC: entry + 32})
        self.assertEqual(self.check_mod({patch_test.DEKU_STATIC: grown}, table), (0, [], ""))

    def test_a_mod_kaepora_patch_made_fits_its_size_table_and_outgrows_the_games(self):
        """The shop slot sells an item through an actor the mod adds, so the mod holds a pack the game lacks and
        actor tables that grow. The game's own table gives its actor tables no one overhead: 264 to nineteen of them
        and 3064 to ActorInfo. So a table is not measured."""
        seed = json.loads(pathlib.Path(PLAN).read_text())
        seed["shops"] = [patch_test.shop_slot(patch_test.GORON_TUNIC_SLOT, "TBox_Rupee_Blue", 10)]
        with tempfile.TemporaryDirectory() as tmp:
            tmp = pathlib.Path(tmp)
            (tmp / "plan.json").write_text(json.dumps(seed))
            patched = subprocess.run([PATCH, GAME.nup, tmp / "plan.json", tmp / "mod"], capture_output=True, text=True)
            self.assertEqual(patched.returncode, 0, patched.stderr)
            romfs = tmp / "mod" / "romfs"
            self.assertTrue((romfs / "Pack/Actor/Shop_TBox_Rupee_Blue.pack.zs").is_file())
            new = [{"path": path, "unchecked": NO_COPY}
                   for path in sorted(patch_test.files(romfs)) if path not in GAME.files]
            self.assertEqual(new, [{"path": "Pack/Actor/Shop_TBox_Rupee_Blue.pack.zs", "unchecked": NO_COPY}])
            tables = [{"path": path, "unchecked": UNMEASURED}
                      for path in sorted(patch_test.files(romfs)) if path.endswith(ACTOR_TABLE_KIND)]
            self.assertTrue(tables)
            unchecked = sorted(new + tables, key=lambda line: line["path"])
            result = run("check-mod", tmp / "mod")
            lines = [json.loads(line) for line in result.stdout.splitlines()]
            self.assertEqual((result.returncode, lines, result.stderr), (0, unchecked, ""))

            games = dict(patch_test.read_size_table(content(patch_test.SIZE_TABLE))[1])
            ours = dict(patch_test.read_size_table(zstd.decompress((romfs / patch_test.SIZE_TABLE).read_bytes()))[1])
            raised = sorted(path for path in patch_test.files(romfs) if path in GAME.files
                            and not path.endswith(ACTOR_TABLE_KIND)
                            and ours.get(patch_test.size_key(path)) != games.get(patch_test.size_key(path)))
            (romfs / patch_test.SIZE_TABLE).unlink()
            result = run("check-mod", tmp / "mod")
            self.assertEqual(result.returncode, TOO_LARGE, result.stderr)
            lines = [json.loads(line) for line in result.stdout.splitlines()]
            self.assertEqual([line["path"] for line in lines if "needs" in line], raised)
            self.assertEqual([line for line in lines if "unchecked" in line], unchecked)
            self.assertTrue(raised)

    def test_a_mods_file_opens_as_the_games_own_with_the_mods_start_values(self):
        seed = json.loads(pathlib.Path(PLAN).read_text())
        starts = seed["start_values"]
        self.assertTrue(starts)
        with tempfile.TemporaryDirectory() as tmp:
            tmp = pathlib.Path(tmp)
            patched = subprocess.run([PATCH, GAME.nup, PLAN, tmp / "mod"], capture_output=True, text=True)
            self.assertEqual(patched.returncode, 0, patched.stderr)
            [opened] = dump("open", tmp / "mod" / "romfs" / GAME_DATA_LIST)
        [games] = dump("files", GAME_DATA_LIST)

        def defaults(tree):
            return {entry["Hash"]: entry["DefaultValue"] for entry in tree["root"]["Data"]["Bool"]}

        mods, own = defaults(opened), defaults(games)
        self.assertEqual({name: mods[patch_test.murmur3(name)] for name in starts}, starts)
        self.assertEqual({name: own[patch_test.murmur3(name)] for name in starts}, {name: False for name in starts})

    def test_a_file_cut_short_is_refused_with_its_path(self):
        with tempfile.TemporaryDirectory() as tmp:
            whole = pathlib.Path(tmp) / "GameDataList.Product.110.byml.zs"
            whole.write_bytes(GAME.read(GAME_DATA_LIST))
            cut = pathlib.Path(tmp) / "cut" / whole.name
            cut.parent.mkdir()
            cut.write_bytes(whole.read_bytes()[: whole.stat().st_size // 2])
            result = run("open", cut)
        self.assertEqual((result.returncode, result.stdout), (1, ""))
        self.assertTrue(result.stderr.startswith(f"{cut}: "), result.stderr)


if __name__ == "__main__":
    DUMP, PATCH = sys.argv[1:3]
    GAME = patch_test.Game(sys.argv[3])
    PLAN = sys.argv[4]
    unittest.main(argv=[sys.argv[0], *sys.argv[5:]])
