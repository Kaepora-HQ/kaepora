#include "soh/Enhancements/randomizer/location_access.h"
#include "soh/Enhancements/randomizer/entrance.h"

using namespace Rando;

void RegionTable_Init_TempleOfTime() {
    // clang-format off
    areaTable[RR_TOT_ENTRANCE] = Region("ToT Entrance", SCENE_TEMPLE_OF_TIME_EXTERIOR_DAY, {
        //Events
        EVENT_ACCESS(LOGIC_FAIRY_ACCESS, logic->CallGossipFairyExceptSuns()),
    }, {
        //Locations
        LOCATION(RC_TOT_LEFTMOST_GOSSIP_STONE_FAIRY,         logic->CallGossipFairyExceptSuns() || (logic->CanUse(RG_SUNS_SONG) && logic->IsAdult)),
        LOCATION(RC_TOT_LEFTMOST_GOSSIP_STONE_FAIRY_BIG,     logic->CanUse(RG_SONG_OF_STORMS)),
        LOCATION(RC_TOT_LEFT_CENTER_GOSSIP_STONE_FAIRY,      logic->CallGossipFairyExceptSuns() || (logic->CanUse(RG_SUNS_SONG) && logic->IsAdult)),
        LOCATION(RC_TOT_LEFT_CENTER_GOSSIP_STONE_FAIRY_BIG,  logic->CanUse(RG_SONG_OF_STORMS)),
        LOCATION(RC_TOT_RIGHT_CENTER_GOSSIP_STONE_FAIRY,     logic->CallGossipFairyExceptSuns() || (logic->CanUse(RG_SUNS_SONG) && logic->IsAdult)),
        LOCATION(RC_TOT_RIGHT_CENTER_GOSSIP_STONE_FAIRY_BIG, logic->CanUse(RG_SONG_OF_STORMS)),
        LOCATION(RC_TOT_RIGHTMOST_GOSSIP_STONE_FAIRY,        logic->CallGossipFairyExceptSuns() || (logic->CanUse(RG_SUNS_SONG) && logic->IsAdult)),
        LOCATION(RC_TOT_RIGHTMOST_GOSSIP_STONE_FAIRY_BIG,    logic->CanUse(RG_SONG_OF_STORMS)),
        LOCATION(RC_TOT_LEFTMOST_GOSSIP_STONE,               true),
        LOCATION(RC_TOT_LEFT_CENTER_GOSSIP_STONE,            true),
        LOCATION(RC_TOT_RIGHT_CENTER_GOSSIP_STONE,           true),
        LOCATION(RC_TOT_RIGHTMOST_GOSSIP_STONE,              true),
    }, {
        //Exits
        ENTRANCE(RR_THE_MARKET,     true),
        ENTRANCE(RR_TEMPLE_OF_TIME, true),
    });

    areaTable[RR_TEMPLE_OF_TIME] = Region("Temple of Time", SCENE_TEMPLE_OF_TIME, {
        //Events
        // Zelda waits once the Shadow and the Spirit sages have woken, in the scenes after their bosses. Her scene
        // moves the Ganon quest on, and the bridge to Ganon's Castle follows the quest, whatever her place gives.
        EVENT_ACCESS(LOGIC_SHEIK_REVEALS_ZELDA, logic->IsAdult && logic->Get(LOGIC_SHADOW_TEMPLE_CLEAR) && logic->Get(LOGIC_SPIRIT_TEMPLE_CLEAR)),
    }, {
        //Locations
        LOCATION(RC_TOT_LIGHT_ARROWS_CUTSCENE, logic->IsAdult && logic->Get(LOGIC_SHADOW_TEMPLE_CLEAR) && logic->Get(LOGIC_SPIRIT_TEMPLE_CLEAR)),
        LOCATION(RC_ALTAR_HINT_CHILD,          logic->IsChild),
        LOCATION(RC_ALTAR_HINT_ADULT,          logic->IsAdult),
        LOCATION(RC_TOT_SHEIK_HINT,            logic->IsAdult && logic->HasItem(RG_SPEAK_HYLIAN)),
        LOCATION(RC_TOT_ALTAR,                 logic->CanRead()),
    }, {
        //Exits
        ENTRANCE(RR_TOT_ENTRANCE,            true),
        // The altar opens the door to the Song of Time on either ocarina once the door's quest is past Step04. Zelda's
        // scene at the moat writes Step05, whatever its place gives.
        ENTRANCE(RR_TOT_BEYOND_DOOR_OF_TIME, ctx->GetOption(RSK_DOOR_OF_TIME).Is(RO_DOOROFTIME_OPEN) || (logic->CanUse(RG_SONG_OF_TIME) && (ctx->GetOption(RSK_DOOR_OF_TIME).Is(RO_DOOROFTIME_SONGONLY) || logic->Get(LOGIC_MOAT_SCENE)))),
    });

    areaTable[RR_TOT_BEYOND_DOOR_OF_TIME] = Region("Beyond Door of Time", SCENE_TEMPLE_OF_TIME, {}, {
        //Locations
        LOCATION(RC_TOT_MASTER_SWORD, logic->IsAdult),
        LOCATION(RC_GIFT_FROM_RAURU,  logic->IsAdult),
        // Sheik waits at the pedestal once the Forest Temple's blue warp has completed its quest.
        LOCATION(RC_SHEIK_AT_TEMPLE,  logic->IsAdult && logic->Get(LOGIC_FOREST_TEMPLE_CLEAR)),
    }, {
        //Exits
        ENTRANCE(RR_TEMPLE_OF_TIME, true),
    });

    // clang-format on
}
