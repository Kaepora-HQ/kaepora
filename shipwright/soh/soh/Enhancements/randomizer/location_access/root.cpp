#include "soh/Enhancements/randomizer/location_access.h"
#include "soh/Enhancements/randomizer/entrance.h"

#include <algorithm>

using namespace Rando;

// Whether Link walks on from where a warp song lands him. The Minuet, the Serenade, the Nocturne and the Prelude land
// him, at either age, in the meadow, at Lake Hylia, on the graveyard's ledge and in the Temple of Time, each a scene with
// exits to the rest of the world.
static bool WalksOnFrom(RandomizerGet song) {
    switch (song) {
        case RG_BOLERO_OF_FIRE:
            // The middle of Death Mountain Crater. A child who warps there cannot get across to an exit. An adult gets
            // across as the pad's own way to the pots asks.
            return logic->IsAdult && (logic->FireTimer() >= 16 || logic->Health() >= 3 * 16) &&
                   (logic->DMCPadToPots() || logic->BeanPlanted(LOGIC_PLANT_DEATH_MOUNTAIN_CRATER_BEAN));
        case RG_REQUIEM_OF_SPIRIT:
            // The Desert Colossus. A child cannot walk out of it: his Haunted Wasteland leads only back to it. Its way
            // into the Wasteland stands for an adult only once he has walked into the Wasteland from the fortress, a
            // walk the route of a seed cannot name.
            return false;
        default:
            return true;
    }
}

// The remake has no save warp to bring Link back to his spawn, so a warp song takes Link only where he can leave: he
// walks on from where it lands him, or he knows a warp song that lands him where he walks on.
static bool CanWarpWith(RandomizerGet song) {
    static constexpr RandomizerGet warpSongs[] = { RG_MINUET_OF_FOREST,  RG_BOLERO_OF_FIRE,    RG_SERENADE_OF_WATER,
                                                   RG_NOCTURNE_OF_SHADOW, RG_REQUIEM_OF_SPIRIT, RG_PRELUDE_OF_LIGHT };
    return logic->CanUse(song) &&
           std::ranges::any_of(warpSongs, [](RandomizerGet way) { return logic->CanUse(way) && WalksOnFrom(way); });
}

