#pragma once

#include "../location_access.h"
#include "../entrance.h"
#include "generator/remake.h"

#include <vector>

// Gives Link what location holds, in the search under way. While the spoiler log's route is made, a place whose scene
// later steps wait for also sets what its scene sets (see SetByItsScenePlace).
void ApplyCheck(Rando::ItemLocation* location);

// Whether event is one that, while the spoiler log's route is made, only its scene's place sets: a dungeon clear, which
// the boss's blue warp sets, the Gerudo letting Link pass, which the scene after the last carpenter sets, Zelda's scene
// in the Temple of Time, which the bridge to Ganon's Castle follows, or King Zora's thanks, which Morpha waits for.
// Later steps wait for those scenes, which come with those places whatever they give, so the route names a place
// wherever a later step waits for its scene. The fill's own searches set each as soon as its scene can play.
bool SetByItsScenePlace(LogicVal event);

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
            ApplyCheck(location);
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
// quiver, the Goron Ruby or the empty bottle they may need, nor that a shelf sells a bag. So with shop goods shuffled
// they go to a chest, a gift or a loose spot: the fill places them before shop slots and Gold Skulltulas join it.
bool KeptFromShops(RandomizerGet item);

// The Spiritual Stones and the medallions. A chest gave the Kokiri Emerald, the Forest Medallion and the Zora Sapphire,
// each with its model, Link's hold-up pose and its named window (seen), and a gift raises an item the same way. No shop
// slot, Gold Skulltula or loose spot has been seen to give one, so the fill places them before those join it: in a
// chest, at a gift or at one of the nine places that hand one over.
bool KeptInChestsAndCalls(RandomizerGet item);

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