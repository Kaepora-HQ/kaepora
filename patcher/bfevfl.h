// BFEVFL version 4, the game's event flows, as far as the patcher changes them. The file stays the bytes it was read
// from, with the place of every pointer field, so a string can change length and everything after it moves.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace bfevfl {

struct Flow {
    std::string data;               // the file up to the end of its string pool, where its relocation table's data ends
    std::vector<uint32_t> pointers; // the offset of every pointer field the relocation table lists, in order
};

struct Query {
    std::string actor;
    std::string name;
};

Flow Parse(std::string_view data);

// The query the named event asks, or nothing when the event is no switch. Throws when the flow has no such event.
std::optional<Query> QueryOf(const Flow& flow, std::string_view event);

// Sets the string parameter of the named event to the value. Returns whether it changed. Throws, saying what is
// missing, when the flow has no such event or parameter, or the parameter holds no string.
bool SetString(Flow& flow, std::string_view event, std::string_view param, std::string_view value);

// Sets the integer parameter of the named event to the value. Returns whether it changed. Throws, saying what is
// missing, when the flow has no such event or parameter, or the parameter holds no integer.
bool SetInt(Flow& flow, std::string_view event, std::string_view param, int32_t value);

// Sends the named event on to the next one: an action, a join or a sub-flow call goes there when it ends, and a switch
// goes there on every case. With none, an action, a join or a sub-flow call ends its branch. Returns whether it
// changed. Throws, saying what is wrong, when the flow has no event of either name, the first is a fork or a return, or
// a switch is given none, as no case of a switch in the game's flows ends its branch. Each entry point lists the
// sub-flow calls it can reach, and those lists stay as they are: a change may take a call out of an entry point's
// reach, but must not bring into reach a call the entry point does not list.
bool SetNext(Flow& flow, std::string_view event, std::optional<std::string_view> next);

// Starts the named entry point at the event. Returns whether it changed. Throws, saying what is missing, when the flow
// has no entry point or event of that name. The entry point's list of the sub-flow calls it can reach stays as it is:
// a change may take a call out of its reach, but must not bring into reach a call the list leaves out.
bool SetEntry(Flow& flow, std::string_view entryPoint, std::string_view event);

// The file with its relocation table, laid out as the game's own files are.
std::string Write(const Flow& flow);

} // namespace bfevfl
