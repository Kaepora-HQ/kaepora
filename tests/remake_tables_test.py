"""The tables in remake/ checked against the game's own files.

Usage: python3 -I -B tests/remake_tables_test.py <game.nup> [test name ...]
"""

import csv
import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import patch_test  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parent.parent
GAME = None


def read_tsv(name):
    with open(ROOT / "remake" / name, newline="") as f:
        return list(csv.DictReader(f, delimiter="\t"))


class RemakeTablesTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        game = patch_test.Game(GAME)
        cls.packs = {path: game.text_pack(game.read(path)) for path in patch_test.ENGLISH_PACKS}

    def messages(self, file):
        """The messages of one message file in each English text pack."""
        return {path: patch_test.msbt(pack[file])[1] for path, pack in self.packs.items()}

    def test_every_paired_stone_has_a_message_of_its_own(self):
        labels = [row["label"] for row in read_tsv("stones.tsv")]
        self.assertEqual(len(labels), len(set(labels)))
        for path, messages in self.messages(patch_test.STONE_MESSAGES).items():
            self.assertLessEqual(set(labels), messages.keys(), path)


if __name__ == "__main__":
    GAME = sys.argv[1]
    unittest.main(argv=[sys.argv[0], *sys.argv[2:]])
