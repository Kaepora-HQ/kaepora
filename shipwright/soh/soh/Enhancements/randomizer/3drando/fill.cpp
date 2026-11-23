#include "fill.hpp"

#include "../dungeon.h"
#include "../SeedContext.h"
#include "item_pool.hpp"
#include "starting_inventory.hpp"
#include "../rng.h"
#include "shops.hpp"
#include "pool_functions.hpp"
#include "soh/Enhancements/randomizer/static_data.h"
#include "soh/util.h"

#include <algorithm>
#include <bitset>
#include <vector>
#include <list>
#include <map>
#include <ranges>
#include <set>
#include <stdexcept>
#include <string>
#include <spdlog/spdlog.h>

using namespace Rando;

static bool placementFailure = false;

// The places whose scene sets what later steps wait for, whatever the place gives, and what each sets: the place after
// each boss and the dungeon its blue warp clears, the scene after the last carpenter and the Gerudo letting Link pass,
// Zelda's scene in the Temple of Time, which the bridge to Ganon's Castle follows, and the talks of Malon at the castle
// and of adult Anju, after which each one's egg found hatches.
static constexpr std::pair<RandomizerCheck, LogicVal> everySeedsScenePlaces[] = {
    { RC_QUEEN_GOHMA, LOGIC_DEKU_TREE_CLEAR },     { RC_KING_DODONGO, LOGIC_DODONGOS_CAVERN_CLEAR },
    { RC_BARINADE, LOGIC_JABU_JABUS_BELLY_CLEAR }, { RC_PHANTOM_GANON, LOGIC_FOREST_TEMPLE_CLEAR },
    { RC_VOLVAGIA, LOGIC_FIRE_TEMPLE_CLEAR },      { RC_MORPHA, LOGIC_WATER_TEMPLE_CLEAR },
    { RC_BONGO_BONGO, LOGIC_SHADOW_TEMPLE_CLEAR }, { RC_TWINROVA, LOGIC_SPIRIT_TEMPLE_CLEAR },
    { RC_TH_FREED_CARPENTERS, LOGIC_GERUDOS_ACCEPT_LINK }, { RC_TOT_LIGHT_ARROWS_CUTSCENE, LOGIC_SHEIK_REVEALS_ZELDA },
    { RC_HC_MALON_EGG, LOGIC_MET_MALON_AT_CASTLE },
    { RC_KAK_ANJU_AS_ADULT, LOGIC_MET_ADULT_ANJU },
};

// Zelda's courtyard, where Link meets her, which Impa's and Saria's lessons, the moat and Kakariko's gate wait for. It
// is a scene's place only in a seed that shuffles it: one that keeps her letter there keeps SoH's route, which names
// her place where a step waits for the letter.
static constexpr std::pair<RandomizerCheck, LogicVal> zeldasCourtyard = { RC_HC_ZELDAS_LETTER, LOGIC_MET_ZELDA };

static std::vector<std::pair<RandomizerCheck, LogicVal>> scenePlaces;

// The events of scenePlaces that no region sets, so every search sets each where it takes or reaches its place, the
// moment it would take an item there: a giver's talk, which an egg's hatch follows whatever the talk gives.
static constexpr LogicVal talks[] = { LOGIC_MET_MALON_AT_CASTLE, LOGIC_MET_ADULT_ANJU };

// Set while the spoiler log's route is made.
static bool routeTakesScenes = false;

static bool IsScenePlace(RandomizerCheck location) {
    return std::ranges::any_of(scenePlaces, [location](const auto& scene) { return scene.first == location; });
}

bool SetByItsScenePlace(LogicVal event) {
    return routeTakesScenes &&
           std::ranges::any_of(scenePlaces, [event](const auto& scene) { return scene.second == event; });
}

// Sets what the scene at location sets, where the search under way lets the place set it: every event while the
// spoiler log's route is made, and a talk in every search. Returns whether it set one.
static bool SetScenesAt(RandomizerCheck location) {
    bool set = false;
    for (const auto& [place, event] : scenePlaces) {
        if (place == location && (routeTakesScenes || std::ranges::find(talks, event) != std::end(talks))) {
            logic->Set(event, true);
            set = true;
        }
    }
    return set;
}

void ApplyCheck(Rando::ItemLocation* location) {
    location->ApplyPlacedItemEffect();
    SetScenesAt(location->GetRandomizerCheck());
}

PriceSettingsStruct shopsanityPrices = {
    RSK_SHOPSANITY_PRICES,
    RSK_SHOPSANITY_PRICES_FIXED_PRICE,
    RSK_SHOPSANITY_PRICES_RANGE_1,
    RSK_SHOPSANITY_PRICES_RANGE_2,
    RSK_SHOPSANITY_PRICES_NO_WALLET_WEIGHT,
    RSK_SHOPSANITY_PRICES_CHILD_WALLET_WEIGHT,
    RSK_SHOPSANITY_PRICES_ADULT_WALLET_WEIGHT,
    RSK_SHOPSANITY_PRICES_GIANT_WALLET_WEIGHT,
    RSK_SHOPSANITY_PRICES_TYCOON_WALLET_WEIGHT,
    RSK_SHOPSANITY_PRICES_AFFORDABLE,
};

PriceSettingsStruct scrubPrices = {
    RSK_SCRUBS_PRICES,
    RSK_SCRUBS_PRICES_FIXED_PRICE,
    RSK_SCRUBS_PRICES_RANGE_1,
    RSK_SCRUBS_PRICES_RANGE_2,
    RSK_SCRUBS_PRICES_NO_WALLET_WEIGHT,
    RSK_SCRUBS_PRICES_CHILD_WALLET_WEIGHT,
    RSK_SCRUBS_PRICES_ADULT_WALLET_WEIGHT,
    RSK_SCRUBS_PRICES_GIANT_WALLET_WEIGHT,
    RSK_SCRUBS_PRICES_TYCOON_WALLET_WEIGHT,
    RSK_SCRUBS_PRICES_AFFORDABLE,
};

PriceSettingsStruct merchantPrices = {
    RSK_MERCHANT_PRICES,
    RSK_MERCHANT_PRICES_FIXED_PRICE,
    RSK_MERCHANT_PRICES_RANGE_1,
    RSK_MERCHANT_PRICES_RANGE_2,
    RSK_MERCHANT_PRICES_NO_WALLET_WEIGHT,
    RSK_MERCHANT_PRICES_CHILD_WALLET_WEIGHT,
    RSK_MERCHANT_PRICES_ADULT_WALLET_WEIGHT,
    RSK_MERCHANT_PRICES_GIANT_WALLET_WEIGHT,
    RSK_MERCHANT_PRICES_TYCOON_WALLET_WEIGHT,
    RSK_MERCHANT_PRICES_AFFORDABLE,
};

static void PropagateTimeTravel(GetAccessibleLocationsStruct& gals, RandomizerGet ignore = RG_NONE,
                                bool stopOnBeatable = false, bool addToPlaythrough = false) {
    // special check for temple of time
    if (gals.haveTimeAccess && gals.foundTempleOfTime && gals.validatedStartingRegion) {
        if (!RegionTable(RR_ROOT)->Adult() &&
            RegionTable(RR_TOT_BEYOND_DOOR_OF_TIME)
                ->Child()) { // RANDOTODO: sphere weirdness, other age locations not propagated in this sphere
            RegionTable(RR_ROOT)->adultDay = RegionTable(RR_TOT_BEYOND_DOOR_OF_TIME)->childDay;
            RegionTable(RR_ROOT)->adultNight = RegionTable(RR_TOT_BEYOND_DOOR_OF_TIME)->childNight;
            ProcessRegion(RegionTable(RR_ROOT), gals, ignore, stopOnBeatable, addToPlaythrough);
        } else if (!RegionTable(RR_ROOT)->Child() && RegionTable(RR_TOT_BEYOND_DOOR_OF_TIME)->Adult()) {
            RegionTable(RR_ROOT)->childDay = RegionTable(RR_TOT_BEYOND_DOOR_OF_TIME)->adultDay;
            RegionTable(RR_ROOT)->childNight = RegionTable(RR_TOT_BEYOND_DOOR_OF_TIME)->adultNight;
            ProcessRegion(RegionTable(RR_ROOT), gals, ignore, stopOnBeatable, addToPlaythrough);
        }
    }
}

// This function will propagate Time of Day access through the entrance
static bool UpdateToDAccess(Entrance* entrance, Region* connection) {
    bool ageTimePropagated = false;
    Region* parent = entrance->GetParentRegion();

    if (!connection->childDay && parent->childDay && entrance->CheckConditionAtAgeTime(logic->IsChild, logic->AtDay)) {
        connection->childDay = true;
        ageTimePropagated = true;
    }
    if (!connection->childNight && parent->childNight &&
        entrance->CheckConditionAtAgeTime(logic->IsChild, logic->AtNight)) {
        connection->childNight = true;
        ageTimePropagated = true;
    }
    if (!connection->adultDay && parent->adultDay && entrance->CheckConditionAtAgeTime(logic->IsAdult, logic->AtDay)) {
        connection->adultDay = true;
        ageTimePropagated = true;
    }
    if (!connection->adultNight && parent->adultNight &&
        entrance->CheckConditionAtAgeTime(logic->IsAdult, logic->AtNight)) {
        connection->adultNight = true;
        ageTimePropagated = true;
    }

    return ageTimePropagated;
}

