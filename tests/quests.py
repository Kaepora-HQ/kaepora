"""The quest tables of remake/, read from the game's files: each quest's steps.

Usage: python3 -I -B tests/quests.py <game.nup> remake

tests/remake_tables_test.py makes the tables again and compares them with remake/. remake/README.md says what each
column holds.
"""

import csv
import pathlib
import re
import sys
from compression import zstd

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import patch_test  # noqa: E402

QUEST_TABLE = "RSDB/Challenge.Product.111.rstbl.byml.zs"
QUEST_ROW = re.compile(r"Work/Challenge/(\w+)\.game__challenge__Challenge\.gyml")
SCHEDULE = re.compile(r"Work/Component/AIScheduleParam/(\w+)\.game__component__AIScheduleParam\.gyml")


class Quests:
    """The game's quests and their steps in order."""

    def __init__(self, game):
        rows = patch_test.byml(zstd.decompress(game.read(QUEST_TABLE), zstd_dict=game.zs_dictionary))
        data = game.game_data_list(game.read(patch_test.GAME_DATA_LIST))["Data"]
        enums = {entry["Hash"][1]: entry["RawValues"] for entry in data["Enum"]}
        self.table, self.steps = {}, {}
        for row in rows:
            quest = QUEST_ROW.fullmatch(row["__RowId"])[1]
            steps = ["NotReady"] + [step["Name"] for step in row["Steps"]] + ["Complete"]
            # IsAfter and IsBefore compare places among the values of Step_<quest>, so the steps keep its order.
            assert enums[patch_test.murmur3("Step_" + quest)] == steps, quest
            self.table[quest], self.steps[quest] = row, steps


def step_rows(quests):
    rows = []
    for quest, row in quests.table.items():
        by_name = {step["Name"]: step for step in row["Steps"]}
        for step in quests.steps[quest]:
            entry = by_name.get(step, {})
            schedules = [SCHEDULE.fullmatch(p["Schedule"])[1] for p in entry.get("Participants", [])]
            rows.append([quest, step, row.get("DependFlagName", "") if step == "NotReady" else "",
                         entry.get("ForceProgressGameData", ""), " ".join(schedules)])
    return rows


def tables(game):
    """Each table's file name and its rows, the header first."""
    quests = Quests(game)
    return {
        "quest_steps.tsv": [["quest", "step", "depends_on", "forced_by", "participants"], *step_rows(quests)],
    }


def main():
    folder = pathlib.Path(sys.argv[2])
    for name, rows in tables(patch_test.Game(sys.argv[1])).items():
        with open(folder / name, "w", newline="") as f:
            csv.writer(f, delimiter="\t", lineterminator="\n").writerows(rows)


if __name__ == "__main__":
    main()
