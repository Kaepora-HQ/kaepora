"""The tables in remake/ checked against the game's own files.

Usage: python3 -I -B tests/remake_tables_test.py <game.nup> [test name ...]
"""

import csv
import functools
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
ARRIVE = "Event/EventFlow/Dm_SY_BancChangeArrive.bfevfl.zs"
HIT_TARGET = "Event/EventFlow/MiniGame_HitTarget.bfevfl.zs"
ZELDA_WRITE = "AI/Npc_Hylia_Zelda_Adult_Patrol_RailEndPointSpecialAction.actionseq.root.ainb"
DAMPE = "Pack/Actor/Npc_Hylia_Dampe_002.pack.zs:AI/Npc_Hylia_Dampe_002.utility.root.ainb"
# Rows read from the game's files with readers other than tests/quests.py.
WRITES_OF_THE_NEXT_STEP = [
    ["EscapeFromCrumblingGanonCastle", "", f"Pack/Actor/{pack}.pack.zs:{ZELDA_WRITE}", "12",
     "OneShotChallengeProgressNextStep (blackboard TargetChallengeName)", "", "", ""]
    for pack in ("Npc_Hylia_Zelda_Adult", "Npc_Hylia_Zelda_Adult_Scalable")
]
READS_OF_THE_SAVED_STEP = [
    ["SageOfSpirit", "Step07", "IsEqual", ARRIVE, "Event96", "Step_SageOfSpirit"],
    ["SageOfSpirit", "Step07_pre00", "IsEqual", ARRIVE, "Event462", "Step_SageOfSpirit"],
    ["SageOfSpirit", "Step02", "IsEqual", ARRIVE, "Event100", "Step_SageOfSpirit"],
    ["MiniGame_HitTarget", "Playing", "IsEqual", HIT_TARGET, "Event19", "Step_MiniGame_HitTarget"],
    *[["EscapeFromCrumblingGanonCastle", step, "IsEqual",
       f"Logic/CrumblingGanonCastle_Prefab_{prefab}.logic.module.ainb", node, "Step_EscapeFromCrumblingGanonCastle"]
      for prefab, node, step in (("1be4", "1", "Step12"), ("5144", "1", "Step10"), ("7126", "1", "Step08"),
                                 ("8326", "1", "Step07"), ("c184", "0", "Step03"), ("e30c", "1", "Step02"))],
    ["MiniGame_DampeRace", "Playing", "IsEqual", DAMPE, "13", "Step_MiniGame_DampeRace > QueryMathEqualU32"],
    ["MiniGame_DampeRace", "PlayingSecond", "IsEqual", DAMPE, "88", "Step_MiniGame_DampeRace > QueryMathEqualU32"],
]
TESTED_BY_THE_SAVED_STEP = {
    (ARRIVE, "Event97"): ("Event96:0,1 Event462:0", ""),
    (ARRIVE, "Event104"): ("Event100:0", ""),
    (HIT_TARGET, "Event80"): ("Event19:0,1 Event507:0", " ".join((
        "MiniGame_HitTarget_RetryTalk", "Finish_Npc_Hylia_019_Shop_Child_StepStart", "MiniGame_HitTarget_ResultTalk",
        "Retry_Npc_Hylia_019_Shop_Child_StepStart", "MiniGame_HitTarget_GiveupTalk", "RewardTalk",
        "MiniGame_HitTarget_End", "MiniGame_HitTarget_GiveupTalk_TryOpenDoor", "Child_Reward", "Adult_Reward",
        "MiniGame_HitTarget_ZeroRemainBullet", "NeverPlayed", "MiniGame_HitTarget_End_noReset",
        "MiniGame_HitTarget_End_NoTalk", "MiniGame_HitTarget_GameOver", "MiniGame_HitTarget_DeadEnd"))),
}


@functools.cache
def quest_tables(game):
    return quests.tables(game)


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
        for name, rows in quest_tables(self.game).items():
            with open(ROOT / "remake" / name, newline="") as f:
                self.assertEqual(list(csv.reader(f, delimiter="\t")), rows,
                                 f"{name}: python3 -I -B tests/quests.py <game.nup> remake writes it again")

    def test_the_quest_tables_hold_the_writes_and_reads_not_named_through_an_expression(self):
        tables = quest_tables(self.game)
        writes, reads = tables["quest_writes.tsv"], tables["quest_reads.tsv"]
        tested = {(file, at): (tests, untested) for _, _, file, at, _, _, tests, untested in writes}
        self.assertEqual({
            "writes missing": [row for row in WRITES_OF_THE_NEXT_STEP if row not in writes],
            "reads missing": [row for row in READS_OF_THE_SAVED_STEP if row not in reads],
            "tests that differ": {site: tested[site] for site, cells in TESTED_BY_THE_SAVED_STEP.items()
                                  if tested[site] != cells},
        }, {"writes missing": [], "reads missing": [], "tests that differ": {}})


if __name__ == "__main__":
    GAME = sys.argv[1]
    unittest.main(argv=[sys.argv[0], *sys.argv[2:]])
