"""The quest tables of remake/, read from the game's files: each quest's steps, every write of a step and every read
of one.

Usage: python3 -I -B tests/quests.py <game.nup> remake

tests/remake_tables_test.py makes the tables again and compares them with remake/. remake/README.md says what each
column holds.
"""

import array
import csv
import pathlib
import re
import struct
import sys
from compression import zstd

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import patch_test  # noqa: E402

QUEST_TABLE = "RSDB/Challenge.Product.111.rstbl.byml.zs"
QUEST_ROW = re.compile(r"Work/Challenge/(\w+)\.game__challenge__Challenge\.gyml")
SCHEDULE = re.compile(r"Work/Component/AIScheduleParam/(\w+)\.game__component__AIScheduleParam\.gyml")
FLOW_WRITES = {"EventExecuteProgressChallengeStep", "EventExecuteRollbackChallengeStep",
               "EventTriggerProgressChallengeStep"}
ENUM_QUERY = "EventQueryIsGameDataEnum"
WRITE_NODES = {"TriggerChallengeSetStep", "TriggerChallengeSetStepNoEventPause", "OneShotChallengeSetStep",
               "OneShotChallengeProgressNextStep", "SeqOneShotResetChallengeStepForTimerStop"}
STEP_PARAMS = ("StepName", "ChallengeStep")
COMPARE = "HoldChallengeCurrentStepCheckOrder"
STEP_QUERY = "QueryGetChallengeStep"
RAIL_QUERY = "QueryAIScheduleGetCurRailIndexRelatedToChallengeProgress"
# A saved expression is a postfix list. These operations compare an enum with the value that follows: 12 is strictly
# after and 1 equal, as the game's own expression names say. 11 stands only negated beside a negated 12, as the low end
# of a range, and is read as strictly before.
STEP_TESTS = {1: "IsEqual", 11: "IsBefore", 12: "IsAfter"}
EXPRESSION, VALUE, NOT = 10, 2, 5
TIMELINE = (0, 1)
ACTION, SWITCH, FORK, JOIN, SUB_FLOW, SUB_FLOW_SWITCH, RETURN = range(7)
END = 0xFFFF
AINB_VERSION = 0x408
AINB_TYPES = ("int", "bool", "float", "string", "vec3f", "userdefined")
AINB_INPUT_SIZES = {"vec3f": 24, "userdefined": 20}
# An input with any of these flags takes its value from the actor's blackboard, or from an expression when all three are
# set, and the string in its slot is only a default.
NOT_FIXED = 0xC2000000
DEFAULT = " (default)"
HASH = " (hash)"
PRINTABLE = re.compile(rb"[\x20-\x7e]{4,}")


