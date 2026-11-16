#include "fill.hpp"

#include "../dungeon.h"
#include "../SeedContext.h"
#include "item_pool.hpp"
#include "starting_inventory.hpp"
#include "hints.hpp"
#include "../rng.h"
#include "shops.hpp"
#include "pool_functions.hpp"
#include "soh/Enhancements/randomizer/static_data.h"
#include "soh/Enhancements/debugger/performanceTimer.h"
#include "soh/util.h"

#include <algorithm>
#include <bitset>
#include <vector>
#include <list>
#include <map>
#include <ranges>
#include <set>
#include <string>
#include <spdlog/spdlog.h>

using namespace Rando;

static bool placementFailure = false;

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
    // Reached after wincon, so never part of playthrough. Pare first
    if (ctx->playthroughBeatable && location->GetPlacedItem().IsAdvancement()) {
        gals.lateLocations.push_back(loc);
    }
    // Item is an advancement item, figure out if it should be added to this sphere
    if (!ctx->playthroughBeatable && location->GetPlacedItem().IsAdvancement()) {
        ItemType type = location->GetPlacedItem().GetItemType();

        // Decide whether to exclude this location
        // This preprocessing is done to reduce the amount of searches performed in PareDownPlaythrough
        // Want to exclude:
        // 1) Tokens after the last potentially useful one (the last one that gives an advancement item or last for
        // token bridge) 2) Buy items of the same type, after the first (So only see Buy Deku Nut of any amount once)
        bool exclude = true;
        // Exclude tokens after the last possibly useful one
        if (type == ITEMTYPE_TOKEN && gals.gsCount < gals.maxGsCount) {
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
        loc->ApplyPlacedItemEffect();
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

void LookForExternalArea(Region* currentRegion, std::set<Region*>& alreadyChecked, std::set<RandomizerArea>& areas,
                         bool LowPriorityMode = false) {
    for (const auto& entrance : currentRegion->entrances) {
        // if the region is arealess and hasn't already been checked, recursivly check what connects to it
        // if this entrance does not pass areas, only process it if we are in low priority mode
        if ((LowPriorityMode || entrance->DoesSpreadAreas()) && !alreadyChecked.contains(entrance->GetParentRegion())) {
            std::set<RandomizerArea> otherAreas = entrance->GetParentRegion()->GetAllAreas();
            if (otherAreas.size() == 0) {
                alreadyChecked.insert(entrance->GetParentRegion());
                LookForExternalArea(entrance->GetParentRegion(), alreadyChecked, areas, LowPriorityMode);
                // If we find a valid area we should add it.
                // If it's Links Pocket or RA_NONE, do not propagate those, they are not real areas.
                // This check is likely to fail if a region somehow is both in Link's Pocket and elsewhere, but this
                // should never happen
            } else if (*otherAreas.begin() > RA_LINKS_POCKET) {
                areas.merge(otherAreas);
            }
        }
    }
}

void SetAreas() {
    auto ctx = Rando::Context::GetInstance();
    // RANDOTODO give entrances an enum like RandomizerCheck, then give them all areas here,
    // then use those areas to not need to recursively find ItemLocation areas when identifying entrance's area
    for (int regionType = 0; regionType < RR_MAX; regionType++) {
        Region* region = &areaTable[regionType];
        std::set<RandomizerArea> areas = region->GetAllAreas();
        std::set<Region*> regionsToSet = { region };
        if (areas.empty()) {
            LookForExternalArea(region, regionsToSet, areas);
            // If we found nothing, try again in low priority mode to try every entrance
            if (areas.empty()) {
                LookForExternalArea(region, regionsToSet, areas, true);
                // If we still found nothing, we're disconnected, use RA_NONE to represent that
                if (areas.empty()) {
                    areas.insert(RA_NONE);
                }
            }
        }
        for (auto regionToSet : regionsToSet) {
            regionToSet->ReplaceAreas(areas);
            for (auto& loc : regionToSet->locations) {
                ctx->GetItemLocation(loc.GetLocation())->MergeAreas(areas);
            }
        }
    }
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
    // Start at sphere before Ganon's and count down
    for (int32_t i = static_cast<int32_t>(ctx->playthroughLocations.size()) - 2; i >= 0; i--) {
        // Check each item location in sphere
        std::vector<RandomizerCheck> sphere = ctx->playthroughLocations.at(i);
        for (int32_t j = static_cast<int32_t>(sphere.size()) - 1; j >= 0; j--) {
            RandomizerCheck loc = sphere.at(j);
            RandomizerGet locGet = ctx->GetItemLocation(loc)->GetPlacedRandomizerGet();

            RandomizerGet ignore = RG_NONE;
            if (locGet == RG_GOLD_SKULLTULA_TOKEN || IsBombchus(locGet, true) ||
                Rando::StaticData::RetrieveItem(locGet).GetItemType() == ITEMTYPE_SHOP) {
                ignore = locGet;
            }

            // Playthrough is still beatable without this item, therefore it can be removed from playthrough section.
            if (IsBeatableWithout(loc, false, ignore)) {
                ctx->playthroughLocations[i].erase(ctx->playthroughLocations[i].begin() + j);
                toAddBackItem.push_back(loc); // Game is still beatable, don't add back until later
            } else {
                ctx->GetItemLocation(loc)->SaveDelayedItem(); // Immediately put item back so game is beatable again
            }
        }
    }

    // Some spheres may now be empty, remove these
    for (int32_t i = static_cast<int32_t>(ctx->playthroughLocations.size()) - 2; i >= 0; i--) {
        if (ctx->playthroughLocations.at(i).size() == 0) {
            ctx->playthroughLocations.erase(ctx->playthroughLocations.begin() + i);
        }
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
            // necessary, so add it unless it is in Links Pocket or an isolated place.
            auto itemLoc = ctx->GetItemLocation(ctx->playthroughLocations[i][j]);
            if (itemLoc->IsHintable() && itemLoc->GetFirstArea() > RA_LINKS_POCKET &&
                !IsBeatableWithout(ctx->playthroughLocations[i][j], true)) {
                itemLoc->SetWothCandidate();
            }
        }
    }
    ctx->playthroughBeatable = true;
    logic->Reset();
    ReachabilitySearch(ctx->allLocations);
}

static bool FindIfBarren(Rando::ItemLocation* itemLoc, const std::array<bool, RA_MAX>& NotBarren) {
    for (auto locArea : itemLoc->GetAreas()) {
        if (NotBarren[locArea]) {
            return false;
        }
    }
    return true;
}

// Calculate barren locations and assign Barren Candidacy to all locations inside those areas
static void CalculateBarren() {
    auto ctx = Rando::Context::GetInstance();
    std::array<bool, RA_MAX>
        NotBarren = {}; // I would invert this but the "initialise all as true" syntax wasn't working
    // Isolated Areas and Link's Pocket are never Hinted Barren
    NotBarren[RA_NONE] = true;
    NotBarren[RA_LINKS_POCKET] = true;

    // When shop shields/tunics are gated behind finding a shield, those items become relevant, so
    // regions holding a shield or tunic should not be hinted foolish.
    const bool shieldTunicGate = ctx->GetOption(RSK_SHOP_SHIELDS_AND_TUNICS_ONLY_REFILL).Is(RO_GENERIC_ON);

    for (RandomizerCheck loc : ctx->allLocations) {
        Rando::ItemLocation* itemLoc = ctx->GetItemLocation(loc);
        for (auto locArea : itemLoc->GetAreas()) {
            // If a location has a major item or is a way of the hero location, it is not barren
            if (NotBarren[locArea] == false && (itemLoc->GetPlacedItem().IsMajorItem() || itemLoc->IsWothCandidate() ||
                                                (shieldTunicGate && itemLoc->GetPlacedItem().IsShieldOrTunic()))) {
                NotBarren[locArea] = true;
            }
        }
    }

    for (RandomizerCheck loc : ctx->allLocations) {
        Rando::ItemLocation* itemLoc = ctx->GetItemLocation(loc);
        if (FindIfBarren(itemLoc, NotBarren)) {
            itemLoc->SetBarrenCandidate();
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
                if (CheckBeatable()) {
                    SPDLOG_DEBUG("Game beatable, now placing items randomly. {} major items remaining",
                                 itemsToPlace.size());
                    FastFill(itemsToPlace, GetEmptyLocations(allowedLocations));
                    return;
                }
            }
        }
    } while (unsuccessfulPlacement);
}