// Check if key locations in the overworld are accessable
static void ValidateOtherEntrance(GetAccessibleLocationsStruct& gals) {
    auto ctx = Rando::Context::GetInstance();
    // Condition for validating Temple of Time Access
    if (!gals.foundTempleOfTime &&
        ((ctx->GetOption(RSK_SELECTED_STARTING_AGE).Is(RO_AGE_CHILD) && RegionTable(RR_TEMPLE_OF_TIME)->Adult()) ||
         (ctx->GetOption(RSK_SELECTED_STARTING_AGE).Is(RO_AGE_ADULT) && RegionTable(RR_TEMPLE_OF_TIME)->Child()))) {
        gals.foundTempleOfTime = true;
    }
    // Condition for validating a valid starting region
    if (!gals.validatedStartingRegion) {
        bool childAccess = ctx->GetOption(RSK_SELECTED_STARTING_AGE).Is(RO_AGE_CHILD) ||
                           RegionTable(RR_TOT_BEYOND_DOOR_OF_TIME)->Child();
        bool adultAccess = ctx->GetOption(RSK_SELECTED_STARTING_AGE).Is(RO_AGE_ADULT) ||
                           RegionTable(RR_TOT_BEYOND_DOOR_OF_TIME)->Adult();

        Region* kokiri = RegionTable(RR_KOKIRI_FOREST);
        Region* kakariko = RegionTable(RR_KAKARIKO_VILLAGE);

        if ((childAccess &&
             (kokiri->Child() ||
              kakariko->Child())) || // RANDOTODO when proper ammo logic is done, this could probably be made optional
            (adultAccess && (kokiri->Adult() || kakariko->Adult()))) {
            gals.validatedStartingRegion = true;
            ApplyStartingInventory(); // RANDOTODO when proper ammo logic is done, this could be moved to the start
        }
    }
}

// Apply all items that are necessary for checking all location access
static void ApplyAllAdvancmentItems() {
    std::vector<RandomizerGet> itemsToPlace =
        FilterFromPool(itemPool, [](const auto i) { return Rando::StaticData::RetrieveItem(i).IsAdvancement(); });
    for (RandomizerGet unplacedItem : itemsToPlace) {
        Rando::StaticData::RetrieveItem(unplacedItem).ApplyEffect();
    }
}

// Check if everything in an entrance rando seed that needs to be avalible without items, is,
// and if so allow obtaining items in logic
static void ValidateSphereZero(GetAccessibleLocationsStruct& gals) {
    auto ctx = Rando::Context::GetInstance();
    // Condition for verifying everything required for sphere 0, expanding search to all locations
    if (gals.validatedStartingRegion && gals.foundTempleOfTime && gals.haveTimeAccess) {
        // Apply all items that are necessary for checking all location access
        ApplyAllAdvancmentItems();
        // Reset access as the non-starting age
        if (ctx->GetOption(RSK_SELECTED_STARTING_AGE).Is(RO_AGE_CHILD)) {
            for (RandomizerRegion regionKey : gals.regionPool) {
                RegionTable(regionKey)->adultDay = false;
                RegionTable(regionKey)->adultNight = false;
            }
        } else {
            for (RandomizerRegion regionKey : gals.regionPool) {
                RegionTable(regionKey)->childDay = false;
                RegionTable(regionKey)->childNight = false;
            }
        }
        // RANDOTODO do we want to keep the region pool after this reset?
        // It's a lot of potential looping over regions we have no access to
        gals.sphereZeroComplete = true;
    }
}

// This function handles each possible exit
void ProcessExits(Region* region, GetAccessibleLocationsStruct& gals, RandomizerGet ignore = RG_NONE,
                  bool stopOnBeatable = false, bool addToPlaythrough = false) {
    for (auto& exit : region->exits) {
        if (!logic->ACProcessUndiscoveredExits && logic->CalculatingAvailableChecks &&
            ctx->GetOption(RSK_SHUFFLE_ENTRANCES).Get() && exit.IsShuffled() && exit.GetIndex() != -1 &&
            !Entrance_GetIsEntranceDiscovered(exit.GetIndex())) {
            continue;
        }

        Region* exitRegion = exit.GetConnectedRegion();
        // Update Time of Day Access for the exit
        if (UpdateToDAccess(&exit, exitRegion)) {
            gals.logicUpdated = true;
            if (!gals.sphereZeroComplete) {
                if (!gals.foundTempleOfTime || !gals.validatedStartingRegion) {
                    ValidateOtherEntrance(gals);
                }
                ValidateSphereZero(gals);
            }
            // If the exit is accessible and hasn't been added yet, add it to the pool
            // RANDOTODO do we want to add the region after the loop now, considering we
            // are processing the new region immediately. Maybe a reverse for loop in ProcessRegion?
            if (!exitRegion->addedToPool) {
                exitRegion->addedToPool = true;
                gals.regionPool.push_back(exit.GetConnectedRegionKey());
            }

            // process the region we just expanded to, to reduce looping
            ProcessRegion(exitRegion, gals, ignore, stopOnBeatable, addToPlaythrough);
        }

        if (addToPlaythrough) {
            // RANDOTODO Should this match the regular spheres?
            // Add shuffled entrances to the entrance playthrough
            // Include bluewarps when unshuffled but dungeon or boss shuffle is on
            if ((exit.IsShuffled() ||
                 (exit.GetType() == Rando::EntranceType::BlueWarp &&
                  (ctx->GetOption(RSK_SHUFFLE_GANONS_TOWER_ENTRANCE) ||
                   exit.GetParentRegionKey() != RR_GANONS_TOWER_STAIRS_1) &&
                  (ctx->GetOption(RSK_SHUFFLE_DUNGEON_ENTRANCES) || ctx->GetOption(RSK_SHUFFLE_BOSS_ENTRANCES)))) &&
                !exit.IsAddedToPool() && !ctx->GetEntranceShuffler()->HasNoRandomEntrances()) {
                gals.entranceSphere.push_back(&exit);
                exit.AddToPool();
                // Don't list a two-way coupled entrance from both directions
                if (exit.GetReverse() != nullptr && exit.GetReplacement()->GetReverse() != nullptr &&
                    !exit.IsDecoupled()) {
                    exit.GetReplacement()->GetReverse()->AddToPool();
                }
            }
        }
    }
}

// Get the max number of tokens that can possibly be useful
static int GetMaxGSCount() {
    auto ctx = Rando::Context::GetInstance();
    // Get the max amount of GS which could be useful from token reward locations
    int maxUseful = 0;
    // If the highest advancement item is a token, we know it is useless since it won't lead to an otherwise useful item
    if (ctx->GetItemLocation(RC_KAK_100_GOLD_SKULLTULA_REWARD)->GetPlacedItem().IsAdvancement() &&
        ctx->GetItemLocation(RC_KAK_100_GOLD_SKULLTULA_REWARD)->GetPlacedItem().GetItemType() != ITEMTYPE_TOKEN) {
        maxUseful = 100;
    } else if (ctx->GetItemLocation(RC_KAK_50_GOLD_SKULLTULA_REWARD)->GetPlacedItem().IsAdvancement() &&
               ctx->GetItemLocation(RC_KAK_50_GOLD_SKULLTULA_REWARD)->GetPlacedItem().GetItemType() != ITEMTYPE_TOKEN) {
        maxUseful = 50;
    } else if (ctx->GetItemLocation(RC_KAK_40_GOLD_SKULLTULA_REWARD)->GetPlacedItem().IsAdvancement() &&
               ctx->GetItemLocation(RC_KAK_40_GOLD_SKULLTULA_REWARD)->GetPlacedItem().GetItemType() != ITEMTYPE_TOKEN) {
        maxUseful = 40;
    } else if (ctx->GetItemLocation(RC_KAK_30_GOLD_SKULLTULA_REWARD)->GetPlacedItem().IsAdvancement() &&
               ctx->GetItemLocation(RC_KAK_30_GOLD_SKULLTULA_REWARD)->GetPlacedItem().GetItemType() != ITEMTYPE_TOKEN) {
        maxUseful = 30;
    } else if (ctx->GetItemLocation(RC_KAK_20_GOLD_SKULLTULA_REWARD)->GetPlacedItem().IsAdvancement() &&
               ctx->GetItemLocation(RC_KAK_20_GOLD_SKULLTULA_REWARD)->GetPlacedItem().GetItemType() != ITEMTYPE_TOKEN) {
        maxUseful = 20;
    } else if (ctx->GetItemLocation(RC_KAK_10_GOLD_SKULLTULA_REWARD)->GetPlacedItem().IsAdvancement() &&
               ctx->GetItemLocation(RC_KAK_10_GOLD_SKULLTULA_REWARD)->GetPlacedItem().GetItemType() != ITEMTYPE_TOKEN) {
        maxUseful = 10;
    }
    // If bridge, GBK, Ganon's Soul, or win condition is set to tokens, get how many are required
    if (ctx->GetOption(RSK_RAINBOW_BRIDGE).Is(RO_BRIDGE_TOKENS)) {
        maxUseful = std::max(maxUseful, (int)ctx->GetOption(RSK_RAINBOW_BRIDGE_TOKEN_COUNT).Get());
    }
    if (ctx->GetOption(RSK_GANONS_BOSS_KEY).Is(RO_GANON_BOSS_KEY_TOKENS)) {
        maxUseful = std::max(maxUseful, (int)ctx->GetOption(RSK_GBK_TOKEN_COUNT).Get());
    }
    if (ctx->GetOption(RSK_GANONS_SOUL).Is(RO_GANONS_SOUL_TOKENS)) {
        maxUseful = std::max(maxUseful, (int)ctx->GetOption(RSK_GANONS_SOUL_TOKEN_COUNT).Get());
    }
    if (ctx->GetOption(RSK_WINCON).Is(RO_WINCON_TOKENS)) {
        maxUseful = std::max(maxUseful, (int)ctx->GetOption(RSK_WINCON_TOKEN_COUNT).Get());
    }
    // Return max of the two possible reasons tokens could be important, minus the tokens in the starting inventory
    return maxUseful - ctx->GetOption(RSK_STARTING_SKULLTULA_TOKEN).Get();
}