class Ainb:
    """An AINB file of version 0x408, a logic node graph: each node's name, its fixed strings, the strings its inputs
    hold only as defaults, the whole numbers its inputs hold, its inputs with the node and output each reads, and its
    outputs; and the names of the string parameters of its blackboard."""

    def __init__(self, data):
        assert data[:4] == b"AIB ", data[:4]
        self.data = data
        assert self.u32(4) == AINB_VERSION, hex(self.u32(4))
        self.pool = self.u32(0x24)
        blackboard = self.u32(0x20)
        # The string parameters come first, after a header of 8 bytes for each of the seven types.
        self.blackboard = [self.string(self.u32(blackboard + 56 + 8 * i) & 0x3FFFFF)
                           for i in range(struct.unpack_from("<H", data, blackboard)[0])]
        constants, wires, self.multi = self.u32(0x2C), self.u32(0x34), self.u32(0x38)
        strings = self.u32(constants + 4 * AINB_TYPES.index("string"))
        wire_tables = [self.u32(wires + 4 * i) for i in range(12)]
        self.nodes = []
        nodes = 0x74 + 0x18 * self.u32(0x0C)
        for index in range(self.u32(0x10)):
            at = nodes + 0x3C * index
            assert struct.unpack_from("<H", data, at + 2)[0] == index, at
            params = self.u32(at + 0x14)
            node = {"name": self.string(self.u32(at + 8)), "strings": {}, "defaults": {}, "numbers": {}, "inputs": [],
                    "outputs": []}
            first, count = struct.unpack_from("<II", data, params + 8 * AINB_TYPES.index("string"))
            for entry in range(strings + 12 * first, strings + 12 * (first + count), 12):
                node["strings"][self.string(self.u32(entry))] = self.string(self.u32(entry + 8))
            for k, kind in enumerate(AINB_TYPES):
                first, count, first_out, count_out = struct.unpack_from("<IIII", data, params + 0x30 + 16 * k)
                size = AINB_INPUT_SIZES.get(kind, 16)
                for entry in range(wire_tables[2 * k] + size * first, wire_tables[2 * k] + size * (first + count),
                                   size):
                    name = self.string(self.u32(entry))
                    source_at = entry + (8 if kind == "userdefined" else 4)
                    source, output, flags = struct.unpack_from("<hhI", data, source_at)
                    if kind == "string":
                        fixed = source == -1 and not flags & NOT_FIXED
                        node["strings" if fixed else "defaults"][name] = self.string(self.u32(source_at + 8))
                    if kind == "int":
                        node["numbers"][name] = struct.unpack_from("<i", data, source_at + 8)[0]
                    node["inputs"].append((kind, name, source, output, flags))
                size = 8 if kind == "userdefined" else 4
                for entry in range(wire_tables[2 * k + 1] + size * first_out,
                                   wire_tables[2 * k + 1] + size * (first_out + count_out), size):
                    node["outputs"].append((kind, self.string(self.u32(entry) & 0x7FFFFFFF)))
            self.nodes.append(node)
        self.feeds = {}
        for index, node in enumerate(self.nodes):
            for source, output in self.sources(node):
                self.feeds.setdefault(source, []).append((index, output))

    def u32(self, at):
        return struct.unpack_from("<I", self.data, at)[0]

    def string(self, offset):
        at = self.pool + offset
        return self.data[at:self.data.index(b"\0", at)].decode()

    def sources(self, node):
        """Each (node, output name) one of the node's inputs reads. An input can read several. The output is found
        among the source's outputs of the input's type, by the index's low 15 bits; its name is empty where none is
        there."""
        for kind, _, source, output, _ in node["inputs"]:
            if source >= 0:
                wired = [(source, output)]
            elif source <= -100:
                wired = [struct.unpack_from("<hh", self.data, self.multi + 8 * k)
                         for k in range(-100 - source, -100 - source + output)]
            else:
                continue
            for s, o in wired:
                outputs = [name for k, name in self.nodes[s]["outputs"] if k == kind]
                yield s, outputs[o & 0x7FFF] if o & 0x7FFF < len(outputs) else ""

    def upstream(self, index):
        """Each node that feeds the node, directly or through others, with its outputs that are read."""
        found, todo = {}, [index]
        while todo:
            for source, output in self.sources(self.nodes[todo.pop()]):
                if source not in found:
                    todo.append(source)
                found.setdefault(source, set()).add(output)
        return found

    def origin(self, index, slot):
        """Where a string slot of a node takes its value, following each feeding node's first fed string input:
        ("", text) where the file fixes it, ("blackboard", name) where it reads the actor's blackboard, ("from", node)
        where a node with no fed string input gives it."""
        node = self.nodes[index]
        source, flags = next(((s, f) for kind, name, s, _, f in node["inputs"] if kind == "string" and name == slot),
                             (-1, 0))
        assert source >= -1 and flags & NOT_FIXED != NOT_FIXED, (node["name"], slot)
        if source == -1:
            return ("blackboard", self.blackboard[flags & 0xFFFF]) if flags & NOT_FIXED \
                else ("", node["strings"].get(slot, ""))
        fed = [name for kind, name, s, _, f in self.nodes[source]["inputs"]
               if kind == "string" and (s != -1 or f & NOT_FIXED)]
        return self.origin(source, fed[0]) if fed else ("from", self.nodes[source]["name"])

    @staticmethod
    def named(node):
        """Each (slot, text, mark) the node names: its fixed strings, then its inputs' defaults."""
        return [(k, v, "") for k, v in node["strings"].items()] + [(k, v, DEFAULT) for k, v in node["defaults"].items()]


