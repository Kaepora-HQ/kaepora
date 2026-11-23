#include "bfevfl.h"

#include "bytes.h"

#include <algorithm>
#include <format>
#include <map>
#include <ranges>
#include <set>
#include <stdexcept>
#include <utility>

namespace bfevfl {

namespace {

constexpr std::string_view Magic("BFEVFL\0\0", 8);
constexpr uint16_t Version = 0x0400;
constexpr uint16_t LittleEndian = 0xFEFF;

constexpr size_t FlowchartCount = 0x20;
constexpr size_t TimelineCount = 0x22;
constexpr size_t Flowcharts = 0x28;

constexpr size_t ActorSize = 0x38;
constexpr size_t EventSize = 0x30;
constexpr size_t EntrySize = 0x20;

// The relocation table: "RELT", its own offset, its one section, the section's data end at 0x1C, its entry count at
// 0x24, and from 0x28 each entry's base offset and mask. Bit k of the mask lists a pointer field at base + 8k.
constexpr size_t PointersPerEntry = 32;

// A string: its u16 length, its bytes, and a zero byte.
std::string Text(std::string_view data, uint64_t offset) {
    const std::string_view text = StringAt(data, offset + 2);
    if (text.size() != ReadAt<uint16_t>(data, offset)) {
        throw std::runtime_error("a string's length does not match its text");
    }
    return std::string(text);
}

std::string Name(std::string_view data, uint64_t field) {
    return Text(data, ReadAt<uint64_t>(data, field));
}

// A DIC holds its count at 0x04, and from 0x08 its tree's 16-byte nodes, the root first, each pointing to its name at
// 0x08.
std::vector<std::string> DicNames(std::string_view data, uint64_t dic) {
    const uint32_t count = ReadAt<uint32_t>(data, dic + 0x04);
    if (count >= (data.size() - dic - 0x08) / 0x10) {
        throw std::runtime_error(std::format("a DIC counts {} names, more than the file holds", count));
    }
    std::vector<std::string> names;
    for (uint32_t i = 0; i < count; ++i) {
        names.push_back(Name(data, dic + 0x08 + 0x10 * (i + 1) + 0x08));
    }
    return names;
}

Param ReadParam(std::string_view data, uint64_t value) {
    Param param{ .type = ValueType(ReadAt<uint8_t>(data, value)) };
    const uint16_t count = ReadAt<uint16_t>(data, value + 0x02);
    if (count != (param.type == ValueType::FloatArray ? count : param.type == ValueType::Actor ? 2 : 1)) {
        throw std::runtime_error(std::format("a parameter of type {} holds {} values", int(param.type), count));
    }
    const uint64_t at = value + 0x10;
    switch (param.type) {
        case ValueType::Int:
        case ValueType::Bool:
        case ValueType::Float:
        case ValueType::FloatArray:
            for (uint16_t i = 0; i < count; ++i) {
                param.words.push_back(ReadAt<uint32_t>(data, at + 4 * i));
            }
            return param;
        case ValueType::Argument:
        case ValueType::String:
            param.text = Name(data, at);
            return param;
        case ValueType::Actor:
            param.text = Name(data, at);
            param.subName = Name(data, at + 8);
            return param;
    }
    throw std::runtime_error(
        std::format("a parameter of type {}, which no flowchart of the game holds", int(param.type)));
}

std::vector<Param> ReadParams(std::string_view data, uint64_t field) {
    const uint64_t container = ReadAt<uint64_t>(data, field);
    if (container == 0) {
        return {};
    }
    const std::vector<std::string> keys = DicNames(data, ReadAt<uint64_t>(data, container + 0x08));
    const uint16_t count = ReadAt<uint16_t>(data, container + 0x02);
    if (count == 0 || count != keys.size()) {
        throw std::runtime_error(
            std::format("a parameter container counts {} values and names {}", count, keys.size()));
    }
    std::vector<Param> params;
    for (size_t i = 0; i < keys.size(); ++i) {
        params.push_back(ReadParam(data, ReadAt<uint64_t>(data, container + 0x10 + 8 * i)));
        params.back().key = keys[i];
    }
    return params;
}

std::vector<std::string> Names(std::string_view data, uint64_t field, uint16_t count) {
    std::vector<std::string> names;
    for (uint16_t i = 0; i < count; ++i) {
        names.push_back(Name(data, ReadAt<uint64_t>(data, field) + 8 * i));
    }
    return names;
}

Actor ReadActor(std::string_view data, uint64_t actor) {
    return Actor{ .name = Name(data, actor),
                  .subName = Name(data, actor + 0x08),
                  .argumentName = Name(data, actor + 0x10),
                  .actions = Names(data, actor + 0x18, ReadAt<uint16_t>(data, actor + 0x30)),
                  .queries = Names(data, actor + 0x20, ReadAt<uint16_t>(data, actor + 0x32)),
                  .params = ReadParams(data, actor + 0x28),
                  .entryPoint = ReadAt<uint16_t>(data, actor + 0x34),
                  .clips = ReadAt<uint16_t>(data, actor + 0x36) };
}

std::vector<Case> ReadCases(std::string_view data, uint64_t field, uint16_t count) {
    std::vector<Case> cases;
    for (uint16_t i = 0; i < count; ++i) {
        const uint64_t at = ReadAt<uint64_t>(data, field) + 8 * i;
        cases.push_back({ .value = ReadAt<uint32_t>(data, at), .event = ReadAt<uint16_t>(data, at + 4) });
    }
    return cases;
}

Event ReadEvent(std::string_view data, uint64_t at) {
    Event event{ .name = Name(data, at), .type = EventType(ReadAt<uint8_t>(data, at + 0x08)) };
    const uint16_t field = ReadAt<uint16_t>(data, at + 0x0A);
    switch (event.type) {
        case EventType::Action:
        case EventType::Switch:
            event.actor = ReadAt<uint16_t>(data, at + 0x0C);
            event.verb = ReadAt<uint16_t>(data, at + 0x0E);
            event.params = ReadParams(data, at + 0x10);
            if (event.type == EventType::Action) {
                event.next = field;
            } else {
                event.cases = ReadCases(data, at + 0x18, field);
            }
            return event;
        case EventType::Fork:
            event.join = ReadAt<uint16_t>(data, at + 0x0C);
            for (uint16_t i = 0; i < field; ++i) {
                event.forks.push_back(ReadAt<uint16_t>(data, ReadAt<uint64_t>(data, at + 0x10) + 2 * i));
            }
            return event;
        case EventType::Join:
            event.next = field;
            return event;
        case EventType::Subflow:
            event.next = field;
            event.params = ReadParams(data, at + 0x10);
            event.flowchart = Name(data, at + 0x18);
            event.entry = Name(data, at + 0x20);
            return event;
        case EventType::SubflowSwitch:
            event.params = ReadParams(data, at + 0x10);
            event.cases = ReadCases(data, at + 0x18, field);
            event.flowchart = Name(data, at + 0x20);
            event.entry = Name(data, at + 0x28);
            return event;
        case EventType::Return:
            event.value = ReadAt<uint32_t>(data, at + 0x0C);
            return event;
    }
    throw std::runtime_error(std::format("{} has an event type the game's flowcharts do not use", event.name));
}

Entry ReadEntry(std::string_view data, uint64_t at, std::string name) {
    Entry entry{ .name = std::move(name), .main = ReadAt<uint16_t>(data, at + 0x1C) };
    for (uint16_t i = 0; i < ReadAt<uint16_t>(data, at + 0x18); ++i) {
        entry.calls.push_back(ReadAt<uint16_t>(data, ReadAt<uint64_t>(data, at) + 2 * i));
    }
    if (ReadAt<uint16_t>(data, at + 0x1A) != 0) {
        const std::vector<std::string> names = DicNames(data, ReadAt<uint64_t>(data, at + 0x08));
        for (size_t i = 0; i < names.size(); ++i) {
            const uint64_t variable = ReadAt<uint64_t>(data, at + 0x10) + 0x10 * i;
            const auto type = ValueType(ReadAt<uint8_t>(data, variable + 0x0A));
            if ((type != ValueType::Int && type != ValueType::Bool) || ReadAt<uint16_t>(data, variable + 0x08) != 1) {
                throw std::runtime_error(std::format("{} of {} is no integer or Boolean", names[i], entry.name));
            }
            entry.variables.push_back({ .name = names[i], .type = type, .value = ReadAt<int32_t>(data, variable) });
        }
    }
    return entry;
}

// Refuses an index outside its table. The next of an action, a join or a sub-flow call may also end its branch.
void CheckIndices(const Flowchart& flowchart) {
    const auto within = [](std::string_view owner, size_t index, size_t size, std::string_view table) {
        if (index >= size) {
            throw std::runtime_error(std::format("{} names {} {} of {}", owner, table, index, size));
        }
    };
    const size_t events = flowchart.events.size();
    for (const Event& event : flowchart.events) {
        if (event.type == EventType::Action || event.type == EventType::Switch) {
            within(event.name, event.actor, flowchart.actors.size(), "actor");
            const Actor& actor = flowchart.actors[event.actor];
            const bool action = event.type == EventType::Action;
            within(event.name, event.verb, (action ? actor.actions : actor.queries).size(),
                   action ? "action" : "query");
        }
        if (event.next != NoEvent) {
            within(event.name, event.next, events, "event");
        }
        for (const Case& c : event.cases) {
            within(event.name, c.event, events, "event");
        }
        for (const uint16_t fork : event.forks) {
            within(event.name, fork, events, "event");
        }
        if (event.type == EventType::Fork) {
            within(event.name, event.join, events, "event");
        }
    }
    for (const Entry& entry : flowchart.entries) {
        within(entry.name, entry.main, events, "event");
        for (const uint16_t call : entry.calls) {
            within(entry.name, call, events, "event");
        }
    }
}

struct Out {
    std::string data;
    std::set<uint64_t> pointers;
    std::map<std::string, std::vector<uint64_t>> strings{ { "", {} } };