// The remake lets a mod change only which item a chest holds. Every location keeps what the unmodified game gives
// there, and the items of the shuffled chests make up the pool: nothing added, nothing removed.
static std::vector<const RemakeChest*> PlaceUnshuffledItems(const std::vector<RemakeChest>& chests) {
    auto ctx = Rando::Context::GetInstance();
    std::map<RandomizerCheck, const RemakeChest*> chestAt;
    for (const RemakeChest& chest : chests) {
        chestAt.emplace(chest.location, &chest);
    }
    std::vector<const RemakeChest*> shuffled;
    itemPool.clear();
    for (RandomizerCheck loc : ctx->allLocations) {
        if (Rando::StaticData::GetLocation(loc)->GetActorID() != ACTOR_EN_BOX) {
            ctx->GetItemLocation(loc)->PlaceVanillaItem();
        } else if (const RemakeChest* chest = chestAt.at(loc); chest->shuffled) {
            shuffled.push_back(chest);
            itemPool.push_back(chest->sohItem);
        } else {
            ctx->GetItemLocation(loc)->SetPlacedItem(chest->sohItem);
        }
    }
    // SoH leaves these out of the seed's checks while their shuffle is off, but its logic still collects their items.
    for (RandomizerCheck loc :
         { RC_TOT_MASTER_SWORD, RC_KAK_100_GOLD_SKULLTULA_REWARD, RC_LH_HYRULE_LOACH, RC_ZR_FROGS_ZELDAS_LULLABY,
           RC_ZR_FROGS_EPONAS_SONG, RC_ZR_FROGS_SARIAS_SONG, RC_ZR_FROGS_SUNS_SONG, RC_ZR_FROGS_SONG_OF_TIME }) {
        ctx->GetItemLocation(loc)->PlaceVanillaItem();
    }
    return shuffled;
}