std::string GetShopItemBaseName(std::string itemName) {
    std::string baseName = itemName.erase(0, 4); // Delete "Buy "
    // Delete amount, if present (so when it looks like Buy Deku Nut (10) remove the (10))
    if (baseName.find("(") != std::string::npos) {
        baseName = baseName.erase(baseName.find("("));
    }
    // Do the same for [] (only applies to red potions, other things with [] also have a ())
    if (baseName.find("[") != std::string::npos) {
        baseName = baseName.erase(baseName.find("["));
    }
    return baseName;
}

std::vector<RandomizerCheck> GetEmptyLocations(std::vector<RandomizerCheck> targetLocations) {
    auto ctx = Rando::Context::GetInstance();
    return FilterFromPool(targetLocations, [ctx](const auto loc) {
        return ctx->GetItemLocation(loc)->GetPlacedRandomizerGet() == RG_NONE;
    });
}

bool IsBombchus(RandomizerGet item, bool includeShops = false) {
    return (item >= RG_BOMBCHU_5 && item <= RG_BOMBCHU_20) || item == RG_PROGRESSIVE_BOMBCHU_BAG ||
           (includeShops && (item == RG_BUY_BOMBCHUS_10 || item == RG_BUY_BOMBCHUS_20));
}

bool IsBeatableWithout(RandomizerCheck excludedCheck, bool replaceItem,
                       RandomizerGet ignore = RG_NONE) { // RANDOTODO make excludedCheck an ItemLocation
    Rando::ItemLocation* location = Rando::Context::GetInstance()->GetItemLocation(excludedCheck);
    location->DelayItem();
    logic->Reset();
    bool result = CheckBeatable(ignore);
    if (replaceItem) {
        location->SaveDelayedItem(); // Immediately put item back
    }
    return result;
}

// Reset non-Logic-class logic, and optionally apply the initial inventory
void ResetLogic(std::shared_ptr<Context>& ctx, GetAccessibleLocationsStruct& gals, bool applyInventory = false) {
    gals.timePassChildDay = true;
    gals.timePassChildNight = true;
    gals.timePassAdultDay = true;
    gals.timePassAdultNight = true;
    gals.haveTimeAccess = true;
    gals.foundTempleOfTime = true;
    gals.validatedStartingRegion = true;
    gals.sphereZeroComplete = true;
    if (applyInventory) {
        ApplyStartingInventory();
    }
    Regions::AccessReset();
    ctx->LocationReset();
}

// Generate the playthrough, so we want to add advancement items, unless we know to ignore them
void AddToPlaythrough(LocationAccess& locPair, GetAccessibleLocationsStruct& gals) {
    auto ctx = Rando::Context::GetInstance();
    RandomizerCheck loc = locPair.GetLocation();
    Rando::ItemLocation* location = ctx->GetItemLocation(loc);
    RandomizerGet locItem = location->GetPlacedRandomizerGet();
    // A scene's place is a step whatever it holds, for what its scene sets.
    const bool scenePlace = routeTakesScenes && IsScenePlace(loc) && locItem != RG_NONE;
    // Reached after wincon, so never part of playthrough. Pare first
    if (ctx->playthroughBeatable && (scenePlace || location->GetPlacedItem().IsAdvancement())) {
        gals.lateLocations.push_back(loc);
    }
    // Item is an advancement item, figure out if it should be added to this sphere
    if (!ctx->playthroughBeatable && (scenePlace || location->GetPlacedItem().IsAdvancement())) {
        ItemType type = location->GetPlacedItem().GetItemType();

        // Decide whether to exclude this location
        // This preprocessing is done to reduce the amount of searches performed in PareDownPlaythrough
        // Want to exclude:
        // 1) Tokens after the last potentially useful one (the last one that gives an advancement item or last for
        // token bridge) 2) Buy items of the same type, after the first (So only see Buy Deku Nut of any amount once)
        bool exclude = true;
        if (scenePlace) {
            exclude = false;
        }
        // Exclude tokens after the last possibly useful one
        else if (type == ITEMTYPE_TOKEN && gals.gsCount < gals.maxGsCount) {
            gals.gsCount++;
            exclude = false;
        }
        // Handle buy items
        // If ammo drops are off, don't do this step, since buyable ammo becomes logically important
        //  TODO: Reimplement Ammo Drops setting
        else if (/*AmmoDrops.IsNot(AMMODROPS_NONE) &&*/ type == ITEMTYPE_SHOP) {
            // Only check each buy item once
            auto buyItem = location->GetPlacedItem().GetLogicVal();
            // Buy item not in list to ignore, add it to list and write to playthrough
            if (std::find(gals.buyIgnores.begin(), gals.buyIgnores.end(), buyItem) == gals.buyIgnores.end()) {
                exclude = false;
                gals.buyIgnores.push_back(buyItem);
            }
        }
        // Add all other advancement items
        else if (type != ITEMTYPE_TOKEN && (/*AmmoDrops.Is(AMMODROPS_NONE) ||*/ type != ITEMTYPE_SHOP)) {
            exclude = false;
        }
        // Has not been excluded, add to playthrough
        if (!exclude) {
            gals.itemSphere.push_back(loc);
        }
        // Triforce has been found, seed is beatable, nothing else in this or future spheres matters
        if (location->GetPlacedRandomizerGet() == RG_TRIFORCE) {
            gals.itemSphere.clear();
            gals.itemSphere.push_back(loc);
            ctx->playthroughBeatable = true;
            // Items found in this sphere aren't collected before wincon
            for (Rando::ItemLocation* pending : gals.newItemLocations) {
                if (pending != location && pending->GetPlacedItem().IsAdvancement()) {
                    gals.lateLocations.push_back(pending->GetRandomizerCheck());
                }
            }
        }
    }
}

void ApplyOrStoreItem(Rando::ItemLocation* loc, GetAccessibleLocationsStruct& gals, bool addToPlaythrough) {
    if (addToPlaythrough) {
        gals.newItemLocations.push_back(loc);
    } else {
        ApplyCheck(loc);
    }
    gals.logicUpdated = true;
}

