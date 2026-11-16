#include <libultraship/bridge/consolevariablebridge.h>

#include "option.h"
#include "randomizerEnumStrings.h"
#include "static_data.h"
#include "soh/Enhancements/Lang/Lang.h"
#include <soh/cvar_prefixes.h>
#include <spdlog/spdlog.h>

namespace Rando {
Option Option::Bool(RandomizerSettingKey key_, std::vector<std::string> options_, const OptionCategory category_,
                    std::string cvarName_, WidgetType widgetType_, const uint8_t defaultOption_,
                    const bool defaultHidden_, WidgetFunc callback_, int imFlags_) {
    return { static_cast<size_t>(key_),
             std::move(options_),
             category_,
             std::move(cvarName_),
             widgetType_,
             defaultOption_,
             defaultHidden_,
             callback_,
             imFlags_ };
}

Option Option::Bool(RandomizerSettingKey key_, std::string cvarName_, const int imFlags_, const WidgetType widgetType_,
                    const bool defaultOption_, WidgetFunc callback_) {
    return Option(key_, { "Off", "On" }, OptionCategory::Setting, std::move(cvarName_), widgetType_, defaultOption_,
                  false, callback_, imFlags_);
}

Option Option::U8(RandomizerSettingKey key_, std::vector<std::string> options_, const OptionCategory category_,
                  std::string cvarName_, WidgetType widgetType_, const uint8_t defaultOption_,
                  const bool defaultHidden_, WidgetFunc callback_, int imFlags_) {
    return { static_cast<size_t>(key_),
             std::move(options_),
             category_,
             std::move(cvarName_),
             widgetType_,
             defaultOption_,
             defaultHidden_,
             callback_,
             imFlags_ };
}

OptionValue::OptionValue(uint8_t val) : mVal(val) {
}

uint8_t OptionValue::Get() {
    return mVal;
}

void OptionValue::Set(uint8_t val) {
    mVal = val;
}

OptionValue::operator bool() const {
    return mVal != 0;
}

size_t Option::GetOptionCount() const {
    return options.size();
}

RandomizerSettingKey Option::GetKey() const {
    return static_cast<RandomizerSettingKey>(key);
}

#pragma region Lang

const static std::string namePostfix = ".name";

const static std::string settingPrefix = "randomizer.settings.";
const static std::string trickPrefix = "randomizer.tricks.";

const static std::string empty = "";
const static std::string error = "[ERROR]";

static const std::string& MakeSettingName(RandomizerSettingKey key) {
    std::string settingNamePart(EnumToString(key).substr(4));
    std::transform(settingNamePart.begin(), settingNamePart.end(), settingNamePart.begin(), ::tolower);
    return Lang::Translate((settingPrefix + settingNamePart + namePostfix).c_str());
}

static const std::string& MakeTrickName(RandomizerTrick key) {
    std::string trickNamePart(EnumToString(key).substr(3));
    std::transform(trickNamePart.begin(), trickNamePart.end(), trickNamePart.begin(), ::tolower);
    return Lang::Translate((trickPrefix + trickNamePart + namePostfix).c_str());
}

#pragma endregion

const std::string& Option::GetName() const {
    switch (this->GetCategory()) {
        case OptionCategory::Setting:
        case OptionCategory::Toggle:
            return MakeSettingName(static_cast<RandomizerSettingKey>(this->key));
        case OptionCategory::Trick:
            return MakeTrickName(static_cast<RandomizerTrick>(this->key));
        case OptionCategory::LocationExclusion:
            return StaticData::GetLocation(static_cast<RandomizerCheck>(this->key))->GetName();
        default:
            assert(false);
            return error;
    }
}

uint8_t Option::GetOptionIndex() const {
    return CVarGetInteger(cvarName.c_str(), defaultOption);
}

uint8_t Option::GetMenuOptionDefault() const {
    return defaultOption;
}

const std::string& Option::GetOptionText(size_t index) const {
    if (options.empty()) {
        return empty;
    }
    if (index >= options.size()) {
        index = options.size() - 1;
    }
    return options[index];
}

const std::string& Option::GetCVarName() const {
    return cvarName;
}

void Option::SetContextIndex(uint8_t idx) {
    // TODO: Set to Context's OptionValue array.
    contextSelection = idx;
    if (contextSelection > static_cast<uint8_t>(options.size() - 1)) {
        contextSelection = static_cast<uint8_t>(options.size() - 1);
    }
}

OptionCategory Option::GetCategory() const {
    return this->category;
}

uint8_t Option::GetValueFromText(const std::string text) {
    if (optionsTextToVar.contains(text)) {
        return optionsTextToVar[text];
    } else {
        SPDLOG_ERROR("Option {} does not have a var named {}.", this->GetName(), text);
        assert(false);
    }
    return defaultOption;
}

Option::Option(size_t key_, std::vector<std::string> options_, OptionCategory category_, std::string cvarName_,
               WidgetType widgetType_, uint8_t defaultOption_, bool defaultHidden_, WidgetFunc callback_, int imFlags_)
    : key(key_), options(std::move(options_)), category(category_), cvarName(std::move(cvarName_)),
      defaultOption(defaultOption_) {
    contextSelection = defaultOption;
    PopulateTextToNum();
}

void Option::PopulateTextToNum() {
    for (uint8_t count = 0; count < options.size(); count++) {
        optionsTextToVar[options[count]] = count;
    }
}

LocationOption::LocationOption(RandomizerCheck key_)
    : Option(key_, { "Included", "Excluded" }, OptionCategory::LocationExclusion, "", WIDGET_CVAR_CHECKBOX,
             RO_LOCATION_INCLUDE, false, nullptr, IMFLAG_NONE) {
}

TrickSetting::TrickSetting(RandomizerTrick key_, const RandomizerCheckQuest quest_, const RandomizerArea area_,
                           std::set<Tricks::Tag> tags_, const std::string nameTag_)
    : Option(key_, { "Disabled", "Enabled" }, OptionCategory::Trick, "", WIDGET_CVAR_CHECKBOX, 0, false, nullptr,
             IMFLAG_NONE),
      mQuest(quest_), mArea(area_), mNameTag(nameTag_), mTags(std::move(tags_)) {
}

TrickSetting TrickSetting::LogicTrick(RandomizerTrick key_, RandomizerCheckQuest quest_, RandomizerArea area_,
                                      std::set<Tricks::Tag> tags_, const std::string nameTag_) {
    return { key_, quest_, area_, std::move(tags_), nameTag_ };
}

RandomizerTrick TrickSetting::GetKey() const {
    return static_cast<RandomizerTrick>(key);
}

RandomizerCheckQuest TrickSetting::GetQuest() const {
    return mQuest;
}

RandomizerArea TrickSetting::GetArea() const {
    return mArea;
}

std::string TrickSetting::GetNameTag() const {
    return mNameTag;
}

bool TrickSetting::HasTag(const Tricks::Tag tag) const {
    return mTags.contains(tag);
}

const std::set<Tricks::Tag>& TrickSetting::GetTags() const {
    return mTags;
}

OptionGroup::OptionGroup(std::string name, std::vector<Option*> options, const OptionGroupType groupType,
                         const WidgetContainerType containerType, std::string description)
    : mName(std::move(name)), mOptions(std::move(options)), mGroupType(groupType), mContainerType(containerType),
      mDescription(std::move(description)) {
}

OptionGroup::OptionGroup(std::string name, std::vector<OptionGroup*> subGroups, const OptionGroupType groupType,
                         const WidgetContainerType containerType, std::string description)
    : mName(std::move(name)), mSubGroups(std::move(subGroups)), mGroupType(groupType),
      mContainsType(OptionGroupType::SUBGROUP), mContainerType(containerType), mDescription(std::move(description)) {
}

OptionGroup OptionGroup::SubGroup(std::string name, std::vector<Option*> options,
                                  const WidgetContainerType containerType, std::string description) {
    return { std::move(name), std::move(options), OptionGroupType::SUBGROUP, containerType, std::move(description) };
}

OptionGroup OptionGroup::SubGroup(std::string name, std::vector<OptionGroup*> subGroups,
                                  const WidgetContainerType containerType, std::string description) {
    return { std::move(name), std::move(subGroups), OptionGroupType::SUBGROUP, containerType, std::move(description) };
}

const std::string& OptionGroup::GetName() const {
    return mName;
}

const std::vector<Option*>& OptionGroup::GetOptions() const {
    return mOptions;
}

const std::vector<OptionGroup*>& OptionGroup::GetSubGroups() const {
    return mSubGroups;
}

OptionGroupType OptionGroup::GetGroupType() const {
    return mGroupType;
}

OptionGroupType OptionGroup::GetContainsType() const {
    return mContainsType;
}

WidgetContainerType OptionGroup::GetContainerType() const {
    return mContainerType;
}

const std::string& OptionGroup::GetDescription() const {
    return mDescription;
}

void OptionGroup::Enable() {
    mDisabled = false;
}

void OptionGroup::Disable() {
    mDisabled = true;
}
} // namespace Rando