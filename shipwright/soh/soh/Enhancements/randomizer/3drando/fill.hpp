#pragma once

#include "../location_access.h"
#include "../entrance.h"
#include "generator/remake.h"

#include <vector>

// RANDOTODO merge into Logic once Logic is a class passed to logic funtions
struct GetAccessibleLocationsStruct {
    std::vector<RandomizerCheck> accessibleLocations;
    std::vector<RandomizerRegion> regionPool;
    // Variables for playthrough
    int gsCount;
    int maxGsCount;
    std::vector<LogicVal> buyIgnores;
    std::vector<RandomizerCheck> lateLocations;

    // Variables for search
    std::vector<Rando::ItemLocation*> newItemLocations;
    bool logicUpdated;
    bool resetSphere;

    // Variables For Validating Entrences
    bool haveTimeAccess;
    bool foundTempleOfTime;
    bool validatedStartingRegion;
    bool sphereZeroComplete;
    bool timePassChildDay;
    bool timePassChildNight;
    bool timePassAdultDay;
    bool timePassAdultNight;

    std::vector<RandomizerCheck> itemSphere;
    std::list<Rando::Entrance*> entranceSphere;

    bool calculatingAvailableChecks = false;

    GetAccessibleLocationsStruct(int _maxGsCount) {
        regionPool = { RR_ROOT };
        gsCount = 0;
        maxGsCount = _maxGsCount;
        logicUpdated = false;
        resetSphere = false;
    }

    void InitLoop() {
        logicUpdated = false;
        for (Rando::ItemLocation* location : newItemLocations) {
            location->ApplyPlacedItemEffect();
        }
        newItemLocations.clear();
        itemSphere.clear();
        entranceSphere.clear();
    }
};

void ClearProgress();
std::vector<const RemakeChest*> SetUpWorld(const std::vector<RemakeChest>& chests,
                                           const std::vector<RemakeShopSlot>& shops,
                                           const std::vector<RemakeGift>& gifts,
                                           const std::vector<RemakeSkulltula>& skulltulas,
                                           const std::vector<RemakeSongPlace>& songs);
int Fill(const std::vector<RemakeChest>& chests, const std::vector<RemakeChest>& looseItems,
         const std::vector<RemakeShopSlot>& shops, const std::vector<RemakeGift>& gifts,
         const std::vector<RemakeSkulltula>& skulltulas, const std::vector<RemakeSongPlace>& songs);

// What a shop slot never holds. Dungeon items stay in their dungeons, and PlaceDungeonItems places them before shop
// slots join the fill. The game has not shown that a shop sells the rest to a Link who lacks the bullet bag, the
// quiver, the Goron Ruby or the empty bottle they may need. So with shop goods shuffled they go to a chest, a gift or
// a loose spot: the fill places them before shop slots and Gold Skulltulas join it.
bool KeptFromShops(RandomizerGet item);

std::vector<RandomizerCheck> GetEmptyLocations(std::vector<RandomizerCheck> allowedLocations);

void ProcessRegion(Region* region, GetAccessibleLocationsStruct& gals, RandomizerGet ignore = RG_NONE,
                   bool stopOnBeatable = false, bool addToPlaythrough = false);

std::vector<RandomizerCheck> ReachabilitySearch(const std::vector<RandomizerCheck>& allowedLocations,
                                                RandomizerGet ignore = RG_NONE, bool calculatingAvailableChecks = false,
                                                RandomizerRegion startingRegion = RR_ROOT,
                                                RandoAgeTime startingAgeTime = RAT_NONE);

std::vector<RandomizerCheck> GeneratePlaythrough();

bool CheckBeatable(RandomizerGet ignore = RG_NONE);

void ValidateEntrances(bool checkOtherEntranceAccess);

void ValidateEntrances(bool checkPoeCollectorAccess, bool checkOtherEntranceAccess);