// Adds the contents of a location to the current progression and optionally playthrough
bool AddCheckToLogic(LocationAccess& locPair, GetAccessibleLocationsStruct& gals, RandomizerGet ignore,
                     bool stopOnBeatable, Region* parentRegion, bool addToPlaythrough = false) {
    RandomizerCheck loc = locPair.GetLocation();
    Rando::ItemLocation* location = ctx->GetItemLocation(loc);
    // Most calls hit a location this search already added, so skip those before any other work
    if (location->IsAddedToPool()) {
        return false;
    }
    logic->CurrentCheckKey = loc;

#ifndef NDEBUG
    RandomizerCheckQuest quest = Rando::StaticData::GetLocation(loc)->GetQuest();
    assert(ctx->GetOption(RSK_NO_LOGIC).Is(RO_GENERIC_ON) || quest == RCQUEST_BOTH ||
           (quest == RCQUEST_VANILLA && ctx->GetDungeonFromScene(parentRegion->scene)->IsVanilla()) ||
           (quest == RCQUEST_MQ && ctx->GetDungeonFromScene(parentRegion->scene)->IsMQ()));
#endif

    if (locPair.ConditionsMet(parentRegion, logic->CalculatingAvailableChecks) && !logic->ShopItemNotForSale(loc)) {
        location->AddToPool();
        RandomizerGet locItem = location->GetPlacedRandomizerGet();

        if (locItem == RG_NONE || logic->CalculatingAvailableChecks) {
            gals.accessibleLocations.push_back(loc); // Empty location, consider for placement
        }
        // A talk comes whatever the place will hold, so the fill sets it where it reaches the place still empty. The
        // route's searches leave it to the place's taking, so that the route names the place where a step waits for it.
        if (locItem == RG_NONE && !routeTakesScenes && SetScenesAt(loc)) {
            gals.logicUpdated = true;
        }
        if (locItem != RG_NONE) {
            // If ignore has a value, we want to check if the item location should be considered or not
            // This is necessary due to the below preprocessing for playthrough generation
            if (ignore != RG_NONE) {
                ItemType type = location->GetPlacedItem().GetItemType();
                // If we want to ignore tokens, only add if not a token
                if (ignore == RG_GOLD_SKULLTULA_TOKEN && type != ITEMTYPE_TOKEN) {
                    ApplyOrStoreItem(location, gals, addToPlaythrough);
                }
                // If we want to ignore bombchus, only add if bombchu is not in the name
                else if (IsBombchus(ignore, true) && !IsBombchus(locItem, true)) {
                    ApplyOrStoreItem(location, gals, addToPlaythrough);
                }
                // We want to ignore a specific Buy item. Buy items with different RandomizerGets are recognised by a
                // shared GetLogicVal
                else if (ignore != RG_GOLD_SKULLTULA_TOKEN && !IsBombchus(ignore, true)) {
                    if ((type == ITEMTYPE_SHOP && Rando::StaticData::GetItemTable()[ignore].GetLogicVal() !=
                                                      location->GetPlacedItem().GetLogicVal()) ||
                        type != ITEMTYPE_SHOP) {
                        ApplyOrStoreItem(location, gals, addToPlaythrough);
                    }
                }
            }
            // If it doesn't, we can just add the location
            else {
                ApplyOrStoreItem(location, gals,
                                 addToPlaythrough); // Add item to cache to be considered in logic next iteration
            }
        }
        if (addToPlaythrough) {
            AddToPlaythrough(locPair, gals);
        }
        // All we care about is if the game is beatable, used to pare down playthrough
        if (location->GetPlacedRandomizerGet() == RG_TRIFORCE && stopOnBeatable) {
            logic->CurrentCheckKey = RC_UNKNOWN_CHECK;
            return true; // Return early for efficiency
        }
    }
    logic->CurrentCheckKey = RC_UNKNOWN_CHECK;
    return false;
}

void ProcessRegion(Region* region, GetAccessibleLocationsStruct& gals, RandomizerGet ignore, bool stopOnBeatable,
                   bool addToPlaythrough) {
    RandomizerRegion previousRegionKey = logic->CurrentRegionKey;
    logic->CurrentRegionKey = region->randomizerRegionKey;

    if (gals.haveTimeAccess) {
        region->ApplyTimePass();
    } else {
        // If we're checking for TimePass access do that for each region as it's being updated.
        // TimePass Access is satisfied when every AgeTime can reach a region with TimePass
        // without the aid of TimePass. During this mode, TimePass won't update ToD access
        // in any region.
        // RANDOTODO can probably be removed after a ToD rework that accounts for having Dampe time access
        if (region->TimePass()) {
            if (region->childDay) {
                gals.timePassChildDay = true;
            }
            if (region->childNight) {
                gals.timePassChildNight = true;
            }
            if (region->adultDay) {
                gals.timePassAdultDay = true;
            }
            if (region->adultNight) {
                gals.timePassAdultNight = true;
            }
        }
        // Condition for validating that all startring AgeTimes have timepass access
        if (gals.timePassChildDay && gals.timePassChildNight && gals.timePassAdultDay && gals.timePassAdultNight) {
            gals.haveTimeAccess = true;
            region->ApplyTimePass();
        }
    }

    if (region->UpdateEvents()) {
        gals.logicUpdated = true;
        // if we are working in spheres, reset the sphere on an event being enabled to avoid sphere skipping
        if (addToPlaythrough) {
            gals.resetSphere = true;
        }
    }

    ProcessExits(region, gals, ignore, stopOnBeatable, addToPlaythrough);

    PropagateTimeTravel(gals, ignore, stopOnBeatable, addToPlaythrough);
    for (size_t k = 0; k < region->locations.size(); k++) {
        if (AddCheckToLogic(region->locations[k], gals, ignore, stopOnBeatable, region, addToPlaythrough)) {
            Rando::Context::GetInstance()->playthroughBeatable = true;
            logic->CurrentRegionKey = previousRegionKey;
            return;
        }
    }

    logic->CurrentRegionKey = previousRegionKey;
}

// Return any of the targetLocations that are accessible in logic
std::vector<RandomizerCheck> ReachabilitySearch(const std::vector<RandomizerCheck>& targetLocations,
                                                RandomizerGet ignore /* = RG_NONE*/,
                                                bool calculatingAvailableChecks /* = false */,
                                                RandomizerRegion startingRegion /* = RR_ROOT */,
                                                RandoAgeTime startingAgeTime /* = RAT_NONE*/) {
    auto ctx = Rando::Context::GetInstance();
    GetAccessibleLocationsStruct gals(0);
    ResetLogic(ctx, gals, !calculatingAvailableChecks);
    if (startingRegion != RR_ROOT) {
        gals.regionPool.insert(gals.regionPool.begin(), startingRegion);

        const auto& region = RegionTable(startingRegion);
        if (startingAgeTime == RAT_CHILD_DAY) {
            region->childDay = true;
            region->childNight = region->timePass;
        } else if (startingAgeTime == RAT_CHILD_NIGHT) {
            region->childNight = true;
            region->childDay = region->timePass;
        } else if (startingAgeTime == RAT_ADULT_DAY) {
            region->adultDay = true;
            region->adultNight = region->timePass;
        } else if (startingAgeTime == RAT_ADULT_NIGHT) {
            region->adultNight = true;
            region->adultDay = region->timePass;
        }
    }
    if (calculatingAvailableChecks) {
        logic->Reset(false);
        logic->CalculatingAvailableChecks = true;
    }
    do {
        gals.InitLoop();
        for (size_t i = 0; i < gals.regionPool.size(); i++) {
            ProcessRegion(RegionTable(gals.regionPool[i]), gals, ignore);
        }
    } while (gals.logicUpdated);
    std::bitset<RC_MAX> isTarget;
    for (RandomizerCheck allowedLocation : targetLocations) {
        isTarget.set(allowedLocation);
    }
    std::erase_if(gals.accessibleLocations, [&isTarget, ctx, calculatingAvailableChecks](RandomizerCheck loc) {
        if (ctx->GetItemLocation(loc)->GetPlacedRandomizerGet() != RG_NONE && !calculatingAvailableChecks) {
            return false;
        }
        return !isTarget[loc];
    });
    return gals.accessibleLocations;
}

// Create the playthrough for the seed
std::vector<RandomizerCheck> GeneratePlaythrough() {
    auto ctx = Rando::Context::GetInstance();
    ctx->playthroughBeatable = false;
    logic->Reset();
    GetAccessibleLocationsStruct gals(GetMaxGSCount());
    ResetLogic(ctx, gals, true);
    do {
        gals.InitLoop();
        for (size_t i = 0; i < gals.regionPool.size(); i++) {
        resetSphere:
            ProcessRegion(RegionTable(gals.regionPool[i]), gals, RG_NONE, false, true);
            if (gals.resetSphere) {
                gals.resetSphere = false;
                i = 0;
                goto resetSphere;
            }
        }
        if (gals.itemSphere.size() > 0) {
            ctx->playthroughLocations.push_back(gals.itemSphere);
        }
        if (gals.entranceSphere.size() > 0 && !ctx->GetEntranceShuffler()->HasNoRandomEntrances()) {
            ctx->GetEntranceShuffler()->playthroughEntrances.push_back(gals.entranceSphere);
        }
    } while (gals.logicUpdated);
    return gals.lateLocations;
}

// return if the seed is currently beatable or not
bool CheckBeatable(RandomizerGet ignore /* = RG_NONE*/) {
    auto ctx = Rando::Context::GetInstance();
    ctx->playthroughBeatable = false;
    GetAccessibleLocationsStruct gals(0);
    ResetLogic(ctx, gals, true);
    do {
        gals.InitLoop();
        for (size_t i = 0; i < gals.regionPool.size(); i++) {
            ProcessRegion(RegionTable(gals.regionPool[i]), gals, ignore, true);
            if (ctx->playthroughBeatable == true) {
                return true;
            }
        }
    } while (gals.logicUpdated);
    return false;
}