def byml_strings(data):
    """Every string of a BYML document's string table, read without parsing the document."""
    order = {b"YB": "little", b"BY": "big"}[data[:2]]
    at = int.from_bytes(data[8:12], order)
    if not at:
        return []
    count = int.from_bytes(data[at + 1:at + 4], order)
    starts = [at + int.from_bytes(data[at + 4 + 4 * i:at + 8 + 4 * i], order) for i in range(count)]
    return [data[s:data.index(b"\0", s)].decode() for s in starts]


def byml_words(data):
    """Every aligned 32-bit word of a BYML document, in its byte order. Every node, and so every number, is aligned."""
    words = array.array("I", data[:len(data) // 4 * 4])
    if (data[:2] == b"BY") == (sys.byteorder == "little"):
        words.byteswap()
    return set(words)


def byml_paths(node, path=""):
    """Each (path, value) of a parsed BYML document: its strings, and its numbers as (type, value). A list element
    with a Hash or a __RowId is named by it."""
    if isinstance(node, (str, tuple)):
        yield path, node
    elif isinstance(node, dict):
        for key, value in node.items():
            yield from byml_paths(value, f"{path}/{key}" if path else str(key))
    elif isinstance(node, list):
        for i, value in enumerate(node):
            key = i
            if isinstance(value, dict) and isinstance(value.get("Hash"), tuple):
                key = value["Hash"][1]
            elif isinstance(value, dict) and isinstance(value.get("__RowId"), str):
                key = value["__RowId"]
            yield from byml_paths(value, f"{path}/{key}" if path else str(key))


def byml_named_steps(node, path=""):
    """Each (path, quest, step, key) where a dict names a quest at a key ending in ChallengeName and its steps at the
    same key with ChallengeStepName."""
    if isinstance(node, dict):
        for key, value in node.items():
            if isinstance(key, str) and key.endswith("ChallengeName") and QUEST_ROW.fullmatch(f"{value}"):
                steps_key = key.removesuffix("ChallengeName") + "ChallengeStepName"
                steps = node.get(steps_key, [])
                for i, step in enumerate(steps if isinstance(steps, list) else [steps]):
                    yield f"{path}/{steps_key}/{i}" if path else f"{steps_key}/{i}", challenge(value), step, key
    for key, value in node.items() if isinstance(node, dict) else enumerate(node) if isinstance(node, list) else []:
        yield from byml_named_steps(value, f"{path}/{key}" if path else str(key))


def challenge(text):
    """A quest's name, from its name or its quest table row."""
    found = QUEST_ROW.fullmatch(text)
    return found[1] if found else text


class Quests:
    """The game's quests, their steps in order, and the saved expressions that compare a step."""

    def __init__(self, game):
        self.game = game
        rows = patch_test.byml(self.decompressed(QUEST_TABLE, game.read(QUEST_TABLE)))
        data = game.game_data_list(game.read(patch_test.GAME_DATA_LIST))["Data"]
        enums = {entry["Hash"][1]: entry["RawValues"] for entry in data["Enum"]}
        self.table, self.steps, self.step_of = {}, {}, {}
        for row in rows:
            quest = QUEST_ROW.fullmatch(row["__RowId"])[1]
            steps = ["NotReady"] + [step["Name"] for step in row["Steps"]] + ["Complete"]
            enum = patch_test.murmur3("Step_" + quest)
            # IsAfter and IsBefore compare places among the values of Step_<quest>, so the steps keep its order.
            assert enums[enum] == steps, quest
            self.table[quest], self.steps[quest] = row, steps
            self.step_of |= {(enum, patch_test.murmur3(step)): (quest, step) for step in steps}
        self.enums = {"Step_" + quest: quest for quest in self.steps}
        self.all_steps = {step for steps in self.steps.values() for step in steps}
        self.expressions = {entry["Hash"][1]: [[v for _, v in item] for item in entry["Values"]]
                            for entry in data["BoolExp"]}
        self.names = {patch_test.murmur3(name): name for quest, steps in self.steps.items()
                      for name in [f"{quest}_IsCompleted_Exp"] + [f"{quest}_IsAfter_{step}_Exp" for step in steps]}
        self.terms_of, self.named = {}, {}
        self.hashes = {h for h in self.expressions if self.terms(h)}

    def decompressed(self, path, data):
        if path.endswith(".bcett.byml.zs"):
            return zstd.decompress(data, zstd_dict=self.game.dictionary)
        if path.endswith(".pack.zs"):
            return zstd.decompress(data, zstd_dict=self.game.pack_dictionary)
        if path.endswith(".zs"):
            return zstd.decompress(data, zstd_dict=self.game.zs_dictionary)
        return data

    def terms(self, expression):
        """Each (quest, test, step, through) of a saved expression, through the expressions it names. A test directly
        negated is `not <test>`. `through` holds each (negated, hash) of the expressions on the way."""
        if expression not in self.terms_of:
            items = self.expressions[expression]
            out = []
            for i, item in enumerate(items):
                if item[0] in STEP_TESTS and len(item) == 2 and (item[1], items[i + 1][1]) in self.step_of:
                    assert items[i + 1][0] == VALUE, items
                    quest, step = self.step_of[(item[1], items[i + 1][1])]
                    test = ("not " if items[i + 2:i + 3] == [[NOT]] else "") + STEP_TESTS[item[0]]
                    out.append((quest, test, step, ()))
                elif item[0] == EXPRESSION:
                    negated = items[i + 1:i + 2] == [[NOT]]
                    out += [(q, t, s, ((negated, item[1]),) + through) for q, t, s, through in self.terms(item[1])]
            self.terms_of[expression] = out
        return self.terms_of[expression]

    def reads(self, text):
        """The terms of the saved expression the text names, if it names one."""
        if text not in self.named:
            h = patch_test.murmur3(text)
            self.named[text] = self.terms(h) if h in self.expressions else []
            if self.named[text]:
                self.names[h] = text
        return self.named[text]

    def via(self, text, through):
        """What a read names. An expression a file holds by its hash takes a name that hashes to it, where another
        file or the quest's own expression names give one, and is in hex where none does."""
        head = self.names.get(text, f"{text:#010x}") + HASH if isinstance(text, int) else text
        return " > ".join([head] + [("not " if negated else "") + self.names.get(h, f"{h:#010x}")
                                    for negated, h in through])


def params(event):
    """An event's string parameters by name."""
    if event[1][0] not in (ACTION, SWITCH, SUB_FLOW, SUB_FLOW_SWITCH) or event[2] is None:
        return {}
    names, values = event[2]
    return {name: value[1] for (_, name), value in zip(names[1:], values) if value[0] == 5}


def flow_graph(flow, start):
    """The states (event, calls) a flow reaches from an event, with each (state, case) that leads to each. Calls are
    the open calls of entries of the same flow, each as (the entry, the event after the call, the sub-flow switch whose
    cases follow it). A call of an entry already open is stepped over. An entry ends with no next event or at a
    return."""
    events = flow["events"]
    entries = entry_points(flow)
    before, todo = {(start, ()): []}, [(start, ())]

    def cases(index):
        return [(case, target) for case, target, _ in struct.iter_unpack("<IHH", events[index][3] or b"")]

    def land(target, calls, edge):
        while target == END:
            if not calls:
                return
            (_, target, switch), calls = calls[-1], calls[:-1]
            if switch is not None:
                for _, after in cases(switch):
                    land(after, calls, edge)
                return
        if (target, calls) not in before:
            before[(target, calls)] = []
            todo.append((target, calls))
        before[(target, calls)].append(edge)

    while todo:
        state = todo.pop()
        index, calls = state
        _, header, *rest = events[index]
        kind, first, join = header[0], *struct.unpack_from("<HH", header, 2)
        if kind in (SUB_FLOW, SUB_FLOW_SWITCH) and not rest[-2] and rest[-1] in entries \
                and rest[-1] not in {entry for entry, _, _ in calls}:
            call = (rest[-1], first, None) if kind == SUB_FLOW else (rest[-1], None, index)
            land(entries[rest[-1]], calls + (call,), (state, None))
        elif kind in (ACTION, JOIN, SUB_FLOW):
            land(first, calls, (state, None))
        elif kind in (SWITCH, SUB_FLOW_SWITCH):
            for case, target in cases(index):
                land(target, calls, (state, case))
        elif kind == FORK:
            for (target,) in struct.iter_unpack("<H", rest[0]):
                land(target, calls, (state, None))
            land(join, calls, (state, None))
        else:
            assert kind == RETURN, kind
            land(END, calls, (state, None))
    return before


def entry_points(flow):
    return {name: struct.unpack_from("<H", entry[0], 4)[0]
            for (_, name), entry in zip(flow["entry points"][0][1:], flow["entry points"][1])}


def tests_on_the_way(before, start, target, is_test):
    """Whether the target is among the states, whether a way from the start reaches it past no test, and each
    (test, case) on a way to it."""
    targets = [state for state in before if state[0] == target]
    if not targets:
        return False, False, set()
    found, seen, todo = set(), set(), list(targets)
    while todo:
        state = todo.pop()
        if state not in seen:
            seen.add(state)
            for previous, case in before[state]:
                if is_test(previous[0]):
                    found.add((previous[0], case))
                todo.append(previous)
    after = {}
    for state, edges in before.items():
        for previous, _ in edges:
            after.setdefault(previous, []).append(state)
    seen, todo = set(), [] if is_test(start) else [(start, ())]
    while todo:
        state = todo.pop()
        if state[0] == target:
            return True, True, found
        if state not in seen:
            seen.add(state)
            todo += [s for s in after.get(state, []) if not is_test(s[0])]
    return True, False, found


def flow_rows(quests, path, flow):
    """The writes and the reads of one event flow."""
    events = flow["events"]
    reads, writes, tested = [], [], {}
    for index, event in enumerate(events):
        kind = event[1][0]
        actor, action = struct.unpack_from("<HH", event[1], 4)
        p = params(event)
        for text in p.values():
            found = quests.reads(text)
            if text in quests.enums:
                # A flow asks the saved value itself only whether it equals a step, and sets it only by quest.
                assert kind == SWITCH and flow["actors"][actor][6][action] == ENUM_QUERY, (path, event[0])
                quest = quests.enums[text]
                found = [(quest, "IsEqual", next(v for v in p.values() if v in quests.steps[quest]), ())]
            for quest, test, step, through in found:
                reads.append([quest, step, test, path, event[0], (text, through), index])
                if kind == SWITCH:
                    tested.setdefault(index, set()).add(quest)
        if kind == ACTION and flow["actors"][actor][5][action] in FLOW_WRITES:
            step = next(p[k] for k in STEP_PARAMS if k in p)
            writes.append((p["ChallengeName"], step, index, flow["actors"][actor][5][action]))
    entries = entry_points(flow)
    graphs = {entry: flow_graph(flow, start) for entry, start in entries.items()} if writes else {}
    rows = []
    for quest, step, index, action in writes:
        reaching, untested, cases = [], [], {}
        for entry, start in entries.items():
            hit, clear, found = tests_on_the_way(graphs[entry], start, index, lambda i: quest in tested.get(i, ()))
            reaching += [entry] if hit else []
            untested += [entry] if clear else []
            for test, case in found:
                cases.setdefault(test, set()).add(case)
        tests = " ".join(f"{events[t][0]}:{','.join(map(str, sorted(c)))}" for t, c in sorted(cases.items()))
        rows.append([quest, step, path, events[index][0], action, " ".join(reaching), tests, " ".join(untested),
                     index])
    return rows, reads


def ainb_rows(quests, path, ainb, pack_quests):
    """The writes and the reads of one AINB file. `pack_quests` is the quest each key of its actor pack names."""
    writes, reads = [], []

    def quest_of(index):
        """A node's quest, and what its row names: the saved value it compares, or the node, with where its quest
        comes from where the file does not fix it."""
        node = ainb.nodes[index]
        enum = next((v for v in node["strings"].values() if v in quests.enums), None)
        if enum:
            return quests.enums[enum], enum
        how, value = ainb.origin(index, "ChallengeName")
        if how == "blackboard":
            return pack_quests.get(value, ""), f"{node['name']} (blackboard {value})"
        if how == "from":
            return "", f"{node['name']} (from {value})"
        return challenge(value), node["name"]

    def reads_step(node):
        return node["name"] in (COMPARE, STEP_QUERY, RAIL_QUERY) or not quests.enums.keys().isdisjoint(
            node["strings"].values())

    def reads_quest(index, quest):
        node = ainb.nodes[index]
        return any(q == quest for _, text, _ in ainb.named(node) for q, *_ in quests.reads(text)) or (
            reads_step(node) and quest_of(index)[0] == quest)

    def steps_named(nodes, quest):
        """The steps of the quest, or of any quest where it is not known, that the nodes name in fixed strings."""
        known = quests.steps[quest] if quest else quests.all_steps
        return sorted({v for node in nodes for k, v in node["strings"].items() if v in known and k != "ChallengeName"})

    for index, node in enumerate(ainb.nodes):
        for _, text, mark in ainb.named(node):
            for q, test, s, through in quests.reads(text):
                reads.append([q, s, test, path, str(index), (text + mark, through), index])
        if node["name"] in WRITE_NODES:
            quest, action = quest_of(index)
            step = next((node["strings"][k] for k in STEP_PARAMS if k in node["strings"]), "")
            tests = " ".join(f"{source}:{','.join(sorted(o for o in outputs if o))}"
                             for source, outputs in sorted(ainb.upstream(index).items())
                             if quest and reads_quest(source, quest))
            writes.append([quest, step, path, str(index), action, "", tests, "", index])
        elif reads_step(node):
            quest, via = quest_of(index)
            if steps := steps_named([node], quest):
                outputs = sorted({output for _, output in ainb.feeds.get(index, [])}) or [""]
                reads += [[quest, step, output, path, str(index), (via, ()), index]
                          for step in steps for output in outputs]
            elif node["name"] == RAIL_QUERY:
                reads.append([quest, "", "", path, str(index), (via, ()), index])
            else:
                for consumer, _ in ainb.feeds.get(index, []) or [(None, "")]:
                    if consumer is None:
                        reads.append([quest, "", "", path, str(index), (via, ()), index])
                        continue
                    other = ainb.nodes[consumer]
                    compared = steps_named([other] + [ainb.nodes[s] for s, _ in ainb.sources(other)], quest) or [""]
                    test = ",".join(name for _, name in other["outputs"])
                    reads += [[quest, step, test if step else "", path, str(index),
                               (f"{via} > {other['name'] or consumer}", ()), index] for step in compared]
    return writes, reads


def byml_rows(quests, path, data):
    """The reads of one BYML document: the saved expressions it names or holds by hash, and the steps it names beside
    their quest."""
    order = {b"YB": "little", b"BY": "big"}.get(data[:2])
    # The game's other BYML versions are effect sets and node definitions.
    if order is None or int.from_bytes(data[2:4], order) != 7 or not (
            byml_words(data) & quests.hashes or b"ChallengeStepName" in data
            or any(quests.reads(text) for text in byml_strings(data))):
        return []
    document = patch_test.byml(data)
    rows = []
    for at, value in byml_paths(document):
        if isinstance(value, str):
            rows += [[quest, step, test, path, at, (value, through), 0]
                     for quest, test, step, through in quests.reads(value)]
        elif value[0] in (patch_test.INT, patch_test.UINT) and value[1] in quests.hashes:
            rows += [[quest, step, test, path, at, (value[1], through), 0]
                     for quest, test, step, through in quests.terms(value[1])]
    rows += [[quest, step, "", path, at, (key, ()), 0] for at, quest, step, key in byml_named_steps(document)]
    return rows


def files(game, quests):
    """Each (path, data, quests by key) of the game's event flows, AINB files and BYML documents, actor packs' members
    included. For a pack's member, `quests by key` names the quest each key of the pack's BYML members names."""
    for path in sorted(game.files):
        if path in (patch_test.GAME_DATA_LIST, "Pack/ZsDic.pack.zs"):
            continue
        if path.endswith((".bfevfl.zs", ".byml.zs", ".bgyml")):
            yield path, quests.decompressed(path, game.read(path)), {}
        elif path.endswith(".ainb"):
            yield path, game.read(path), {}
        elif path.endswith(".pack.zs"):
            members = patch_test.sarc(quests.decompressed(path, game.read(path)))
            by_key = {}
            for member, data in members.items():
                if member.endswith((".bgyml", ".byml")) and b"Work/Challenge/" in data:
                    for at, value in byml_paths(patch_test.byml(data)):
                        if isinstance(value, str) and QUEST_ROW.fullmatch(value):
                            key = at.split("/")[-1]
                            assert by_key.setdefault(key, challenge(value)) == challenge(value), (path, key)
            for member, data in sorted(members.items()):
                if member.endswith((".ainb", ".bgyml", ".byml")):
                    yield f"{path}:{member}", data, by_key


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
    writes, reads = [], []
    for path, data, pack_quests in files(game, quests):
        if path.endswith(".bfevfl.zs"):
            if struct.unpack_from("<HH", data, 0x20) == TIMELINE:
                assert not any(quests.reads(t) or t in quests.enums or t in FLOW_WRITES
                               for t in (m.group().decode() for m in PRINTABLE.finditer(data))), path
                continue
            w, r = flow_rows(quests, path, patch_test.event_flow(data))
        elif path.endswith(".ainb"):
            w, r = ainb_rows(quests, path, Ainb(data), pack_quests)
        else:
            w, r = [], byml_rows(quests, path, data)
        writes += w
        reads += r
    order = {quest: i for i, quest in enumerate(quests.steps)}

    def step_order(quest, step):
        """A row's quest and step in the game's order; a row whose file names no quest comes first."""
        steps = quests.steps.get(quest, [])
        return order.get(quest, -1), steps.index(step) if step in steps else len(steps), step

    writes = [w[:-1] for w in sorted(writes, key=lambda w: (*step_order(w[0], w[1]), w[2], w[-1]))]
    reads = sorted({(q, s, t, p, at, quests.via(*via), i) for q, s, t, p, at, via, i in reads},
                   key=lambda r: (*step_order(r[0], r[1]), r[3], r[6], r[4], r[2], r[5]))
    return {
        "quest_steps.tsv": [["quest", "step", "depends_on", "forced_by", "participants"], *step_rows(quests)],
        "quest_writes.tsv": [["quest", "step", "file", "at", "action", "entries", "tests", "untested_from"], *writes],
        "quest_reads.tsv": [["quest", "step", "test", "file", "at", "via"], *[list(r[:-1]) for r in reads]],
    }


def main():
    folder = pathlib.Path(sys.argv[2])
    for name, rows in tables(patch_test.Game(sys.argv[1])).items():
        with open(folder / name, "w", newline="") as f:
            csv.writer(f, delimiter="\t", lineterminator="\n").writerows(rows)


if __name__ == "__main__":
    main()
