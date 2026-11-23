"""The tables in remake/ checked against the game's own files.

Usage: python3 -I -B tests/remake_tables_test.py <game.nup> [test name ...]
"""

import csv
import functools
import itertools
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


SAVE_WRITERS = {"GameDataActor", "ChallengeSystemActor"}
BANNER = "EventTriggerOpenLocationUI"
CLOCK_SETS = {"EventTrigger_SetTime", "Event_SetTimeForFlowchart"}
MUSIC_STARTS = {"EventTriggerStartBgmSimple"}


def scene_effect(actor, action, given):
    if actor in SAVE_WRITERS or action in {BANNER, *CLOCK_SETS, *MUSIC_STARTS}:
        return actor, action, repr(given)
    return None


def what_a_way_leaves(flow, entry, cases):
    """The saved writes and banners of the way through the entry point that takes these cases, with the last clock set
    and the last music start on it, which hold as the event ends."""
    (effects, _), = patch_test.ways_through(flow, entry, cases, scene_effect)
    last = [next((effect for effect in reversed(effects) if effect[1] in names), None)
            for names in (CLOCK_SETS, MUSIC_STARTS)]
    return {effect for effect in effects if effect[1] not in CLOCK_SETS | MUSIC_STARTS}, *last


def sub_flow_calls(flow, reached):
    return {index for index in reached if flow["events"][index][1][0] in (4, 5)}


def every_answer(flow):
    """Each way to answer the questions of a parsed flow, as the case each switch takes, by the switch's name."""
    switches = {event[0]: [struct.unpack_from("<I", event[3], 8 * case)[0]
                           for case in range(struct.unpack_from("<H", event[1], 2)[0])]
                for event in flow["events"] if event[1][0] == 1}
    return [dict(zip(switches, answers)) for answers in itertools.product(*switches.values())]


def played_in(flow, entries):
    """The entry points of a parsed flow that play when these do: they and those their ways call, in turn."""
    played = set(entries)
    while True:
        calls = sub_flow_calls(flow, patch_test.flow_reach(flow, roots=played))
        more = {flow["events"][index][-1] for index in calls if flow["events"][index][-2] in ("", flow["flowchart"])}
        if more <= played:
            return played
        played |= more


def parameters(flow, index):
    """An action's or a call's parameters, by name; an empty one for the other events."""
    event = flow["events"][index]
    keys, values = (event[2] if event[1][0] in (0, 1, 4, 5) else None) or ([None], [])
    return {key: value[1] for (_, key), value in zip(keys[1:], values)}


def event_settings(game):
    """Each event setting's flow, as a RomFS path, and the entry point it starts, empty for the flow's first."""
    rows = patch_test.byml(zstd_decompressed(game, "RSDB/EventSetting.Product.111.rstbl.byml.zs", None))
    flow = "Event/EventFlow/{}.bfevfl.zs".format
    return {row["__RowId"]: (flow(row["FlowchartFileName"].rsplit("/", 1)[1].removesuffix(".evfc")),
                             row.get("EntryPointName", ""))
            for row in rows if "FlowchartFileName" in row}


def arrivals(game):
    """The event settings the game starts as Link arrives after a scene change: those an entrance names for a first
    arrival (InitEventName) or a later one (NextEventName), and the boot events a scene change names
    (BootEventName)."""
    named = set()
    for path in game.files:
        if path.startswith("Banc/") and path.endswith(".bcett.byml.zs"):
            data = zstd_decompressed(game, path, game.dictionary)
            if b"EventName" in data:
                named |= {actor["Dynamic"][key] for actor in patch_test.byml(data).get("Actors", [])
                          for key in ("InitEventName", "NextEventName") if key in actor.get("Dynamic", {})}
        elif path.startswith("Event/EventFlow/"):
            data = zstd_decompressed(game, path, game.zs_dictionary)
            if b"BootEventName" in data:
                flow = patch_test.event_flow(data)
                named |= {given["BootEventName"] for index in range(len(flow["events"]))
                          if isinstance((given := parameters(flow, index)).get("BootEventName"), str)}
    return named


def zstd_decompressed(game, path, dictionary):
    data = game.read(path)
    return patch_test.zstd.decompress(data, zstd_dict=dictionary) if dictionary else patch_test.zstd.decompress(data)


def scene_change_starts(game):
    """By flow, the entry points the game plays as Link arrives after a scene change: those the arrival events start,
    and those that these start in turn through an EventTransition."""
    settings = event_settings(game)
    flows, starts, seen, todo = {}, {}, set(), sorted(arrivals(game))
    while todo:
        setting = todo.pop()
        if setting in seen or setting not in settings:
            continue
        seen.add(setting)
        path, entry = settings[setting]
        if path not in flows:
            flows[path] = game.event_flow(game.read(path))
        flow = flows[path]
        entry = entry or flow["entry points"][0][1][1]
        starts.setdefault(path, set()).add(entry)
        for index in patch_test.flow_reach(flow, roots=[entry]):
            actor, action = struct.unpack_from("<HH", flow["events"][index][1], 4)
            if flow["events"][index][1][0] == 0 and flow["actors"][actor][5][action] == "EventTransition":
                todo.append(parameters(flow, index)["EventName"])
    return starts