// Check if the currently randomised set of entrances is a valid game map.
void ValidateEntrances(bool checkOtherEntranceAccess) {
    auto ctx = Rando::Context::GetInstance();
    GetAccessibleLocationsStruct gals(0);
    ResetLogic(ctx, gals, !checkOtherEntranceAccess);

    ctx->allLocationsReachable = false;

    if (checkOtherEntranceAccess) {
        gals.foundTempleOfTime = false;
        gals.validatedStartingRegion = false;
        // Variables for Time Pass access
        gals.timePassChildDay = false;
        gals.timePassChildNight = false;
        gals.timePassAdultDay = false;
        gals.timePassAdultNight = false;
        gals.haveTimeAccess = false;
        gals.sphereZeroComplete = false;
        RegionTable(RR_ROOT)->childNight = true;
        RegionTable(RR_ROOT)->adultNight = true;
        RegionTable(RR_ROOT)->childDay = true;
        RegionTable(RR_ROOT)->adultDay = true;
    } else {
        ApplyAllAdvancmentItems();
    }

    do {
        gals.InitLoop();
        for (size_t i = 0; i < gals.regionPool.size(); i++) {
            ProcessRegion(RegionTable(gals.regionPool[i]), gals);
        }
    } while (gals.logicUpdated);
    if (gals.sphereZeroComplete) {
        ctx->allLocationsReachable = true;
        // RANDOTODO a size check here before getting the exact fails would be a minor optimization
        // and a full list of location failures would be useful for logging when it does fail
        for (const RandomizerCheck loc : ctx->allLocations) {
            if (!ctx->GetItemLocation(loc)->IsAddedToPool()) {
                ctx->allLocationsReachable = false;
                SPDLOG_DEBUG(
                    "Location {} not reachable",
                    Rando::StaticData::GetLocation(ctx->GetItemLocation(loc)->GetRandomizerCheck())->GetName());
#ifndef ENABLE_DEBUG
                break;
#endif
            }
        }
    }
}

static bool IsPurchaseOrSkulltulaReward(RandomizerCheck check) {
    return Rando::Context::GetInstance()->GetItemLocation(check)->CanBePurchased() ||
           Rando::StaticData::GetLocation(check)->GetScene() == SCENE_HOUSE_OF_SKULLTULA;
}

// Remove unnecessary items from playthrough by removing their location, and checking if game is still beatable
// To reduce searches, some preprocessing is done in playthrough generation to avoid adding obviously unnecessary items
static void PareDownPlaythrough(const std::vector<RandomizerCheck>& lateLocations) {
    auto ctx = Rando::Context::GetInstance();
    std::vector<RandomizerCheck> toAddBackItem;
    // Hide items only reachable after wincon. Otherwise they can stand in for a removed playthrough item
    for (RandomizerCheck loc : lateLocations) {
        ctx->GetItemLocation(loc)->DelayItem();
        toAddBackItem.push_back(loc);
    }
    // The route names one shop slot of each purchase, the first it reaches. Every other item that counts as progress,
    // and every scene's place the route leaves out, is set aside before the passes below, so no slot off the route
    // stands in for a step they drop. No token is set aside: the passes count on every one.
    std::bitset<RC_MAX> onRoute;
    for (const std::vector<RandomizerCheck>& sphere : ctx->playthroughLocations) {
        for (RandomizerCheck loc : sphere) {
            onRoute.set(loc);
        }
    }
    for (RandomizerCheck loc : ctx->allLocations) {
        Rando::ItemLocation* location = ctx->GetItemLocation(loc);
        if (!onRoute[loc] && location->GetPlacedRandomizerGet() != RG_NONE &&
            (IsScenePlace(loc) || (location->GetPlacedItem().IsAdvancement() &&
                                   location->GetPlacedRandomizerGet() != RG_GOLD_SKULLTULA_TOKEN))) {
            location->DelayItem();
            toAddBackItem.push_back(loc);
        }
    }
    // Purchases and Skulltula House rewards the seed can do without go first, so the route takes what Link is handed
    // over what he buys or earns with tokens.
    for (bool purchasesAndRewardsOnly : { true, false }) {
        // Start at sphere before Ganon's and count down
        for (int32_t i = static_cast<int32_t>(ctx->playthroughLocations.size()) - 2; i >= 0; i--) {
            // Check each item location in sphere
            std::vector<RandomizerCheck> sphere = ctx->playthroughLocations.at(i);
            for (int32_t j = static_cast<int32_t>(sphere.size()) - 1; j >= 0; j--) {
                RandomizerCheck loc = sphere.at(j);
                if (purchasesAndRewardsOnly && !IsPurchaseOrSkulltulaReward(loc)) {
                    continue;
                }
                RandomizerGet locGet = ctx->GetItemLocation(loc)->GetPlacedRandomizerGet();

                // Without a scene's place Link also lacks what its scene sets, whatever the place holds.
                RandomizerGet ignore = RG_NONE;
                if (!IsScenePlace(loc) && (locGet == RG_GOLD_SKULLTULA_TOKEN || IsBombchus(locGet, true) ||
                                          Rando::StaticData::RetrieveItem(locGet).GetItemType() == ITEMTYPE_SHOP)) {
                    ignore = locGet;
                }

                // Playthrough is still beatable without this item, therefore it can be removed from playthrough
                // section.
                if (IsBeatableWithout(loc, false, ignore)) {
                    ctx->playthroughLocations[i].erase(ctx->playthroughLocations[i].begin() + j);
                    toAddBackItem.push_back(loc); // Game is still beatable, don't add back until later
                } else {
                    ctx->GetItemLocation(loc)->SaveDelayedItem(); // Immediately put item back so game is beatable again
                }
            }
        }
    }

    // The spheres were numbered before the passes above, which can drop what a step counted on in an earlier sphere.
    // So the route is walked again over the steps it kept, and no step stands in a sphere before Link can reach it
    // with what the spheres before it gave him. A walk that stops short of Ganon would give the spoiler log a route
    // that does not finish the seed.
    ctx->playthroughLocations.clear();
    GeneratePlaythrough();
    if (!ctx->playthroughBeatable) {
        throw std::runtime_error("the route the spoiler log would give does not reach Ganon");
    }

    // Now we can add back items which were removed previously
    for (RandomizerCheck loc : toAddBackItem) {
        ctx->GetItemLocation(loc)->SaveDelayedItem();
    }
}

// Very similar to PareDownPlaythrough except it sets WotH candidacy of Way of the Hero items
// Way of the Hero items are more specific than playthrough items in that they are items which *must*
//  be obtained to logically be able to complete the seed, rather than playthrough items which
//  are just possible items you *can* collect to complete the seed.
static void CalculateWotH() {
    auto ctx = Rando::Context::GetInstance();
    // size - 1 so Triforce is not counted
    for (size_t i = 0; i < ctx->playthroughLocations.size() - 1; i++) {
        for (size_t j = 0; j < ctx->playthroughLocations[i].size(); j++) {
            // If removing this item and no other item caused the game to become unbeatable, then it is strictly
            // necessary.
            auto itemLoc = ctx->GetItemLocation(ctx->playthroughLocations[i][j]);
            if (itemLoc->IsHintable() && !IsBeatableWithout(ctx->playthroughLocations[i][j], true)) {
                itemLoc->SetWothCandidate();
            }
        }
    }
}

// Calculate barren locations and assign Barren Candidacy to all locations inside those areas
static bool IsRewardPlace(RandomizerCheck location) {
    return Rando::StaticData::GetLocation(location)->GetRCType() == RCTYPE_DUNGEON_REWARD && location != RC_GANON;
}

