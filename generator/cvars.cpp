// Nothing the generator runs sets a console variable, so each getter returns the caller's default, as with a fresh SoH
// configuration file.
#include <libultraship/bridge/consolevariablebridge.h>

extern "C" int32_t CVarGetInteger(const char*, int32_t defaultValue) {
    return defaultValue;
}

extern "C" const char* CVarGetString(const char*, const char* defaultValue) {
    return defaultValue;
}
