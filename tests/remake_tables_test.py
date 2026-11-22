"""The tables in remake/ checked against the game's own files.

Usage: python3 -I -B tests/remake_tables_test.py <game.nup> [test name ...]
"""

import csv
import pathlib
import struct
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import patch_test  # noqa: E402
import quests  # noqa: E402
import shop_goods  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parent.parent
GAME = None


def read_tsv(name):
    with open(ROOT / "remake" / name, newline="") as f:
        return list(csv.DictReader(f, delimiter="\t"))


# Codes of group 201 come before some nouns in the game's names. Hints leave them out. That they show nothing in
# English is not confirmed.
GRAMMAR = 201


def without_grammar_codes(text):
    shown, at = bytearray(), 0
    while at < len(text):
        if text[at] == 0x0E:
            group, _, size = struct.unpack_from("<HHH", text, at + 1)
            assert group == GRAMMAR, text
            at += 7 + size
        else:
            shown.append(text[at])
            at += 1
    return shown.decode()


class RemakeTablesTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.game = patch_test.Game(GAME)
        cls.packs = {path: cls.game.text_pack(cls.game.read(path)) for path in patch_test.ENGLISH_PACKS}

    def messages(self, file):
        return {path: patch_test.msbt(pack[file])[1] for path, pack in self.packs.items()}

    def test_every_paired_stone_has_a_message_of_its_own(self):
        labels = [row["label"] for row in read_tsv("stones.tsv")]
        self.assertEqual(len(labels), len(set(labels)))
        for path, messages in self.messages(patch_test.STONE_MESSAGES).items():
            self.assertLessEqual(set(labels), messages.keys(), path)

    def assert_names(self, table, key, file):
        for path, messages in self.messages(file).items():
            for row in read_tsv(table):
                self.assertEqual(without_grammar_codes(messages[row["label"]]), row["name"], (path, row[key]))

    def test_every_place_has_the_game_s_name(self):
        self.assert_names("places.tsv", "area", "LocationMsg/Location.msbt")

    def test_a_hint_names_a_place_in_the_end_of_the_game_s_name(self):
        for row in read_tsv("places.tsv"):
            says = row["says"].removeprefix("the ")
            self.assertTrue(row["name"] == says or row["name"].endswith(" " + says), row["area"])

    def test_every_item_has_the_game_s_name(self):
        self.assert_names("item_names.tsv", "remake_item", "ActorMsg/PouchContent.msbt")

    def test_every_shop_goods_row_is_the_game_s_goods_or_its_model_on_the_shelf(self):
        infos = shop_goods.actor_infos(self.game)
        for row in read_tsv("shop_goods.tsv"):
            measured = [row[key] for key in ("pose", "bottom", "top", "width", "reach")]
            if row["goods"]:
                self.assertIn(f"Pack/Actor/{row['goods']}.pack.zs", self.game.files, row["item"])
                self.assertEqual(measured, [""] * 5, row["item"])
            else:
                self.assertEqual(measured, shop_goods.row(self.game, infos, row["item"]), row["item"])

    def test_the_quest_tables_are_the_game_s(self):
        for name, rows in quests.tables(self.game).items():
            with open(ROOT / "remake" / name, newline="") as f:
                self.assertEqual(list(csv.reader(f, delimiter="\t")), rows,
                                 f"{name}: python3 -I -B tests/quests.py <game.nup> remake writes it again")


if __name__ == "__main__":
    GAME = sys.argv[1]
    unittest.main(argv=[sys.argv[0], *sys.argv[2:]])