static void CalculateBarren(const std::vector<RemakeShopSlot>& kept) {
    auto ctx = Rando::Context::GetInstance();
    std::array<bool, RCAREA_INVALID + 1> NotBarren = {};

    // When shop shields/tunics are gated behind finding a shield, those items become relevant, so
    // regions holding a shield or tunic should not be hinted foolish.
    const bool shieldTunicGate = ctx->GetOption(RSK_SHOP_SHIELDS_AND_TUNICS_ONLY_REFILL).Is(RO_GENERIC_ON);

    for (RandomizerCheck loc : ctx->allLocations) {
        Rando::ItemLocation* itemLoc = ctx->GetItemLocation(loc);
        // If a location has a major item or is a way of the hero location, it is not barren. The remake's gates wait
        // for the bosses and for scenes, so a place that hands over a stone or a medallion, or whose scene later steps
        // wait for, keeps its area from being foolish whatever it holds, as it does when it holds its own item.
        if (itemLoc->GetPlacedItem().IsMajorItem() || itemLoc->IsWothCandidate() || IsRewardPlace(loc) ||
            IsScenePlace(loc) ||
            (shieldTunicGate && itemLoc->GetPlacedItem().IsShieldOrTunic())) {
            NotBarren[Rando::StaticData::GetLocation(loc)->GetArea()] = true;
        }
    }
    // A kept shelf is never a way of the hero place, since no hint names one. One whose goods the seed cannot be
    // finished without keeps its area from being foolish too.
    for (const std::vector<RandomizerCheck>& sphere : ctx->playthroughLocations) {
        for (const RandomizerCheck loc : sphere) {
            const bool keptShelf = std::ranges::find(kept, loc, &RemakeShopSlot::location) != kept.end();
            if (keptShelf && !IsBeatableWithout(loc, true)) {
                NotBarren[Rando::StaticData::GetLocation(loc)->GetArea()] = true;
            }
        }
    }

    // The way of the hero asks only about the places a hint can name, one at a time, and neither a dungeon's own small
    // key nor a shop's goods is a major item. So by that rule alone a needed key in its own dungeon, a needed good on a
    // kept shop shelf, or tokens a reward counts only together keep no area from being foolish. An area stays foolish
    // only if the seed can be finished with every place of it in allLocations emptied at once. Ganon's place is the
    // goal itself.
    std::map<RandomizerCheckArea, std::vector<RandomizerCheck>> areas;
    for (RandomizerCheck loc : ctx->allLocations) {
        const RandomizerCheckArea area = Rando::StaticData::GetLocation(loc)->GetArea();
        if (!NotBarren[area] && loc != RC_GANON && ctx->GetItemLocation(loc)->GetPlacedRandomizerGet() != RG_NONE) {
            areas[area].push_back(loc);
        }
    }
    for (const auto& [area, places] : areas) {
        for (RandomizerCheck loc : places) {
            ctx->GetItemLocation(loc)->DelayItem();
        }
        logic->Reset();
        NotBarren[area] = !CheckBeatable();
        for (RandomizerCheck loc : places) {
            ctx->GetItemLocation(loc)->SaveDelayedItem();
        }
    }

    for (RandomizerCheck loc : ctx->allLocations) {
        if (!NotBarren[Rando::StaticData::GetLocation(loc)->GetArea()]) {
            ctx->GetItemLocation(loc)->SetBarrenCandidate();
        }
    }
}

// Will place things completely randomly, no logic checks are performed
static void FastFill(std::vector<RandomizerGet> items, std::vector<RandomizerCheck> locations) {
    auto ctx = Rando::Context::GetInstance();
    while (!locations.empty() && !items.empty()) {
        RandomizerCheck loc = RandomElement(locations, true);
        ctx->GetItemLocation(loc)->SetAsHintable();
        ctx->PlaceItemInLocation(loc, RandomElement(items, true));
    }
}

// Whether a gift group shuffles the 100-token reward. Then the fill keeps the reward in reach as it keeps Ganon in
// reach: Ganon asks nothing of it, so the fill would place the rest of the items anywhere once Ganon is in reach, and a
// token placed on the reward, or one that only the reward's own item reaches, leaves the reward to a player who can
// hold 99.
static bool hundredTokenRewardShuffled = false;

// Whether the items placed so far reach the 100-token reward, or no group shuffles it.
static bool HundredTokenRewardInReach() {
    if (!hundredTokenRewardShuffled) {
        return true;
    }
    logic->Reset();
    ReachabilitySearch({});
    return Rando::Context::GetInstance()->GetItemLocation(RC_KAK_100_GOLD_SKULLTULA_REWARD)->IsAddedToPool();
}

/*
| The algorithm places items in the world in reverse.
| This means we first assume we have every item in the item pool and
| remove an item and try to place it somewhere that is still reachable
| This method helps distribution of items locked behind many requirements.
| - OoT Randomizer
*/
static void AssumedFill(const std::vector<RandomizerGet>& items, const std::vector<RandomizerCheck>& allowedLocations,
                        bool setLocationsAsHintable = false) {
    auto ctx = Rando::Context::GetInstance();
    if (items.size() > allowedLocations.size()) {
        SPDLOG_ERROR("ERROR: MORE ITEMS THAN LOCATIONS IN GIVEN LISTS");
        SPDLOG_DEBUG("Items:");
        // NOLINTNEXTLINE(clang-diagnostic-unused-variable)
        for (const RandomizerGet item : items) {
            SPDLOG_DEBUG("\t{}", Rando::StaticData::RetrieveItem(item).GetName().GetEnglish());
        }
        SPDLOG_DEBUG("\nAllowed Locations:");
        // NOLINTNEXTLINE(clang-diagnostic-unused-variable)
        for (const RandomizerCheck loc : allowedLocations) {
            SPDLOG_DEBUG("\t{}", Rando::StaticData::GetLocation(loc)->GetName());
        }
        placementFailure = true;
        return;
    }

    if (ctx->GetOption(RSK_NO_LOGIC).Is(RO_GENERIC_ON)) {
        FastFill(items, GetEmptyLocations(allowedLocations));
        return;
    }

    // keep retrying to place everything until it works or takes too long
    int retries = 10;
    bool unsuccessfulPlacement = false;
    std::vector<RandomizerCheck> attemptedLocations;
    do {
        retries--;
        if (retries <= 0) {
            placementFailure = true;
            return;
        }
        unsuccessfulPlacement = false;
        std::vector<RandomizerGet> itemsToPlace = items;

        // copy all not yet placed advancement items so that we can apply their effects for the fill algorithm
        std::vector<RandomizerGet> itemsToNotPlace =
            FilterFromPool(itemPool, [](const auto i) { return Rando::StaticData::RetrieveItem(i).IsAdvancement(); });

        // shuffle the order of items to place
        Shuffle(itemsToPlace);
        while (!itemsToPlace.empty()) {
            RandomizerGet item = std::move(itemsToPlace.back());
            Rando::StaticData::RetrieveItem(item).SetAsPlaythrough();
            itemsToPlace.pop_back();

            // assume we have all unplaced items
            logic->Reset();
            for (RandomizerGet unplacedItem : itemsToPlace) {
                Rando::StaticData::RetrieveItem(unplacedItem).ApplyEffect();
            }
            for (RandomizerGet unplacedItem : itemsToNotPlace) {
                Rando::StaticData::RetrieveItem(unplacedItem).ApplyEffect();
            }

            // get all accessible locations that are allowed
            const std::vector<RandomizerCheck> accessibleLocations = ReachabilitySearch(allowedLocations);

            // retry if there are no more locations to place items
            if (accessibleLocations.empty()) {
                SPDLOG_DEBUG("CANNOT PLACE {}. TRYING_AGAIN...",
                             Rando::StaticData::RetrieveItem(item).GetName().GetEnglish());

                // reset any locations that got an item
                for (RandomizerCheck loc : attemptedLocations) {
                    ctx->GetItemLocation(loc)->SetPlacedItem(RG_NONE);
                }
                attemptedLocations.clear();

                unsuccessfulPlacement = true;
                break;
            }

            // place the item within one of the allowed locations
            RandomizerCheck selectedLocation = RandomElement(accessibleLocations);
            ctx->PlaceItemInLocation(selectedLocation, item);
            attemptedLocations.push_back(selectedLocation);

            // This tells us the location went through the randomization algorithm
            // to distinguish it from locations which did not or that the player already
            // knows
            if (setLocationsAsHintable) {
                ctx->GetItemLocation(selectedLocation)->SetAsHintable();
            }

            // If ACR is off, then we check beatability after placing the item.
            // If the game is beatable, then we can stop placing items with logic.
            if (!ctx->GetOption(RSK_ALL_CHECKS_REACHABLE)) {
                logic->Reset();
                if (CheckBeatable() && HundredTokenRewardInReach()) {
                    SPDLOG_DEBUG("Game beatable, now placing items randomly. {} major items remaining",
                                 itemsToPlace.size());
                    FastFill(itemsToPlace, GetEmptyLocations(allowedLocations));
                    return;
                }
            }
        }
    } while (unsuccessfulPlacement);
}

