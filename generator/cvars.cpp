// The console variables start empty, as in a fresh SoH configuration file.
#include <libultraship/bridge/consolevariablebridge.h>

#include <map>
#include <string>
#include <variant>

static std::map<std::string, std::variant<int32_t, std::string>> cvars;

extern "C" int32_t CVarGetInteger(const char* name, int32_t defaultValue) {
    auto it = cvars.find(name);
    if (it == cvars.end() || !std::holds_alternative<int32_t>(it->second)) {
        return defaultValue;
    }
    return std::get<int32_t>(it->second);
}

extern "C" const char* CVarGetString(const char* name, const char* defaultValue) {
    auto it = cvars.find(name);
    if (it == cvars.end() || !std::holds_alternative<std::string>(it->second)) {
        return defaultValue;
    }
    return std::get<std::string>(it->second).c_str();
}

extern "C" void CVarSetInteger(const char* name, int32_t value) {
    cvars[name] = value;
}

extern "C" void CVarSetString(const char* name, const char* value) {
    cvars[name] = std::string(value);
}
