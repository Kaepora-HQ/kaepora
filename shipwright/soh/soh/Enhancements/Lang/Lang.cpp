#include <memory>

#include <libultraship/bridge/consolevariablebridge.h>

#include "Lang.h"
#include "soh/cvar_prefixes.h"
#include "soh/util.h"
#include "spdlog/spdlog.h"

static bool initialized = false;
static std::unordered_map<std::string, nlohmann::json> langs;

static std::unordered_map<std::string, std::string> cache;

#define LANGUAGE_CVAR CVAR_SETTING("Language")
#define DEFAULT_LANGUAGE "en_US"

std::variant<std::reference_wrapper<const std::string>, Lang::Error> Lang::TryTranslate(const char* path) {
    if (!initialized) {
        SPDLOG_ERROR("Tried to obtain a translation before the translation data is initialized");
        assert(false);
        return Lang::Error::LanguageDataNotInitialized;
    }

    std::string currentLang = CVarGetString(LANGUAGE_CVAR, DEFAULT_LANGUAGE);

    if (!langs.contains(currentLang)) {
        SPDLOG_WARN("Current language ({}) doesn't exist, trying to fall back to default language ({})",
                    currentLang.c_str(), DEFAULT_LANGUAGE);

        currentLang = DEFAULT_LANGUAGE;

        if (!langs.contains(currentLang)) {
            SPDLOG_ERROR("Default language ({}) doesn't exist", DEFAULT_LANGUAGE);
            assert(false);
            return Lang::Error::LanguageDataNotFound;
        }

        CVarSetString(LANGUAGE_CVAR, DEFAULT_LANGUAGE);
        SPDLOG_WARN("Fallback to default language ({}) was succesful", DEFAULT_LANGUAGE);
    }

    std::string stringPath = std::string(path);

    if (cache.contains(stringPath)) {
        return cache[stringPath];
    }

    const nlohmann::json* currentLangData = &langs.at(currentLang);

    std::vector<std::string> segments = SohUtils::StringSplit(stringPath, ".");

    std::string lastSegment = segments[segments.size() - 1];

    segments.pop_back();

    for (const auto& segment : segments) {
        if (!currentLangData->contains(segment)) {
            return Lang::Error::PathDataNotFound;
        }

        currentLangData = &currentLangData->at(segment);
    }

    if (!currentLangData->contains(lastSegment)) {
        return Lang::Error::PathDataNotFound;
    }

    if (currentLangData->at(lastSegment).is_string()) {
        const std::string& translatedString = currentLangData->at(lastSegment).get_ref<const std::string&>();
        cache[stringPath] = translatedString;
        return cache[stringPath];
    }

    if (currentLangData->at(lastSegment).is_array()) {
        std::string translatedString = "";

        for (const auto& item : currentLangData->at(lastSegment)) {
            if (!item.is_string()) {
                return Lang::Error::PathInvalidValue;
            }

            translatedString += item.get_ref<const std::string&>();
        }

        cache[stringPath] = translatedString;
        return cache[stringPath];
    }

    return Lang::Error::PathInvalidValue;
}

const std::string errorMessage =
    "[ERROR] Couldn't retrieve language data for this item, check the log for more information.";

const std::string& Lang::Translate(const char* path) {
    auto value = Lang::TryTranslate(path);

    if (std::holds_alternative<std::reference_wrapper<const std::string>>(value)) {
        return std::get<std::reference_wrapper<const std::string>>(value);
    } else if (std::holds_alternative<Lang::Error>(value)) {
        switch (std::get<Lang::Error>(value)) {
            case Lang::Error::PathDataNotFound:
                SPDLOG_WARN("Current language ({}) doesn't have data for the requested path ({})",
                            CVarGetString(LANGUAGE_CVAR, DEFAULT_LANGUAGE), path);
                break;
            case Lang::Error::PathInvalidValue:
                SPDLOG_WARN("Current language ({}) has an array with a non-string at the requested path ({})",
                            CVarGetString(LANGUAGE_CVAR, DEFAULT_LANGUAGE), path);
                break;
            default:
                break;
        }

        return errorMessage;
    } else {
        assert(false);
        return errorMessage;
    }
}

void Lang::LoadLangs() {
    static const unsigned char enUS[] = {
#embed "../../../assets/custom/lang/en_US.json"
    };
    langs.insert_or_assign("en_US", nlohmann::json::parse(std::begin(enUS), std::end(enUS), nullptr, true, true));
    initialized = true;
}