    template <typename T>
    void Put(T value) {
        data.append(reinterpret_cast<const char*>(&value), sizeof value);
    }

    void Zeros(size_t size) {
        data.append(size, '\0');
    }

    void Align(size_t alignment) {
        data.resize(AlignUp(data.size(), alignment));
    }

    // The game's files list a null pointer field in the relocation table in some places and not in others.
    uint64_t Pointer(bool listed = true) {
        const uint64_t field = data.size();
        if (listed) {
            pointers.insert(field);
        }
        Zeros(8);
        return field;
    }

    void PointHere(uint64_t field) {
        WriteAt<uint64_t>(data, field, data.size());
    }

    void Ref(const std::string& text) {
        strings[text].push_back(Pointer());
    }

    void PutText(std::string_view text) {
        Put(uint16_t(text.size()));
        data += text;
        data += '\0';
    }

    void Dic(const std::vector<std::string>& names);
    void Params(const std::vector<Param>& params);
};

int Bit(std::string_view key, int i) {
    const size_t byte = size_t(i / 8);
    return i < 0 || byte >= key.size() ? 0 : uint8_t(key[key.size() - 1 - byte]) >> i % 8 & 1;
}

int LowestDifference(std::string_view a, std::string_view b) {
    for (int i = 0; i < 8 * int(std::max(a.size(), b.size())); ++i) {
        if (Bit(a, i) != Bit(b, i)) {
            return i;
        }
    }
    throw std::runtime_error(std::format("{} is a key of a DIC twice", a));
}

// A DIC is a radix tree over its keys' bits. The root holds the empty key and bit -1, and each name its node, in the
// order of the names. A node's two children are indices of nodes: down the tree to a node with a higher bit, back up it
// otherwise.
void Out::Dic(const std::vector<std::string>& names) {
    struct Node {
        std::string_view key;
        int bit;
        uint16_t child[2];
        uint16_t parent;
    };
    std::vector<Node> nodes{ { .key = "", .bit = -1, .child = { 0, 0 }, .parent = 0 } };
    for (const std::string& key : names) {
        uint16_t closest = 0;
        if (nodes[0].child[0] != 0) {
            uint16_t at = nodes[0].child[0];
            for (;;) {
                closest = at;
                at = nodes[at].child[Bit(key, nodes[at].bit)];
                if (nodes[at].bit <= nodes[closest].bit) {
                    break;
                }
            }
        }
        const int bit = LowestDifference(nodes[closest].key, key);
        while (bit < nodes[nodes[closest].parent].bit) {
            closest = nodes[closest].parent;
        }
        const auto added = uint16_t(nodes.size());
        Node node{ .key = key, .bit = bit, .child = { 0, 0 }, .parent = closest };
        if (bit < nodes[closest].bit) {
            const uint16_t parent = nodes[closest].parent;
            node.parent = parent;
            node.child[Bit(key, bit) ^ 1] = closest;
            node.child[Bit(key, bit)] = added;
            nodes[parent].child[Bit(key, nodes[parent].bit)] = added;
            nodes[closest].parent = added;
        } else if (bit > nodes[closest].bit) {
            node.child[Bit(key, bit)] = added;
            node.child[Bit(key, bit) ^ 1] = Bit(nodes[closest].key, bit) == (Bit(key, bit) ^ 1) ? closest : 0;
            nodes[closest].child[Bit(key, nodes[closest].bit)] = added;
        } else {
            const int side = Bit(key, bit);
            const uint16_t other = nodes[closest].child[side];
            node.bit = LowestDifference(nodes[other].key, key);
            node.child[Bit(key, node.bit)] = added;
            node.child[Bit(key, node.bit) ^ 1] = other;
            nodes[closest].child[side] = added;
        }
        nodes.push_back(node);
    }
    data += "DIC ";
    Put(uint32_t(names.size()));
    for (const Node& node : nodes) {
        Put(uint32_t(node.bit));
        Put(node.child[0]);
        Put(node.child[1]);
        Ref(std::string(node.key));
    }
}

void Out::Params(const std::vector<Param>& params) {
    Put(uint8_t(1));
    Put(uint8_t(0));
    Put(uint16_t(params.size()));
    Put(uint32_t(0));
    const uint64_t dic = Pointer();
    std::vector<uint64_t> fields;
    for (size_t i = 0; i < params.size(); ++i) {
        fields.push_back(Pointer());
    }
    PointHere(dic);
    std::vector<std::string> keys;
    for (const Param& param : params) {
        keys.push_back(param.key);
    }
    Dic(keys);
    for (size_t i = 0; i < params.size(); ++i) {
        const Param& param = params[i];
        Align(8);
        PointHere(fields[i]);
        Put(uint8_t(param.type));
        Put(uint8_t(0));
        Put(uint16_t(param.type == ValueType::FloatArray ? param.words.size()
                     : param.type == ValueType::Actor    ? 2
                                                         : 1));
        Put(uint32_t(0));
        Zeros(8);
        for (const uint32_t word : param.words) {
            Put(word);
        }
        if (param.type == ValueType::String || param.type == ValueType::Argument) {
            PointHere(Pointer());
            PutText(param.text);
        } else if (param.type == ValueType::Actor) {
            const uint64_t name = Pointer();
            const uint64_t subName = Pointer();
            PointHere(name);
            PutText(param.text);
            Align(2);
            PointHere(subName);
            PutText(param.subName);
        }
    }
}

struct EventFields {
    uint64_t params = 0;
    uint64_t cases = 0;
    uint64_t forks = 0;
};

EventFields WriteEvent(Out& out, const Event& event) {
    out.Ref(event.name);
    out.Put(uint8_t(event.type));
    out.Put(uint8_t(0));
    EventFields fields;
    switch (event.type) {
        case EventType::Action:
        case EventType::Switch:
            out.Put(event.type == EventType::Action ? event.next : uint16_t(event.cases.size()));
            out.Put(event.actor);
            out.Put(event.verb);
            fields.params = out.Pointer(!event.params.empty());
            if (event.type == EventType::Switch) {
                fields.cases = out.Pointer();
            } else {
                out.Zeros(8);
            }
            out.Zeros(16);
            break;
        case EventType::Fork:
            out.Put(uint16_t(event.forks.size()));
            out.Put(event.join);
            out.Put(uint16_t(0));
            fields.forks = out.Pointer();
            out.Zeros(24);
            break;
        case EventType::Join:
            out.Put(event.next);
            out.Zeros(36);
            break;
        case EventType::Subflow:
            out.Put(event.next);
            out.Zeros(4);
            fields.params = out.Pointer(!event.params.empty());
            out.Ref(event.flowchart);
            out.Ref(event.entry);
            out.Zeros(8);
            break;
        case EventType::SubflowSwitch:
            out.Put(uint16_t(event.cases.size()));
            out.Zeros(4);
            fields.params = out.Pointer(!event.params.empty());
            fields.cases = out.Pointer();
            out.Ref(event.flowchart);
            out.Ref(event.entry);
            break;
        case EventType::Return:
            out.Put(uint16_t(0));
            out.Put(event.value);
            out.Zeros(32);
            break;
    }
    return fields;
}

void WriteEventData(Out& out, const Event& event, const EventFields& fields) {
    if (!event.cases.empty()) {
        out.Align(8);
        out.PointHere(fields.cases);
        for (const Case& c : event.cases) {
            out.Put(c.value);
            out.Put(c.event);
            out.Align(8);
        }
    }
    if (event.type == EventType::Fork) {
        out.PointHere(fields.forks);
        for (const uint16_t fork : event.forks) {
            out.Put(fork);
        }
        out.Align(8);
    }
    if (!event.params.empty()) {
        out.PointHere(fields.params);
        out.Params(event.params);
    }
}

struct ActorFields {
    uint64_t actions;
    uint64_t queries;
    uint64_t params;
};

ActorFields WriteActor(Out& out, const Actor& actor) {
    out.Ref(actor.name);
    out.Ref(actor.subName);
    out.Ref(actor.argumentName);
    const ActorFields fields{ .actions = out.Pointer(),
                              .queries = out.Pointer(),
                              .params = out.Pointer(!actor.params.empty()) };
    out.Put(uint16_t(actor.actions.size()));
    out.Put(uint16_t(actor.queries.size()));
    out.Put(actor.entryPoint);
    out.Put(actor.clips);
    return fields;
}

void WriteActorData(Out& out, const Actor& actor, const ActorFields& fields) {
    if (!actor.params.empty()) {
        out.Align(8);
        out.PointHere(fields.params);
        out.Params(actor.params);
    }
    for (const auto& [field, names] :
         { std::pair{ fields.actions, &actor.actions }, { fields.queries, &actor.queries } }) {
        if (!names->empty()) {
            out.Align(8);
            out.PointHere(field);
            for (const std::string& name : *names) {
                out.Ref(name);
            }
        }
    }
}

struct EntryFields {
    uint64_t calls;
    uint64_t names = 0;
    uint64_t variables = 0;
};

void WriteFlowchart(Out& out, const Flowchart& flowchart) {
    const uint64_t start = out.data.size();
    out.data += "EVFL";
    const uint64_t pool = out.data.size();
    out.Zeros(12);
    out.Put(uint16_t(flowchart.actors.size()));
    uint16_t actions = 0;
    uint16_t queries = 0;
    for (const Actor& actor : flowchart.actors) {
        actions += uint16_t(actor.actions.size());
        queries += uint16_t(actor.queries.size());
    }
    out.Put(actions);
    out.Put(queries);
    out.Put(uint16_t(flowchart.events.size()));
    out.Put(uint16_t(flowchart.entries.size()));
    out.Zeros(6);
    out.Ref(flowchart.name);
    const uint64_t actorsField = out.Pointer();
    const uint64_t eventsField = out.Pointer();
    const uint64_t entryNamesField = out.Pointer();
    const uint64_t entriesField = out.Pointer();

    if (!flowchart.actors.empty()) {
        out.PointHere(actorsField);
    }
    std::vector<ActorFields> actorFields;
    for (const Actor& actor : flowchart.actors) {
        actorFields.push_back(WriteActor(out, actor));
    }
    if (!flowchart.events.empty()) {
        out.PointHere(eventsField);
    }
    std::vector<EventFields> eventFields;
    for (const Event& event : flowchart.events) {
        eventFields.push_back(WriteEvent(out, event));
    }
    out.PointHere(entryNamesField);
    std::vector<std::string> entryNames;
    for (const Entry& entry : flowchart.entries) {
        entryNames.push_back(entry.name);
    }
    out.Dic(entryNames);
    out.Align(8);
    if (!flowchart.entries.empty()) {
        out.PointHere(entriesField);
    }
    std::vector<EntryFields> entryFields;
    for (const Entry& entry : flowchart.entries) {
        EntryFields& fields = entryFields.emplace_back(EntryFields{ .calls = out.Pointer() });
        if (entry.variables.empty()) {
            out.Pointer(false);
            out.Pointer();
        } else {
            fields.names = out.Pointer();
            fields.variables = out.Pointer();
        }
        out.Put(uint16_t(entry.calls.size()));
        out.Put(uint16_t(entry.variables.size()));
        out.Put(entry.main);
        out.Put(uint16_t(0));
    }

    for (size_t i = 0; i < flowchart.events.size(); ++i) {
        out.Align(8);
        WriteEventData(out, flowchart.events[i], eventFields[i]);
    }
    for (size_t i = 0; i < flowchart.actors.size(); ++i) {
        out.Align(8);
        WriteActorData(out, flowchart.actors[i], actorFields[i]);
    }
    for (size_t i = 0; i < flowchart.entries.size(); ++i) {
        const Entry& entry = flowchart.entries[i];
        out.Align(8);
        if (!entry.calls.empty()) {
            out.PointHere(entryFields[i].calls);
            for (const uint16_t call : entry.calls) {
                out.Put(call);
            }
            out.Align(8);
        }
        if (entry.variables.empty()) {
            // The room the game's files leave for an empty DIC of variable names.
            out.Zeros(0x18);
            continue;
        }
        out.PointHere(entryFields[i].names);
        std::vector<std::string> names;
        for (const Variable& variable : entry.variables) {
            names.push_back(variable.name);
        }
        out.Dic(names);
        out.PointHere(entryFields[i].variables);
        for (const Variable& variable : entry.variables) {
            out.Put(variable.value);
            out.Put(uint32_t(0));
            out.Put(uint16_t(1));
            out.Put(uint8_t(variable.type));
            out.Zeros(5);
        }
        out.Align(8);
    }
    out.Align(8);
    WriteAt<uint32_t>(out.data, pool, uint32_t(out.data.size() - start));
}

std::string PoolOrder(std::string_view text) {
    std::string bits;
    for (auto byte = text.rbegin(); byte != text.rend(); ++byte) {
        for (int i = 0; i < 8; ++i) {
            bits += (uint8_t(*byte) >> i & 1) != 0 ? '1' : '0';
        }
    }
    bits.erase(bits.find_last_of('1') + 1);
    return bits.empty() ? "0" : bits;
}

Entry& EntryNamed(Flowchart& flowchart, std::string_view name) {
    const auto entry = std::ranges::find(flowchart.entries, name, &Entry::name);
    if (entry == flowchart.entries.end()) {
        throw std::runtime_error(std::format("no entry point in the flow is named {}", name));
    }
    return *entry;
}

Param& ParamOf(Flowchart& flowchart, std::string_view event, std::string_view key) {
    Event& found = flowchart.events[EventIndex(flowchart, event)];
    const auto param = std::ranges::find(found.params, key, &Param::key);
    if (param != found.params.end()) {
        return *param;
    }
    throw std::runtime_error("the event has no parameter with this name");
}

} // namespace

Flowchart Parse(std::string_view data) {
    if (!data.starts_with(Magic) || ReadAt<uint16_t>(data, 0x08) != Version ||
        ReadAt<uint16_t>(data, 0x0C) != LittleEndian) {
        throw std::runtime_error("not a little-endian BFEVFL version 4 file");
    }
    if (ReadAt<uint16_t>(data, FlowchartCount) == 0) {
        throw std::runtime_error("the file is a timeline, which has no events");
    }
    if (ReadAt<uint16_t>(data, FlowchartCount) != 1 || ReadAt<uint16_t>(data, TimelineCount) != 0) {
        throw std::runtime_error("the file holds more than one flowchart");
    }
    const uint64_t chart = ReadAt<uint64_t>(data, ReadAt<uint64_t>(data, Flowcharts));
    Flowchart flowchart{ .name = Name(data, chart + 0x20) };
    for (uint16_t i = 0; i < ReadAt<uint16_t>(data, chart + 0x10); ++i) {
        flowchart.actors.push_back(ReadActor(data, ReadAt<uint64_t>(data, chart + 0x28) + ActorSize * i));
    }
    for (uint16_t i = 0; i < ReadAt<uint16_t>(data, chart + 0x16); ++i) {
        flowchart.events.push_back(ReadEvent(data, ReadAt<uint64_t>(data, chart + 0x30) + EventSize * i));
    }
    std::vector<std::string> names = DicNames(data, ReadAt<uint64_t>(data, chart + 0x38));
    for (size_t i = 0; i < names.size(); ++i) {
        flowchart.entries.push_back(
            ReadEntry(data, ReadAt<uint64_t>(data, chart + 0x40) + EntrySize * i, std::move(names[i])));
    }
    CheckIndices(flowchart);
    return flowchart;
}

std::string Write(const Flowchart& flowchart) {
    // The header: the magic, the version, the byte order mark and an alignment of 2^3, the offset of the file's name,
    // the flowchart's offset at 0x16, the relocation table's at 0x18, the file's size at 0x1C, one flowchart and no
    // timeline, then pointers to the flowchart's pointer, a DIC of its name, the timeline's pointer and its DIC.
    Out out;
    out.data += Magic;
    out.Put(Version);
    out.Zeros(2);
    out.Put(LittleEndian);
    out.Put(uint8_t(3));
    out.Put(uint8_t(0));
    const uint64_t nameField = out.data.size();
    out.Zeros(6);
    const uint64_t flowchartOffset = out.data.size();
    out.Zeros(2);
    const uint64_t relocationField = out.data.size();
    out.Zeros(4);
    const uint64_t sizeField = out.data.size();
    out.Zeros(4);
    out.Put(uint16_t(1));
    out.Put(uint16_t(0));
    out.Zeros(4);
    const uint64_t flowcharts = out.Pointer();
    const uint64_t flowchartNames = out.Pointer();
    out.Pointer();
    const uint64_t timelineNames = out.Pointer();
    out.PointHere(flowcharts);
    const uint64_t flowchartField = out.Pointer();
    out.PointHere(flowchartNames);
    out.Dic({ flowchart.name });
    out.PointHere(timelineNames);
    out.Dic({});
    WriteAt<uint16_t>(out.data, flowchartOffset, uint16_t(out.data.size()));
    out.PointHere(flowchartField);
    WriteFlowchart(out, flowchart);

    // The string pool: "STR ", its count without the empty string, which comes first, then each string aligned to 2.
    // The header points past the file name's length to its text.
    out.Align(8);
    out.data += "STR ";
    out.Zeros(12);
    out.Put(uint32_t(out.strings.size() - 1));
    std::vector<std::pair<std::string, const std::string*>> order;
    for (const std::string& text : std::views::keys(out.strings)) {
        order.emplace_back(PoolOrder(text), &text);
    }
    std::ranges::sort(order);
    for (const auto& [_, text] : order) {
        if (*text == flowchart.name) {
            WriteAt<uint32_t>(out.data, nameField, uint32_t(out.data.size() + 2));
        }
        for (const uint64_t field : out.strings.at(*text)) {
            WriteAt<uint64_t>(out.data, field, out.data.size());
        }
        out.PutText(*text);
        out.Align(2);
    }
    const uint64_t dataEnd = out.data.size();

    out.Align(8);
    const uint64_t table = out.data.size();
    out.data += "RELT";
    out.Put(uint32_t(table));
    out.Put(uint32_t(1));
    out.Zeros(16);
    out.Put(uint32_t(dataEnd));
    out.Put(uint32_t(0));
    std::vector<std::pair<uint32_t, uint32_t>> entries;
    for (const uint64_t field : out.pointers) {
        if (entries.empty() || field >= entries.back().first + 8 * PointersPerEntry) {
            entries.emplace_back(uint32_t(field), 0);
        }
        entries.back().second |= 1u << (field - entries.back().first) / 8;
    }
    out.Put(uint32_t(entries.size()));
    for (const auto& [base, mask] : entries) {
        out.Put(base);
        out.Put(mask);
    }
    WriteAt<uint32_t>(out.data, relocationField, uint32_t(table));
    WriteAt<uint32_t>(out.data, sizeField, uint32_t(out.data.size()));
    return std::move(out.data);
}

uint16_t EventIndex(const Flowchart& flowchart, std::string_view name) {
    const auto event = std::ranges::find(flowchart.events, name, &Event::name);
    if (event == flowchart.events.end()) {
        throw std::runtime_error(std::format("no event in the flow is named {}", name));
    }
    return uint16_t(event - flowchart.events.begin());
}

bool SetString(Flowchart& flowchart, std::string_view event, std::string_view param, std::string_view value) {
    Param& found = ParamOf(flowchart, event, param);
    if (found.type != ValueType::String) {
        throw std::runtime_error("the parameter is not a string");
    }
    if (found.text == value) {
        return false;
    }
    found.text = value;
    return true;
}

bool SetInt(Flowchart& flowchart, std::string_view event, std::string_view param, int32_t value) {
    Param& found = ParamOf(flowchart, event, param);
    if (found.type != ValueType::Int) {
        throw std::runtime_error("the parameter is not an integer");
    }
    if (found.words[0] == uint32_t(value)) {
        return false;
    }
    found.words[0] = uint32_t(value);
    return true;
}

bool SetNext(Flowchart& flowchart, std::string_view event, std::optional<std::string_view> next) {
    const uint16_t target = next ? EventIndex(flowchart, *next) : NoEvent;
    Event& sent = flowchart.events[EventIndex(flowchart, event)];
    std::vector<uint16_t*> fields;
    switch (sent.type) {
        case EventType::Action:
        case EventType::Join:
        case EventType::Subflow:
            fields.push_back(&sent.next);
            break;
        case EventType::Switch:
        case EventType::SubflowSwitch:
            if (!next) {
                throw std::runtime_error(
                    std::format("{} is a switch, and no switch case in the game's flows ends its branch", event));
            }
            for (Case& c : sent.cases) {
                fields.push_back(&c.event);
            }
            break;
        case EventType::Fork:
        case EventType::Return:
            throw std::runtime_error(std::format("{} is a fork or a return, which has no next event", event));
    }
    bool changed = false;
    for (uint16_t* field : fields) {
        changed |= *field != target;
        *field = target;
    }
    return changed;
}

bool SetEntry(Flowchart& flowchart, std::string_view entry, std::string_view event) {
    const uint16_t target = EventIndex(flowchart, event);
    Entry& found = EntryNamed(flowchart, entry);
    const bool changed = found.main != target;
    found.main = target;
    return changed;
}

} // namespace bfevfl