// A remake key, map or compass names no dungeon: the chest gives the one of the scene it stands in. So these move only
// between shuffled chests in the same Banc folder. Keys go first, as maps and compasses open nothing.
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

// Resets the world graph, the starting inventory and the prices, and gives every location but the shuffled chests what
// the unmodified game gives there. The shuffled chests' items make up the item pool. Returns the shuffled chests.
std::vector<const RemakeChest*> SetUpWorld(const std::vector<RemakeChest>& chests) {
    auto ctx = Rando::Context::GetInstance();
    RegionTable_Init(); // Reset the world graph to initialize the proper locations
    ctx->ItemReset();
    ctx->GenerateLocationPool();
    std::vector<const RemakeChest*> shuffled = PlaceUnshuffledItems(chests);
    GenerateStartingInventory();

    StartPerformanceTimer(PT_SHOPSANITY);
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
    StopPerformanceTimer(PT_SHOPSANITY);

    return shuffled;
}

void ClearProgress() {
}

int Fill(const std::vector<RemakeChest>& chests) {
    auto ctx = Rando::Context::GetInstance();
    int retries = 0;
    SPDLOG_INFO("Starting seed generation...");
    while (retries < 10) {
        SPDLOG_INFO("Attempt {}...", retries + 1);
        placementFailure = false;
        // showItemProgress = false;
        ctx->playthroughLocations.clear();
        ctx->GetEntranceShuffler()->playthroughEntrances.clear();
        std::vector<const RemakeChest*> shuffled = SetUpWorld(chests);

        // Temporarily add shop items to the itemPool so that entrance randomization
        // can validate the world using deku/hylian shields
        StartPerformanceTimer(PT_ENTRANCE_SHUFFLE);
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
        SetAreas();
        // erase temporary shop items
        std::erase_if(itemPool, [](const auto item) {
            return Rando::StaticData::RetrieveItem(item).GetItemType() == ITEMTYPE_SHOP;
        });
        StopPerformanceTimer(PT_ENTRANCE_SHUFFLE);

        std::vector<RandomizerCheck> shuffledLocations;
        for (const RemakeChest* chest : shuffled) {
            shuffledLocations.push_back(chest->location);
        }

        StartPerformanceTimer(PT_OWN_DUNGEON);
        SPDLOG_INFO("Shuffling and Placing Dungeon Items...");
        PlaceDungeonItems(shuffled);
        StopPerformanceTimer(PT_OWN_DUNGEON);

        StartPerformanceTimer(PT_ADVANCEMENT_ITEMS);
        SPDLOG_INFO("Shuffling Advancement Items");
        std::vector<RandomizerGet> advancementItems = FilterAndEraseFromPool(
            itemPool, [](const auto i) { return Rando::StaticData::RetrieveItem(i).IsAdvancement(); });
        AssumedFill(advancementItems, shuffledLocations, true);
        StopPerformanceTimer(PT_ADVANCEMENT_ITEMS);

        StartPerformanceTimer(PT_REMAINING_ITEMS);
        SPDLOG_INFO("Shuffling Remaining Items");
        FastFill(std::move(itemPool), GetEmptyLocations(shuffledLocations));
        StopPerformanceTimer(PT_REMAINING_ITEMS);

        StartPerformanceTimer(PT_PLAYTHROUGH_GENERATION);
        std::vector<RandomizerCheck> lateLocations = GeneratePlaythrough();
        StopPerformanceTimer(PT_PLAYTHROUGH_GENERATION);
        // Successful placement, produced beatable result
        if (ctx->playthroughBeatable && !placementFailure) {
            SPDLOG_INFO("Calculating Playthrough...");
            StartPerformanceTimer(PT_PARE_DOWN_PLAYTHROUGH);
            PareDownPlaythrough(lateLocations);
            StopPerformanceTimer(PT_PARE_DOWN_PLAYTHROUGH);

            StartPerformanceTimer(PT_WOTH);
            CalculateWotH();
            StopPerformanceTimer(PT_WOTH);

            StartPerformanceTimer(PT_FOOLISH);
            CalculateBarren();
            StopPerformanceTimer(PT_FOOLISH);
            SPDLOG_INFO("Calculating Playthrough Done");

            StartPerformanceTimer(PT_OVERRIDES);
            ctx->CreateItemOverrides();
            ctx->GetEntranceShuffler()->CreateEntranceOverrides();
            StopPerformanceTimer(PT_OVERRIDES);

            StartPerformanceTimer(PT_HINTS);
            CreateAllHints();
            CreateWarpSongTexts();
            StopPerformanceTimer(PT_HINTS);
            SPDLOG_DEBUG("Number of retries {}", retries);
            return 1;
        }
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