static std::vector<const RemakeChest*> PlaceUnshuffledItems(const std::vector<RemakeChest>& chests,
                                                            const RemakeShops& shops,
                                                            const std::vector<RemakeGift>& gifts,
                                                            const std::vector<RemakeSkulltula>& skulltulas,
                                                            const std::vector<RemakeSongPlace>& songs) {
    auto ctx = Rando::Context::GetInstance();
    std::map<RandomizerCheck, const RemakeChest*> chestAt;
    for (const RemakeChest& chest : chests) {
        chestAt.emplace(chest.location, &chest);
    }
    std::map<RandomizerCheck, RandomizerGet> pooled;
    for (const RemakeShopSlot& slot : shops.shuffled) {
        pooled.emplace(slot.location, Filler);
    }
    std::set<RandomizerCheck> kept;
    std::ranges::transform(shops.kept, std::inserter(kept, kept.end()), &RemakeShopSlot::location);
    for (const RemakeGift& gift : gifts) {
        pooled.emplace(gift.location, gift.sohItem);
    }
    for (const RemakeSkulltula& skulltula : skulltulas) {
        pooled.emplace(skulltula.location, RG_GOLD_SKULLTULA_TOKEN);
    }
    for (const RemakeSongPlace& place : songs) {
        pooled.emplace(place.location, place.sohSong);
    }
    // SoH leaves its scrubs out of the seed's locations while its own scrub shuffle is off. Kaepora keeps it off, as on
    // it would also take a reflected nut's kill from the rule of the Lost Woods bean patch's skulltula
    // (lost_woods.cpp), so a gift SoH leaves out joins the locations here.
    for (const RemakeGift& gift : gifts) {
        if (std::ranges::find(ctx->allLocations, gift.location) == ctx->allLocations.end()) {
            ctx->AddLocation(gift.location);
        }
    }
    std::vector<const RemakeChest*> shuffled;
    itemPool.clear();
    for (RandomizerCheck loc : ctx->allLocations) {
        auto chest = chestAt.find(loc);
        if (auto item = pooled.find(loc); item != pooled.end()) {
            itemPool.push_back(item->second);
        } else if (kept.contains(loc)) {
            // The fill puts SoH's goods here.
        } else if ((chest == chestAt.end() || !chest->second->shuffled) && ctx->GetItemLocation(loc)->IsExcluded()) {
            // A kept place that is excluded holds junk, as SoH's own fill gives one.
            ctx->GetItemLocation(loc)->SetPlacedItem(Filler);
        } else if (chest == chestAt.end()) {
            ctx->GetItemLocation(loc)->PlaceVanillaItem();
        } else if (chest->second->shuffled) {
            shuffled.push_back(chest->second);
            itemPool.push_back(chest->second->sohItem);
        } else {
            ctx->GetItemLocation(loc)->SetPlacedItem(chest->second->sohItem);
        }
    }
    // SoH leaves these out of the seed's checks while their shuffle is off, but its logic still collects their items.
    for (RandomizerCheck loc :
         { RC_TOT_MASTER_SWORD, RC_KAK_100_GOLD_SKULLTULA_REWARD, RC_LH_HYRULE_LOACH, RC_ZR_FROGS_ZELDAS_LULLABY,
           RC_ZR_FROGS_EPONAS_SONG, RC_ZR_FROGS_SARIAS_SONG, RC_ZR_FROGS_SUNS_SONG, RC_ZR_FROGS_SONG_OF_TIME }) {
        if (!pooled.contains(loc)) {
            ctx->GetItemLocation(loc)->PlaceVanillaItem();
        }
    }
    return shuffled;
}

// A remake key, map or compass names no dungeon: the chest gives the one of the scene it stands in. Keys go first, as
// maps and compasses open nothing.
static void PlaceDungeonItems(const std::vector<const RemakeChest*>& shuffled) {
    std::map<std::string, std::vector<const RemakeChest*>> byFolder;
    for (const RemakeChest* chest : shuffled) {
        byFolder[chest->Folder()].push_back(chest);
    }
    for (const auto& chests : std::views::values(byFolder)) {
        std::vector<RandomizerCheck> locations;
        std::vector<RandomizerGet> keys;
        std::vector<RandomizerGet> mapsAndCompasses;
        for (const RemakeChest* chest : chests) {
            locations.push_back(chest->location);
            ItemType type = Rando::StaticData::RetrieveItem(chest->sohItem).GetItemType();
            if (type == ITEMTYPE_SMALLKEY || type == ITEMTYPE_BOSSKEY) {
                keys.push_back(chest->sohItem);
            } else if (type == ITEMTYPE_MAP || type == ITEMTYPE_COMPASS) {
                mapsAndCompasses.push_back(chest->sohItem);
            } else {
                continue;
            }
            itemPool.erase(std::ranges::find(itemPool, chest->sohItem));
        }
        AssumedFill(keys, locations);
        AssumedFill(mapsAndCompasses, locations);
    }
}

// Resets the world graph, the starting inventory, the prices and the scenes' places, and gives every location but the
// shuffled chests, shop slots, gifts, Gold Skulltulas and song places what the unmodified game gives there. The
// shuffled chests', gifts' and song places' items, a Red Rupee for each shuffled shop slot and a token for each
// shuffled skulltula make up the item pool, and each shuffled shop slot takes a price. Returns the shuffled chests.
std::vector<const RemakeChest*> SetUpWorld(const std::vector<RemakeChest>& chests, const RemakeShops& shops,
                                           const std::vector<RemakeGift>& gifts,
                                           const std::vector<RemakeSkulltula>& skulltulas,
                                           const std::vector<RemakeSongPlace>& songs) {
    auto ctx = Rando::Context::GetInstance();
    scenePlaces.assign(std::begin(everySeedsScenePlaces), std::end(everySeedsScenePlaces));
    if (std::ranges::find(gifts, zeldasCourtyard.first, &RemakeGift::location) != gifts.end()) {
        scenePlaces.push_back(zeldasCourtyard);
    }
    hundredTokenRewardShuffled =
        std::ranges::find(gifts, RC_KAK_100_GOLD_SKULLTULA_REWARD, &RemakeGift::location) != gifts.end();
    RegionTable_Init(); // Reset the world graph to initialize the proper locations
    ctx->ItemReset();
    ctx->GenerateLocationPool();
    std::vector<const RemakeChest*> shuffled = PlaceUnshuffledItems(chests, shops, gifts, skulltulas, songs);
    GenerateStartingInventory();

    for (const RemakeShopSlot& slot : shops.shuffled) {
        ctx->GetItemLocation(slot.location)
            ->SetCustomPrice(GetRandomPrice(Rando::StaticData::GetLocation(slot.location), shopsanityPrices));
    }

    // Add prices to scrubs
    auto scrubLoc = Rando::StaticData::GetScrubLocations();
    if (ctx->GetOption(RSK_SHUFFLE_SCRUBS).Is(RO_SCRUBS_ALL)) {
        for (size_t i = 0; i < scrubLoc.size(); i++) {
            ctx->GetItemLocation(scrubLoc[i])
                ->SetCustomPrice(GetRandomPrice(Rando::StaticData::GetLocation(scrubLoc[i]), scrubPrices));
        }
    } else {
        for (size_t i = 0; i < scrubLoc.size(); i++) {
            ctx->GetItemLocation(scrubLoc[i])
                ->SetCustomPrice(Rando::StaticData::GetLocation(scrubLoc[i])->GetVanillaPrice());
        }
    }

    // set merchant prices
    if (ctx->GetOption(RSK_SHUFFLE_MERCHANTS).Is(RO_SHUFFLE_MERCHANTS_BEANS_ONLY) ||
        ctx->GetOption(RSK_SHUFFLE_MERCHANTS).Is(RO_SHUFFLE_MERCHANTS_ALL)) {
        ctx->GetItemLocation(RC_ZR_MAGIC_BEAN_SALESMAN)
            ->SetCustomPrice(
                GetRandomPrice(Rando::StaticData::GetLocation(RC_ZR_MAGIC_BEAN_SALESMAN), merchantPrices));
    } else {
        ctx->GetItemLocation(RC_ZR_MAGIC_BEAN_SALESMAN)
            ->SetCustomPrice(Rando::StaticData::GetLocation(RC_ZR_MAGIC_BEAN_SALESMAN)->GetVanillaPrice());
    }

    auto merchantLoc = Rando::StaticData::GetMerchantLocations();

    if (ctx->GetOption(RSK_SHUFFLE_MERCHANTS).Is(RO_SHUFFLE_MERCHANTS_ALL_BUT_BEANS) ||
        ctx->GetOption(RSK_SHUFFLE_MERCHANTS).Is(RO_SHUFFLE_MERCHANTS_ALL)) {
        for (size_t i = 0; i < merchantLoc.size(); i++) {
            ctx->GetItemLocation(merchantLoc[i])
                ->SetCustomPrice(GetRandomPrice(Rando::StaticData::GetLocation(merchantLoc[i]), merchantPrices));
        }
    } else {
        for (size_t i = 0; i < merchantLoc.size(); i++) {
            ctx->GetItemLocation(merchantLoc[i])
                ->SetCustomPrice(Rando::StaticData::GetLocation(merchantLoc[i])->GetVanillaPrice());
        }
    }

    return shuffled;
}

void ClearProgress() {
}

bool KeptInChestsAndCalls(RandomizerGet item) {
    return Rando::StaticData::RetrieveItem(item).GetItemType() == ITEMTYPE_DUNGEONREWARD ||
           item == RG_GERUDO_MEMBERSHIP_CARD || item == RG_WEIRD_EGG || item == RG_ZELDAS_LETTER ||
           item == RG_POCKET_EGG || item == RG_CLAIM_CHECK || item == RG_BIGGORON_SWORD;
}

bool KeptInChests(RandomizerGet item) {
    return item == RG_RUTOS_LETTER || item == RG_DINS_FIRE || item == RG_FARORES_WIND || item == RG_NAYRUS_LOVE ||
           item == RG_DOUBLE_DEFENSE || item == RG_PROGRESSIVE_MAGIC_METER;
}