def holds_the_event(actor, action, given):
    """A wait or a fade in, which hold the event for a moment at least. Seen in the game: an arrival after a scene
    change whose event is all instant actions leaves the loading screen up for good."""
    if action == "EventWait" and given["Frames"] != bytes(4) or action == "EventEventFadeIn":
        return action
    return None


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

    def test_every_kept_good_is_the_game_s_goods_at_its_base_price(self):
        pouch = {row["__RowId"]: row for row in patch_test.byml(patch_test.zstd.decompress(
            self.game.read(patch_test.POUCH_ACTOR_INFO)))}
        for row in read_tsv("kept_goods.tsv"):
            self.assertIn(f"Pack/Actor/{row['goods']}.pack.zs", self.game.files, row["item"])
            self.assertEqual(pouch[row["goods"]]["BuyingPrice"], (patch_test.INT, int(row["price"])), row["item"])

    def test_the_quest_tables_are_the_game_s(self):
        for name, rows in quest_tables(self.game).items():
            with open(ROOT / "remake" / name, newline="") as f:
                self.assertEqual(list(csv.reader(f, delimiter="\t")), rows,
                                 f"{name}: python3 -I -B tests/quests.py <game.nup> remake writes it again")

    def test_every_area_scene_cut_leaves_what_its_scene_leaves_and_reaches_no_unlisted_call(self):
        rows = read_tsv("area_scenes.tsv")
        settings = event_settings(self.game)
        held = set()
        for path in dict.fromkeys(row["flow"] for row in rows):
            game = self.game.event_flow(self.game.read(path))
            cut = patch_test.area_scene_cuts(self.game.event_flow(self.game.read(path)),
                                             [row for row in rows if row["flow"] == path])
            self.assertNotEqual(cut, game, path)
            held |= {name for actor in game["actors"] for name in (actor[0], *actor[5])}
            names = [name for _, name in game["entry points"][0][1:]]
            runs = played_in(cut, {entry or names[0] for flow, entry in settings.values() if flow == path})
            for entry, listed in zip(names, game["entry points"][1]):
                for cases in every_answer(game) if entry in runs else ():
                    kept, clock, music = what_a_way_leaves(game, entry, cases)
                    kept_cut, clock_cut, music_cut = what_a_way_leaves(cut, entry, cases)
                    self.assertLessEqual(kept, kept_cut, (path, entry, cases))
                    self.assertEqual((clock_cut, music_cut), (clock, music), (path, entry, cases))
                calls = sub_flow_calls(game, patch_test.flow_reach(game, roots=[entry]))
                calls_cut = sub_flow_calls(cut, patch_test.flow_reach(cut, roots=[entry]))
                self.assertLessEqual(calls_cut - calls, set(struct.unpack(f"<{len(listed[1]) // 2}H", listed[1])),
                                     (path, entry))
        self.assertLessEqual(SAVE_WRITERS | {BANNER} | CLOCK_SETS | MUSIC_STARTS, held)

    def test_every_area_scene_cut_a_scene_change_plays_holds_the_event_on_every_way(self):
        starts = scene_change_starts(self.game)
        rows = read_tsv("area_scenes.tsv")
        instant = []
        for path in dict.fromkeys(row["flow"] for row in rows):
            cut = patch_test.area_scene_cuts(self.game.event_flow(self.game.read(path)),
                                             [row for row in rows if row["flow"] == path])
            for entry in sorted(starts.get(path, ())):
                for cases in every_answer(cut):
                    instant += [(path, entry, cases) for held, _ in
                                patch_test.ways_through(cut, entry, cases, holds_the_event) if not held]
        self.assertEqual(instant, [])

    def test_every_tutorial_value_is_a_bool_a_new_game_starts_the_other_way(self):
        data = self.game.game_data_list(self.game.read(patch_test.GAME_DATA_LIST))["Data"]["Bool"]
        defaults = {entry["Hash"][1]: entry["DefaultValue"] for entry in data}
        for row in read_tsv("tutorials.tsv"):
            self.assertEqual(defaults.get(patch_test.murmur3(row["value"])),
                             (patch_test.BOOL, int(row["start"] != "true")), row["value"])

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

    def test_every_value_the_tracker_reads_is_in_the_game_data_list_with_its_type(self):
        data = self.game.game_data_list(self.game.read(patch_test.GAME_DATA_LIST))["Data"]
        read = [("0x578d41e1", "Enum", "Title"), ("0x578d41e1", "Enum", "None"),
                ("Sequence_CurrentBanc", "String64", None)]
        for row in read_tsv("tracker.tsv"):
            read += [(row["value"], row["type"], row["when"]), (row["count"], row["count_type"], None)]
        missing = []
        for name, kind, choice in read:
            named = int(name, 16) if name.startswith("0x") else patch_test.murmur3(name)
            entry = next((e for e in data.get(kind, []) if e["Hash"][1] == named), None)
            if name and (entry is None or kind == "Enum" and choice not in entry["RawValues"]):
                missing.append((name, kind, choice))
        self.assertEqual(missing, [])


if __name__ == "__main__":
    GAME = sys.argv[1]
    unittest.main(argv=[sys.argv[0], *sys.argv[2:]])
