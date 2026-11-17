#pragma once

#include "SeedContext.h"
#include "option.h"

#include <array>
#include <unordered_map>

namespace Rando {
class Settings {
  public:
    /**
     * @brief Creates the `Option` objects. This happens after construction because certain
     * other events in the codebase need to happen before all of the `Option`s can be created.
     */
    void CreateOptions();

    /**
     * @brief Populates the map used to translate strings into RandomiserSettingKeys
     *
     * @return std::unordered_map<std::string, RandomizerSettingKey>
     */

    std::unordered_map<std::string, RandomizerSettingKey> PopulateOptionNameToEnum();

    /**
     * @brief Get a reference to the `Option` corresponding to the provided RandomizerSettingKey.
     *
     * @param key
     * @return Option&
     */
    Option& GetOption(RandomizerSettingKey key);

    /**
     * @brief Get a reference to the `Option` corresponding to the provided RandomizerTrick key.
     *
     * @param key
     * @return Option&
     */
    TrickSetting& GetTrickSetting(RandomizerTrick key);

    /**
     * @brief Get the RandomizerTrick corresponding to the provided name.
     *
     * @param name
     * @return int RandomizerTrick index or -1 if not found
     */
    int GetRandomizerTrickByName(const std::string& name);

    /**
     * @brief Get a list of Location Exclude `Option`s for the given
     * RandomizerCheckArea
     *
     * @param group
     * @return std::vector<Option*>&
     */
    std::vector<Option*>& GetExcludeOptionsForArea(RandomizerCheckArea area);

    /**
     * @brief Parse Options from a JSON file.
     *
     * @param spoilerFileJson
     */
    void ParseJson(const nlohmann::json& spoilerFileJson);

    /**
     * @brief Assigns a Rando::Context instance to this settings instance
     * with which to later instantiate and access OptionValues.
     *
     * @param ctx
     */
    void AssignContext(std::shared_ptr<Context> ctx);
    void ClearContext();

    /**
     * @brief Sets all the currently selected MenuIndexes to the currently assigned Rando::Context.
     */
    void SetAllToContext();

    /**
     * @brief Randomizes all randomizer settings (excluding tricks) to random valid values.
     * This function iterates through all options and sets them to a random index within
     * their valid range.
     */
    void RandomizeAllSettings();

    static std::shared_ptr<Settings> GetInstance();

  private:
    static std::shared_ptr<Settings> mInstance;
    std::shared_ptr<Context> mContext = nullptr;
    std::array<Option, RSK_MAX> mOptions = {};
    std::array<TrickSetting, RT_MAX> mTrickSettings = {};
    std::array<std::vector<Option*>, RCAREA_INVALID> mExcludeLocationsOptionsAreas = {};
    std::unordered_map<std::string, RandomizerTrick> mTrickNameToEnum;
};
} // namespace Rando