bool KeptFromShops(RandomizerGet item) {
    const ItemType type = Rando::StaticData::RetrieveItem(item).GetItemType();
    return type == ITEMTYPE_SMALLKEY || type == ITEMTYPE_BOSSKEY || type == ITEMTYPE_MAP || type == ITEMTYPE_COMPASS ||
           item == RG_PROGRESSIVE_BOW || item == RG_PROGRESSIVE_SLINGSHOT || item == RG_PROGRESSIVE_BOMB_BAG ||
           item == RG_BIG_QUIVER || item == RG_BIGGEST_QUIVER || item == RG_BIG_BULLET_BAG ||
           item == RG_BIGGEST_BULLET_BAG || item == RG_BIG_BOMB_BAG || item == RG_BIGGEST_BOMB_BAG ||
           item == RG_DEKU_STICK_CAPACITY_20 || item == RG_DEKU_STICK_CAPACITY_30 || item == RG_DEKU_NUT_CAPACITY_30 ||
           item == RG_DEKU_NUT_CAPACITY_40 || item == RG_BOMBCHU_5 || item == RG_BOMBCHU_20 ||
           item == RG_EMPTY_BOTTLE || item == RG_BOTTLE_WITH_MILK;
}

int Fill(const std::vector<RemakeChest>& chests, const std::vector<RemakeChest>& looseItems, const RemakeShops& shops,
         const std::vector<RemakeGift>& gifts,
         const std::vector<RemakeSkulltula>& skulltulas, const std::vector<RemakeSongPlace>& songs) {
    auto ctx = Rando::Context::GetInstance();
    std::vector<RemakeChest> places = chests;
    places.insert(places.end(), looseItems.begin(), looseItems.end());
    std::set<RandomizerCheck> looseLocations;
    for (const RemakeChest& looseItem : looseItems) {
        looseLocations.insert(looseItem.location);
    }
    int retries = 0;
    SPDLOG_INFO("Starting seed generation...");
    while (retries < 10) {
        SPDLOG_INFO("Attempt {}...", retries + 1);
        placementFailure = false;
        // showItemProgress = false;
        ctx->playthroughLocations.clear();
        ctx->GetEntranceShuffler()->playthroughEntrances.clear();
        std::vector<const RemakeChest*> shuffled = SetUpWorld(places, shops, gifts, skulltulas, songs);

        // Temporarily add shop items to the itemPool so that entrance randomization
        // can validate the world using deku/hylian shields
        SohUtils::AppendVector(itemPool, GetMinVanillaShopItems(8)); // assume worst case shopsanity 7
        if (ctx->GetOption(RSK_SHUFFLE_ENTRANCES)) {
            SPDLOG_INFO("Shuffling Entrances...");
            if (ctx->GetEntranceShuffler()->ShuffleAllEntrances() == ENTRANCE_SHUFFLE_FAILURE) {
                retries++;
                ClearProgress();
                continue;
            }
            SPDLOG_INFO("Shuffling Entrances Done");
        }
        // erase temporary shop items
        std::erase_if(itemPool, [](const auto item) {
            return Rando::StaticData::RetrieveItem(item).GetItemType() == ITEMTYPE_SHOP;
        });

        // Until their own step, every step counts SoH's goods for the slots a count leaves held: the logic counts only
        // a Deku Shield Link buys, and Queen Gohma's door asks for one.
        if (!shops.kept.empty()) {
            SohUtils::AppendVector(itemPool, GetMinVanillaShopItems(static_cast<int>(shops.shuffled.size())));
        }

        std::vector<RandomizerCheck> shuffledLocations;
        for (const RemakeChest* place : shuffled) {
            if (!looseLocations.contains(place->location)) {
                shuffledLocations.push_back(place->location);
            }
        }

        SPDLOG_INFO("Shuffling and Placing Dungeon Items...");
        PlaceDungeonItems(shuffled);

        // As SoH places songs at song locations: once the dungeon items are in their own dungeons, the songs leave the
        // pool for the song places.
        if (!songs.empty()) {
            std::vector<RandomizerCheck> songPlaces;
            std::ranges::transform(songs, std::back_inserter(songPlaces), &RemakeSongPlace::location);
            AssumedFill(FilterAndEraseFromPool(itemPool,
                                               [](RandomizerGet item) {
                                                   return Rando::StaticData::RetrieveItem(item).GetItemType() ==
                                                          ITEMTYPE_SONG;
                                               }),
                        songPlaces, true);
        }

        // The goods take their slots once the songs hold theirs, each at the remake's price for it. A child in a closed
        // forest learns no song before he leaves it, and with no scale to dive out of the Lost Woods he leaves only
        // past Queen Gohma's door, which asks for the one Deku Shield on sale. With the songs and a started ocarina
        // counted held, the shield could go to a shop only a warp reaches, and the songs would then have no place he
        // reaches. A fill that gives up leaves its slots empty for the next try.
        if (!shops.kept.empty()) {
            std::vector<RandomizerCheck> kept;
            std::ranges::transform(shops.kept, std::back_inserter(kept), &RemakeShopSlot::location);
            AssumedFill(FilterAndEraseFromPool(itemPool,
                                               [](RandomizerGet item) {
                                                   return Rando::StaticData::RetrieveItem(item).GetItemType() ==
                                                          ITEMTYPE_SHOP;
                                               }),
                        kept);
            if (!placementFailure) {
                for (const RandomizerCheck loc : kept) {
                    Rando::ItemLocation* place = ctx->GetItemLocation(loc);
                    place->SetCustomPrice(shops.goods.at(place->GetPlacedRandomizerGet()).price);
                }
            }
        }

        // As SoH places the rewards at "End of Dungeons": among the places that hand one over, which the generator
        // gives the fill as gifts.
        if (ctx->GetOption(RSK_SHUFFLE_DUNGEON_REWARDS).Is(RO_DUNGEON_REWARDS_END_OF_DUNGEON)) {
            std::vector<RandomizerCheck> rewardPlaces;
            for (const RemakeGift& gift : gifts) {
                if (IsRewardPlace(gift.location)) {
                    rewardPlaces.push_back(gift.location);
                }
            }
            AssumedFill(FilterAndEraseFromPool(itemPool,
                                               [](RandomizerGet item) {
                                                   return Rando::StaticData::RetrieveItem(item).GetItemType() ==
                                                          ITEMTYPE_DUNGEONREWARD;
                                               }),
                        rewardPlaces);
        }

        AssumedFill(FilterAndEraseFromPool(itemPool, KeptInChests), shuffledLocations, true);

        std::vector<RandomizerCheck> chestsAndCalls = shuffledLocations;
        for (const RemakeGift& gift : gifts) {
            chestsAndCalls.push_back(gift.location);
        }
        AssumedFill(FilterAndEraseFromPool(itemPool, KeptInChestsAndCalls), chestsAndCalls);

        for (const RemakeChest& looseItem : looseItems) {
            shuffledLocations.push_back(looseItem.location);
        }
        for (const RemakeGift& gift : gifts) {
            shuffledLocations.push_back(gift.location);
        }
        if (!shops.shuffled.empty()) {
            AssumedFill(FilterAndEraseFromPool(itemPool, KeptFromShops), shuffledLocations);
            for (const RemakeShopSlot& slot : shops.shuffled) {
                shuffledLocations.push_back(slot.location);
            }
        }
        for (const RemakeSkulltula& skulltula : skulltulas) {
            shuffledLocations.push_back(skulltula.location);
        }

        SPDLOG_INFO("Shuffling Advancement Items");
        // Then place the rest of the advancement items
        std::vector<RandomizerGet> advancementItems = FilterAndEraseFromPool(
            itemPool, [](const auto i) { return Rando::StaticData::RetrieveItem(i).IsAdvancement(); });
        AssumedFill(advancementItems, shuffledLocations, true);

        // Fast fill for the rest of the pool
        SPDLOG_INFO("Shuffling Remaining Items");
        FastFill(std::move(itemPool), GetEmptyLocations(shuffledLocations));

        routeTakesScenes = true;
        std::vector<RandomizerCheck> lateLocations = GeneratePlaythrough();
        // Successful placement, produced beatable result
        if (ctx->playthroughBeatable && !placementFailure) {
            SPDLOG_INFO("Calculating Playthrough...");
            PareDownPlaythrough(lateLocations);
            routeTakesScenes = false;
            CalculateWotH();
            CalculateBarren(shops.kept);
            SPDLOG_INFO("Calculating Playthrough Done");

            ctx->GetEntranceShuffler()->CreateEntranceOverrides();

            SPDLOG_DEBUG("Number of retries {}", retries);
            return 1;
        }
        routeTakesScenes = false;
        // Unsuccessful placement
        if (retries < 9) {
            SPDLOG_DEBUG("Failed to generate a beatable seed. Retrying...");
            Regions::ResetAllLocations();
            logic->Reset();
            ClearProgress();
        }
        retries++;
    }
    // All retries failed
    return -1;
}