void RegionTable_Init_Root() {
    // clang-format off
    areaTable[RR_ROOT] = Region("Root", SCENE_ID_MAX, TIME_DOESNT_PASS, {RA_LINKS_POCKET}, {
        //Events
        EVENT_ACCESS(LOGIC_KAKARIKO_GATE_OPEN,                  (bool)ctx->GetOption(RSK_STARTING_ZELDAS_LETTER)),
        EVENT_ACCESS(LOGIC_TH_COULD_FREE_1_TORCH_CARPENTER,     ctx->GetOption(RSK_GERUDO_FORTRESS).Is(RO_GF_CARPENTERS_FREE)),
        EVENT_ACCESS(LOGIC_TH_COULD_FREE_DOUBLE_CELL_CARPENTER, ctx->GetOption(RSK_GERUDO_FORTRESS).Is(RO_GF_CARPENTERS_FREE) || ctx->GetOption(RSK_GERUDO_FORTRESS).Is(RO_GF_CARPENTERS_FAST)),
        EVENT_ACCESS(LOGIC_TH_COULD_FREE_DEAD_END_CARPENTER,    ctx->GetOption(RSK_GERUDO_FORTRESS).Is(RO_GF_CARPENTERS_FREE) || ctx->GetOption(RSK_GERUDO_FORTRESS).Is(RO_GF_CARPENTERS_FAST)),
        EVENT_ACCESS(LOGIC_TH_COULD_FREE_SLOPE_CARPENTER,       ctx->GetOption(RSK_GERUDO_FORTRESS).Is(RO_GF_CARPENTERS_FREE) || ctx->GetOption(RSK_GERUDO_FORTRESS).Is(RO_GF_CARPENTERS_FAST)),
        EVENT_ACCESS(LOGIC_TH_RESCUED_ALL_CARPENTERS,           ctx->GetOption(RSK_GERUDO_FORTRESS).Is(RO_GF_CARPENTERS_FREE)),
        EVENT_ACCESS(LOGIC_FREED_EPONA,                         (bool)ctx->GetOption(RSK_SKIP_EPONA_RACE)),
        EVENT_ACCESS(LOGIC_SHOWED_MIDO_SWORD_AND_SHIELD,        ctx->GetOption(RSK_FOREST).Is(RO_CLOSED_FOREST_OFF)),
        EVENT_ACCESS(LOGIC_MET_ZELDA,                           ctx->GetOption(RSK_STARTING_ZELDAS_LETTER) && !ctx->GetOption(RSK_SHUFFLE_ZELDAS_LETTER)),
        EVENT_ACCESS(LOGIC_MALON_RETURNED_FROM_CASTLE,          ctx->GetOption(RSK_SHUFFLE_WEIRD_EGG).Is(RO_WEIRD_EGG_SKIP_TALON)),
        EVENT_ACCESS(LOGIC_TALON_RETURNED_FROM_CASTLE,          ctx->GetOption(RSK_SHUFFLE_WEIRD_EGG).Is(RO_WEIRD_EGG_SKIP_TALON)),
    }, {
        //Locations
        LOCATION(RC_LINKS_POCKET,       true),
        LOCATION(RC_GANONS_BOSS_KEY,    logic->CanTriggerGBK()),
        LOCATION(RC_GANON_SOUL,         logic->CanTriggerGanonsSoul()),
        LOCATION(RC_WINCON,             logic->CanTriggerWincon()),
        LOCATION(RC_SARIA_SONG_HINT,    logic->CanUse(RG_SARIAS_SONG)),
        LOCATION(RC_SONG_FROM_IMPA,     ctx->GetOption(RSK_STARTING_ZELDAS_LETTER) && !ctx->GetOption(RSK_SHUFFLE_ZELDAS_LETTER)),
        LOCATION(RC_HC_MALON_EGG,       ctx->GetOption(RSK_SHUFFLE_WEIRD_EGG).Is(RO_WEIRD_EGG_SKIP_TALON)),
        LOCATION(RC_HC_ZELDAS_LETTER,   ctx->GetOption(RSK_STARTING_ZELDAS_LETTER) && !ctx->GetOption(RSK_SHUFFLE_ZELDAS_LETTER)),
        LOCATION(RC_TOT_MASTER_SWORD,   (bool)ctx->GetOption(RSK_SELECTED_STARTING_AGE).Is(RO_AGE_ADULT)),
    }, {
        //Exits
        ENTRANCE(RR_ROOT_EXITS, true),
    });

    areaTable[RR_ROOT_EXITS] = Region("Root Exits", SCENE_ID_MAX, TIME_DOESNT_PASS, {RA_LINKS_POCKET}, {}, {}, {
        //Exits
        ENTRANCE(RR_CHILD_SPAWN,             logic->IsChild),
        ENTRANCE(RR_ADULT_SPAWN,             logic->IsAdult),
        ENTRANCE(RR_MINUET_OF_FOREST_WARP,   CanWarpWith(RG_MINUET_OF_FOREST)),
        ENTRANCE(RR_BOLERO_OF_FIRE_WARP,     CanWarpWith(RG_BOLERO_OF_FIRE)),
        ENTRANCE(RR_SERENADE_OF_WATER_WARP,  CanWarpWith(RG_SERENADE_OF_WATER)),
        ENTRANCE(RR_NOCTURNE_OF_SHADOW_WARP, CanWarpWith(RG_NOCTURNE_OF_SHADOW)),
        ENTRANCE(RR_REQUIEM_OF_SPIRIT_WARP,  CanWarpWith(RG_REQUIEM_OF_SPIRIT)),
        ENTRANCE(RR_PRELUDE_OF_LIGHT_WARP,   CanWarpWith(RG_PRELUDE_OF_LIGHT)),
    });

    areaTable[RR_CHILD_SPAWN] = Region("Child Spawn", SCENE_ID_MAX, TIME_DOESNT_PASS, {RA_LINKS_POCKET}, {}, {}, {
        //Exits
        ENTRANCE(RR_KF_LINKS_HOUSE, true),
    });

    areaTable[RR_ADULT_SPAWN] = Region("Adult Spawn", SCENE_ID_MAX, TIME_DOESNT_PASS, {RA_LINKS_POCKET}, {}, {}, {
        //Exits
        ENTRANCE(RR_TEMPLE_OF_TIME, true),
    });

    areaTable[RR_MINUET_OF_FOREST_WARP] = Region("Minuet of Forest Warp", SCENE_ID_MAX, TIME_DOESNT_PASS, {RA_LINKS_POCKET}, {}, {}, {
        //Exits
        ENTRANCE(RR_SACRED_FOREST_MEADOW, true),
    });

    areaTable[RR_BOLERO_OF_FIRE_WARP] = Region("Bolero of Fire Warp", SCENE_ID_MAX, TIME_DOESNT_PASS, {RA_LINKS_POCKET}, {}, {}, {
        //Exits
        ENTRANCE(RR_DMC_PAD_ENTRY, true),
    });

    areaTable[RR_SERENADE_OF_WATER_WARP] = Region("Serenade of Water Warp", SCENE_ID_MAX, TIME_DOESNT_PASS, {RA_LINKS_POCKET}, {}, {}, {
        //Exits
        ENTRANCE(RR_LAKE_HYLIA, true),
    });

    areaTable[RR_REQUIEM_OF_SPIRIT_WARP] = Region("Requiem of Spirit Warp", SCENE_ID_MAX, TIME_DOESNT_PASS, {RA_LINKS_POCKET}, {}, {}, {
        //Exits
        ENTRANCE(RR_DESERT_COLOSSUS, true),
    });

    areaTable[RR_NOCTURNE_OF_SHADOW_WARP] = Region("Nocturne of Shadow Warp", SCENE_ID_MAX, TIME_DOESNT_PASS, {RA_LINKS_POCKET}, {}, {}, {
        //Exits
        ENTRANCE(RR_GRAVEYARD_WARP_PAD_REGION, true),
    });

    areaTable[RR_PRELUDE_OF_LIGHT_WARP] = Region("Prelude of Light Warp", SCENE_ID_MAX, TIME_DOESNT_PASS, {RA_LINKS_POCKET}, {}, {}, {
        //Exits
        ENTRANCE(RR_TEMPLE_OF_TIME, true),
    });
    // clang-format on
}
