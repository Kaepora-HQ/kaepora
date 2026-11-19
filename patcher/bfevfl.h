// BFEVFL version 4, the game's event flows, as far as the patcher changes them: a string or integer parameter of a
// flowchart's event, and the event one goes on to. The file stays the bytes it was read from, with the place of every
// pointer field, so a string can change length and everything after it moves.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace bfevfl {

struct Flow {
    std::string data;               // the file up to the end of its string pool, where its relocation table's data ends
    std::vector<uint32_t> pointers; // the offset of every pointer field the relocation table lists, in order
};

Flow Parse(std::string_view data);

// Sets the string parameter of the named event to the value. Returns whether it changed. Throws, saying what is
// missing, when the flow has no such event or parameter, or the parameter holds no string.
bool SetString(Flow& flow, std::string_view event, std::string_view param, std::string_view value);

// Sets the integer parameter of the named event to the value. Returns whether it changed. Throws, saying what is
// missing, when the flow has no such event or parameter, or the parameter holds no integer.
bool SetInt(Flow& flow, std::string_view event, std::string_view param, int32_t value);

// Sends the named event on to the next one: an action, a join or a sub-flow call goes there when it ends, and a switch
// goes there on every case. Returns whether it changed. Throws, saying what is missing, when the flow has no event of
// either name, or the first is a fork or a return. Each entry point lists the sub-flow calls it can reach, and those
// lists stay as they are: a change may take a call out of an entry point's reach, but must not bring into reach a
// call the entry point does not list.
bool SetNext(Flow& flow, std::string_view event, std::string_view next);

// The file with its relocation table, laid out as the game's own files are.
std::string Write(const Flow& flow);

} // namespace bfevfl
