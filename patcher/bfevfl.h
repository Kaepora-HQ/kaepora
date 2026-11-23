// BFEVFL version 4, the game's event flows: a flowchart as its actors, events and entry points. A flowchart the game
// holds, read and written back, gives the bytes it was read from. The other kind of event flow, a timeline, is refused,
// as the patcher changes none, and so is a file the model cannot hold.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace bfevfl {

// The index an action's, a join's or a sub-flow call's next holds to end its branch. No other event index of the game's
// flowcharts holds it.
constexpr uint16_t NoEvent = 0xFFFF;

enum class ValueType : uint8_t { Argument = 0, Int = 2, Bool = 3, Float = 4, String = 5, FloatArray = 9, Actor = 12 };

// An integer, a Boolean or a float holds one word with its bits, and a float array a word for each float. An argument
// holds the name of the value its caller passes, a string its text, and an actor its name and its sub-name.
struct Param {
    std::string key;
    ValueType type;
    std::vector<uint32_t> words;
    std::string text;
    std::string subName;
};

struct Actor {
    std::string name;
    std::string subName;
    std::string argumentName;
    std::vector<std::string> actions;
    std::vector<std::string> queries;
    std::vector<Param> params; // empty for none, as no flowchart of the game holds an empty container
    uint16_t entryPoint;       // two fields the patcher keeps as the file holds them
    uint16_t clips;
};

enum class EventType : uint8_t { Action, Switch, Fork, Join, Subflow, SubflowSwitch, Return };

struct Case {
    uint32_t value;
    uint16_t event;
};

struct Event {
    std::string name;
    EventType type;
    uint16_t next = NoEvent; // an action, a join or a sub-flow call: the event after it
    uint16_t actor = 0;      // an action or a switch: the actor, and the index of its action or query
    uint16_t verb = 0;
    std::vector<Param> params;   // an action, a switch, a sub-flow call or a sub-flow switch, as for an actor
    std::vector<Case> cases;     // a switch or a sub-flow switch
    std::vector<uint16_t> forks; // a fork: the branches it starts, and the join they meet at
    uint16_t join = NoEvent;
    std::string flowchart; // a sub-flow call or a sub-flow switch: the flow it calls, empty for its own, and the entry
    std::string entry;
    uint32_t value = 0; // a return
};

// A variable of an entry point: an integer or a Boolean, the only kinds the game's flowcharts give one.
struct Variable {
    std::string name;
    ValueType type;
    int32_t value;
};

struct Entry {
    std::string name;
    uint16_t main;
    std::vector<uint16_t> calls; // the sub-flow calls the file lists for the entry point
    std::vector<Variable> variables;
};

struct Flowchart {
    std::string name; // the file's and the flowchart's, which the game's files keep equal
    std::vector<Actor> actors;
    std::vector<Event> events;
    std::vector<Entry> entries;
};

// Throws when the file is a timeline, or holds anything the model does not: more than its one flowchart, a value of a
// type or count no flowchart of the game holds, an empty container, a variable other than one integer or one Boolean,
// or an index outside its table.
Flowchart Parse(std::string_view data);

// Lays the file out as the game's own flowcharts are, with each entry point's calls as listed.
std::string Write(const Flowchart& flowchart);

// The index of the event with the name. Throws when the flowchart has none.
uint16_t EventIndex(const Flowchart& flowchart, std::string_view name);

// Sets the string parameter of the named event to the value. Returns whether it changed. Throws, saying what is
// missing, when the flowchart has no such event or parameter, or the parameter holds no string.
bool SetString(Flowchart& flowchart, std::string_view event, std::string_view param, std::string_view value);

// Sets the integer parameter of the named event to the value. Returns whether it changed. Throws, saying what is
// missing, when the flowchart has no such event or parameter, or the parameter holds no integer.
bool SetInt(Flowchart& flowchart, std::string_view event, std::string_view param, int32_t value);

// Sends the named event on to the next one: an action, a join or a sub-flow call goes there when it ends, and a switch
// goes there on every case. With none, an action, a join or a sub-flow call ends its branch. Returns whether it
// changed. Throws, saying what is wrong, when the flowchart has no event of either name, the first is a fork or a
// return, or a switch is given none, as no case of a switch in the game's flows ends its branch. Each entry point's
// list of calls stays as the file holds it.
bool SetNext(Flowchart& flowchart, std::string_view event, std::optional<std::string_view> next);

// Starts the named entry point at the event. Returns whether it changed. Throws, saying what is missing, when the
// flowchart has no entry point or event of that name. The entry point's list of calls stays as it is, as for SetNext.
bool SetEntry(Flowchart& flowchart, std::string_view entry, std::string_view event);

} // namespace bfevfl
