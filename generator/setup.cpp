#include "setup.h"

#include "soh/Enhancements/randomizer/SeedContext.h"
#include "soh/Enhancements/randomizer/logic.h"
#include "soh/Enhancements/randomizer/settings.h"
#include "soh/Enhancements/randomizer/static_data.h"
#include "soh/Enhancements/Lang/Lang.h"
#include "soh/OTRGlobals.h"
#include "soh/Enhancements/randomizer/randomizer.h"

#include <algorithm>
#include <iterator>
#include <utility>
#include <vector>

// The value of each option that describes the unmodified game. SoH carries out most options by changing the game
// while it runs, and the mod changes only which item a chest holds. An option that SoH reads only under another
// option's value is left at SoH's default.
static constexpr std::pair<RandomizerSettingKey, uint8_t> unmodifiedGame[] = {
    { RSK_FOREST, RO_CLOSED_FOREST_ON },
    { RSK_DOOR_OF_TIME, RO_DOOROFTIME_CLOSED },
    { RSK_ZORAS_FOUNTAIN, RO_ZF_CLOSED },
    { RSK_SLEEPING_WATERFALL, RO_WATERFALL_CLOSED },
    { RSK_JABU_OPEN, RO_JABU_CLOSED },
    { RSK_LOCK_OVERWORLD_DOORS, RO_GENERIC_OFF },
    { RSK_GERUDO_FORTRESS, RO_GF_CARPENTERS_NORMAL },
    { RSK_RAINBOW_BRIDGE, RO_BRIDGE_VANILLA },
    { RSK_BRIDGE_OPTIONS, RO_BRIDGE_STANDARD_REWARD },
    { RSK_GANONS_TRIALS, RO_GANONS_TRIALS_SET_NUMBER },
    { RSK_TRIAL_COUNT, 6 },
    { RSK_MEDALLION_LOCKED_TRIALS, RO_GENERIC_OFF },
    { RSK_STARTING_AGE, RO_AGE_CHILD },
    { RSK_SELECTED_STARTING_AGE, RO_AGE_CHILD },
    { RSK_SHUFFLE_ENTRANCES, RO_GENERIC_OFF },
    { RSK_SHUFFLE_DUNGEON_ENTRANCES, RO_DUNGEON_ENTRANCE_SHUFFLE_OFF },
    { RSK_SHUFFLE_BOSS_ENTRANCES, RO_BOSS_ROOM_ENTRANCE_SHUFFLE_OFF },
    { RSK_SHUFFLE_GANONS_TOWER_ENTRANCE, RO_GENERIC_OFF },
    { RSK_SHUFFLE_OVERWORLD_ENTRANCES, RO_GENERIC_OFF },
    { RSK_SHUFFLE_INTERIOR_ENTRANCES, RO_INTERIOR_ENTRANCE_SHUFFLE_OFF },
    { RSK_SHUFFLE_THIEVES_HIDEOUT_ENTRANCES, RO_GENERIC_OFF },
    { RSK_SHUFFLE_GROTTO_ENTRANCES, RO_GENERIC_OFF },
    { RSK_SHUFFLE_OWL_DROPS, RO_GENERIC_OFF },
    { RSK_SHUFFLE_WARP_SONGS, RO_GENERIC_OFF },
    { RSK_SHUFFLE_OVERWORLD_SPAWNS, RO_GENERIC_OFF },
    { RSK_MIXED_ENTRANCE_POOLS, RO_GENERIC_OFF },
    { RSK_MIX_DUNGEON_ENTRANCES, RO_GENERIC_OFF },
    { RSK_MIX_BOSS_ENTRANCES, RO_GENERIC_OFF },
    { RSK_MIX_OVERWORLD_ENTRANCES, RO_GENERIC_OFF },
    { RSK_MIX_INTERIOR_ENTRANCES, RO_GENERIC_OFF },
    { RSK_MIX_THIEVES_HIDEOUT_ENTRANCES, RO_GENERIC_OFF },
    { RSK_MIX_GROTTO_ENTRANCES, RO_GENERIC_OFF },
    { RSK_DECOUPLED_ENTRANCES, RO_GENERIC_OFF },
    { RSK_BOMBCHU_BAG, RO_BOMBCHU_BAG_NONE },
    { RSK_ENABLE_BOMBCHU_DROPS, RO_AMMO_DROPS_OFF },
    { RSK_PROGRESSIVE_GORON_SWORD, RO_GENERIC_OFF },
    { RSK_MQ_DUNGEON_RANDOM, RO_MQ_DUNGEONS_NONE },
    { RSK_MQ_DUNGEON_SET, RO_GENERIC_OFF },
    { RSK_MQ_DEKU_TREE, RO_MQ_SET_VANILLA },
    { RSK_MQ_DODONGOS_CAVERN, RO_MQ_SET_VANILLA },
    { RSK_MQ_JABU_JABU, RO_MQ_SET_VANILLA },
    { RSK_MQ_FOREST_TEMPLE, RO_MQ_SET_VANILLA },
    { RSK_MQ_FIRE_TEMPLE, RO_MQ_SET_VANILLA },
    { RSK_MQ_WATER_TEMPLE, RO_MQ_SET_VANILLA },
    { RSK_MQ_SPIRIT_TEMPLE, RO_MQ_SET_VANILLA },
    { RSK_MQ_SHADOW_TEMPLE, RO_MQ_SET_VANILLA },
    { RSK_MQ_BOTTOM_OF_THE_WELL, RO_MQ_SET_VANILLA },
    { RSK_MQ_ICE_CAVERN, RO_MQ_SET_VANILLA },
    { RSK_MQ_GTG, RO_MQ_SET_VANILLA },
    { RSK_MQ_GANONS_CASTLE, RO_MQ_SET_VANILLA },
    { RSK_SHUFFLE_DUNGEON_REWARDS, RO_DUNGEON_REWARDS_VANILLA },
    { RSK_SHOPSANITY, RO_SHOPSANITY_OFF },
    { RSK_SHOP_SHIELDS_AND_TUNICS_ONLY_REFILL, RO_GENERIC_OFF },
    // Off leaves the three one-time scrubs out of the logic, which asks for less than the game gives.
    { RSK_SHUFFLE_SCRUBS, RO_SCRUBS_OFF },
    { RSK_SCRUBS_PRICES, RO_PRICE_VANILLA },
    { RSK_SHUFFLE_BEEHIVES, RO_GENERIC_OFF },
    { RSK_SHUFFLE_COWS, RO_GENERIC_OFF },
    { RSK_SHUFFLE_KOKIRI_SWORD, RO_GENERIC_ON },
    { RSK_SHUFFLE_MASTER_SWORD, RO_GENERIC_OFF },
    { RSK_SHUFFLE_CHILD_WALLET, RO_GENERIC_OFF },
    { RSK_INCLUDE_TYCOON_WALLET, RO_GENERIC_OFF },
    { RSK_SHUFFLE_OCARINA, RO_GENERIC_OFF },
    { RSK_SHUFFLE_OCARINA_BUTTONS, RO_GENERIC_OFF },
    { RSK_SHUFFLE_SWIM, RO_GENERIC_OFF },
    { RSK_SHUFFLE_CLIMB, RO_GENERIC_OFF },
    { RSK_SHUFFLE_CRAWL, RO_GENERIC_OFF },
    { RSK_SHUFFLE_GRAB, RO_GENERIC_OFF },
    { RSK_SHUFFLE_SPEAK, RO_GENERIC_OFF },
    { RSK_SHUFFLE_OPEN_CHEST, RO_OPEN_CHEST_OFF },
    { RSK_SHUFFLE_WEIRD_EGG, RO_WEIRD_EGG_VANILLA },
    { RSK_SHUFFLE_ZELDAS_LETTER, RO_GENERIC_OFF },
    { RSK_SHUFFLE_SCARECROWS_SONG, RO_GENERIC_OFF },
    { RSK_SHUFFLE_GERUDO_MEMBERSHIP_CARD, RO_GENERIC_OFF },
    { RSK_SHUFFLE_POTS, RO_SHUFFLE_POTS_OFF },
    { RSK_SHUFFLE_GRASS, RO_SHUFFLE_GRASS_OFF },
    { RSK_SHUFFLE_CRATES, RO_SHUFFLE_CRATES_OFF },
    { RSK_SHUFFLE_ROCKS, RO_GENERIC_OFF },
    { RSK_SHUFFLE_BOULDERS, RO_SHUFFLE_BOULDERS_OFF },
    { RSK_SHUFFLE_TREES, RO_GENERIC_OFF },
    { RSK_SHUFFLE_BUSHES, RO_GENERIC_OFF },
    { RSK_SHUFFLE_ICICLES, RO_GENERIC_OFF },
    { RSK_SHUFFLE_RED_ICE, RO_GENERIC_OFF },
    { RSK_SHUFFLE_FIRE_GORONS, RO_GENERIC_OFF },
    { RSK_SHUFFLE_SIGNS, RO_SHUFFLE_SIGNS_OFF },
    { RSK_SHUFFLE_FISHING_POLE, RO_GENERIC_OFF },
    { RSK_SHUFFLE_MERCHANTS, RO_SHUFFLE_MERCHANTS_OFF },
    { RSK_MERCHANT_PRICES, RO_PRICE_VANILLA },
    { RSK_SHUFFLE_BEGGAR, RO_GENERIC_OFF },
    { RSK_SHUFFLE_FISHERMANS_HAT, RO_GENERIC_OFF },
    { RSK_SHUFFLE_FROG_SONG_RUPEES, RO_GENERIC_OFF },
    // On keeps every trade in the logic, each holding what the unmodified game gives.
    { RSK_SHUFFLE_ADULT_TRADE, RO_GENERIC_ON },
    { RSK_SHUFFLE_CHEST_MINIGAME, RO_GENERIC_OFF },
    { RSK_SHUFFLE_100_GS_REWARD, RO_GENERIC_OFF },
    { RSK_SHUFFLE_BEAN_SOULS, RO_GENERIC_OFF },
    { RSK_SHUFFLE_BOSS_SOULS, RO_GENERIC_OFF },
    { RSK_SHUFFLE_DEKU_STICK_BAG, RO_GENERIC_OFF },
    { RSK_SHUFFLE_DEKU_NUT_BAG, RO_GENERIC_OFF },
    { RSK_SHUFFLE_FREESTANDING, RO_SHUFFLE_FREESTANDING_OFF },
    { RSK_SHUFFLE_WONDER_ITEMS, RO_SHUFFLE_WONDER_ITEMS_OFF },
    { RSK_SHUFFLE_SILVER, RO_SHUFFLE_SILVER_OFF },
    { RSK_FISHSANITY, RO_FISHSANITY_OFF },
    { RSK_SHUFFLE_FOUNTAIN_FAIRIES, RO_GENERIC_OFF },
    { RSK_SHUFFLE_STONE_FAIRIES, RO_GENERIC_OFF },
    { RSK_SHUFFLE_BEAN_FAIRIES, RO_GENERIC_OFF },
    { RSK_SHUFFLE_SONG_FAIRIES, RO_GENERIC_OFF },
    { RSK_SHUFFLE_BUTTERFLY_FAIRIES, RO_GENERIC_OFF },
    // The game credits a key, map or compass to the scene Link stands in. The fill keeps each in its chest's scene.
    { RSK_SHUFFLE_MAPANDCOMPASS, RO_DUNGEON_ITEM_LOC_OWN_DUNGEON },
    { RSK_KEYSANITY, RO_DUNGEON_ITEM_LOC_OWN_DUNGEON },
    { RSK_GERUDO_KEYS, RO_GERUDO_KEYS_VANILLA },
    { RSK_BOSS_KEYSANITY, RO_DUNGEON_ITEM_LOC_OWN_DUNGEON },
    { RSK_GANONS_BOSS_KEY, RO_GANON_BOSS_KEY_VANILLA },
    { RSK_GANONS_SOUL, RO_GANONS_SOUL_STARTWITH },
    { RSK_WINCON, RO_WINCON_DEFEAT_GANON },
    { RSK_KEYRINGS, RO_KEYRINGS_OFF },
    { RSK_KEYRINGS_GERUDO_FORTRESS, RO_KEYRING_FOR_DUNGEON_OFF },
    { RSK_KEYRINGS_FOREST_TEMPLE, RO_KEYRING_FOR_DUNGEON_OFF },
    { RSK_KEYRINGS_FIRE_TEMPLE, RO_KEYRING_FOR_DUNGEON_OFF },
    { RSK_KEYRINGS_WATER_TEMPLE, RO_KEYRING_FOR_DUNGEON_OFF },
    { RSK_KEYRINGS_SPIRIT_TEMPLE, RO_KEYRING_FOR_DUNGEON_OFF },
    { RSK_KEYRINGS_SHADOW_TEMPLE, RO_KEYRING_FOR_DUNGEON_OFF },
    { RSK_KEYRINGS_BOTTOM_OF_THE_WELL, RO_KEYRING_FOR_DUNGEON_OFF },
    { RSK_KEYRINGS_GTG, RO_KEYRING_FOR_DUNGEON_OFF },
    { RSK_KEYRINGS_GANONS_CASTLE, RO_KEYRING_FOR_DUNGEON_OFF },
    { RSK_KEYRINGS_CHEST_GAME, RO_KEYRING_FOR_DUNGEON_OFF },
    { RSK_EARLY_GRANNYS_SHOP, RO_GENERIC_OFF },
    { RSK_SKIP_EPONA_RACE, RO_GENERIC_DONT_SKIP },
    { RSK_SKIP_PLANTING_BEANS, RO_GENERIC_OFF },
    { RSK_BIG_POE_COUNT, 10 },
    { RSK_SHUFFLE_MASKS, RO_GENERIC_OFF },
    { RSK_BLUE_FIRE_ARROWS, RO_GENERIC_OFF },
    { RSK_SUNLIGHT_ARROWS, RO_GENERIC_OFF },
    { RSK_BUNNY_HOOD, RO_GENERIC_OFF },
    { RSK_MASKS_AS_ADULT, RO_GENERIC_OFF },
    { RSK_NAYRUS_LOVE_USES, 0 },
    { RSK_INFINITE_UPGRADES, RO_INF_UPGRADES_OFF },
    { RSK_SLINGBOW_BREAK_BEEHIVES, RO_GENERIC_OFF },
    { RSK_STARTING_OCARINA, RO_STARTING_OCARINA_OFF },
    { RSK_STARTING_DEKU_SHIELD, RO_GENERIC_OFF },
    { RSK_STARTING_KOKIRI_SWORD, RO_GENERIC_OFF },
    { RSK_STARTING_MASTER_SWORD, RO_GENERIC_OFF },
    { RSK_STARTING_BEANS, RO_GENERIC_NO },
    { RSK_STARTING_MEGATON_HAMMER, RO_GENERIC_OFF },
    { RSK_STARTING_BOOMERANG, RO_GENERIC_OFF },
    { RSK_STARTING_LENS_OF_TRUTH, RO_GENERIC_OFF },
    { RSK_STARTING_DINS_FIRE, RO_GENERIC_OFF },
    { RSK_STARTING_FARORES_WIND, RO_GENERIC_OFF },
    { RSK_STARTING_NAYRUS_LOVE, RO_GENERIC_OFF },
    { RSK_STARTING_FIRE_ARROWS, RO_GENERIC_OFF },
    { RSK_STARTING_ICE_ARROWS, RO_GENERIC_OFF },
    { RSK_STARTING_LIGHT_ARROWS, RO_GENERIC_OFF },
    { RSK_STARTING_IRON_BOOTS, RO_GENERIC_OFF },
    { RSK_STARTING_HOVER_BOOTS, RO_GENERIC_OFF },
    { RSK_STARTING_HYLIAN_SHIELD, RO_GENERIC_OFF },
    { RSK_STARTING_MIRROR_SHIELD, RO_GENERIC_OFF },
    { RSK_STARTING_GORON_TUNIC, RO_GENERIC_OFF },
    { RSK_STARTING_ZORA_TUNIC, RO_GENERIC_OFF },
    { RSK_STARTING_STONE_OF_AGONY, RO_GENERIC_OFF },
    { RSK_STARTING_HOOKSHOT, RO_GENERIC_OFF },
    { RSK_STARTING_BOW, RO_GENERIC_OFF },
    { RSK_STARTING_SLINGSHOT, RO_GENERIC_OFF },
    { RSK_STARTING_BOMB_BAG, RO_GENERIC_OFF },
    { RSK_STARTING_SCALE, RO_GENERIC_OFF },
    { RSK_STARTING_WALLET, RO_GENERIC_OFF },
    { RSK_STARTING_MAGIC_METER, RO_GENERIC_OFF },
    { RSK_STARTING_BOMBCHU_BAG, RO_GENERIC_OFF },
    { RSK_STARTING_BOTTLE_1, RO_STARTING_BOTTLE_OFF },
    { RSK_STARTING_BOTTLE_2, RO_STARTING_BOTTLE_OFF },
    { RSK_STARTING_BOTTLE_3, RO_STARTING_BOTTLE_OFF },
    { RSK_STARTING_BOTTLE_4, RO_STARTING_BOTTLE_OFF },
    { RSK_STARTING_WEIRD_EGG, RO_GENERIC_OFF },
    { RSK_STARTING_ZELDAS_LETTER, RO_GENERIC_OFF },
    { RSK_STARTING_SCARECROWS_SONG, RO_GENERIC_OFF },
    { RSK_STARTING_CLAIM_CHECK, RO_GENERIC_OFF },
    { RSK_STARTING_GERUDO_CARD, RO_GENERIC_OFF },
    { RSK_STARTING_KEATON_MASK, RO_GENERIC_OFF },
    { RSK_STARTING_SKULL_MASK, RO_GENERIC_OFF },
    { RSK_STARTING_SPOOKY_MASK, RO_GENERIC_OFF },
    { RSK_STARTING_BUNNY_HOOD, RO_GENERIC_OFF },
    { RSK_STARTING_GORON_MASK, RO_GENERIC_OFF },
    { RSK_STARTING_ZORA_MASK, RO_GENERIC_OFF },
    { RSK_STARTING_GERUDO_MASK, RO_GENERIC_OFF },
    { RSK_STARTING_MASK_OF_TRUTH, RO_GENERIC_OFF },
    { RSK_STARTING_BIGGORON_SWORD, RO_STARTING_BGS_OFF },
    { RSK_STARTING_ZELDAS_LULLABY, RO_GENERIC_OFF },
    { RSK_STARTING_EPONAS_SONG, RO_GENERIC_OFF },
    { RSK_STARTING_SARIAS_SONG, RO_GENERIC_OFF },
    { RSK_STARTING_SUNS_SONG, RO_GENERIC_OFF },
    { RSK_STARTING_SONG_OF_TIME, RO_GENERIC_OFF },
    { RSK_STARTING_SONG_OF_STORMS, RO_GENERIC_OFF },
    { RSK_STARTING_MINUET_OF_FOREST, RO_GENERIC_OFF },
    { RSK_STARTING_BOLERO_OF_FIRE, RO_GENERIC_OFF },
    { RSK_STARTING_SERENADE_OF_WATER, RO_GENERIC_OFF },
    { RSK_STARTING_REQUIEM_OF_SPIRIT, RO_GENERIC_OFF },
    { RSK_STARTING_NOCTURNE_OF_SHADOW, RO_GENERIC_OFF },
    { RSK_STARTING_PRELUDE_OF_LIGHT, RO_GENERIC_OFF },
    { RSK_STARTING_SKULLTULA_TOKEN, 0 },
    // Three hearts.
    { RSK_STARTING_HEARTS, 2 },
    { RSK_NO_LOGIC, RO_GENERIC_OFF },
    { RSK_ALL_CHECKS_REACHABLE, RO_GENERIC_OFF },
    { RSK_SKULLS_SUNS_SONG, RO_GENERIC_OFF },
    { RSK_DAMAGE_MULTIPLIER, RO_DAMAGE_MULTIPLIER_DEFAULT },
};

// SoH's starting option and item for an item a seed can start with.
struct StartingOption {
    std::string_view actor;
    RandomizerSettingKey option;
    RandomizerGet item;
};

static constexpr StartingOption startingOptions[] = {
    { "Weapon_Sword_Kokiri", RSK_STARTING_KOKIRI_SWORD, RG_KOKIRI_SWORD },
    { "Weapon_Shield_Deku", RSK_STARTING_DEKU_SHIELD, RG_DEKU_SHIELD },
    { "Weapon_Shield_Hylian", RSK_STARTING_HYLIAN_SHIELD, RG_HYLIAN_SHIELD },
    { "Weapon_Shield_Mirror", RSK_STARTING_MIRROR_SHIELD, RG_MIRROR_SHIELD },
    { "Armor_Adult_HeavyBoot", RSK_STARTING_IRON_BOOTS, RG_IRON_BOOTS },
    { "Armor_Adult_HoverBoot", RSK_STARTING_HOVER_BOOTS, RG_HOVER_BOOTS },
    { "Armor_Adult_Goron", RSK_STARTING_GORON_TUNIC, RG_GORON_TUNIC },
    { "Armor_Adult_Zora", RSK_STARTING_ZORA_TUNIC, RG_ZORA_TUNIC },
    { "Item_Hammer", RSK_STARTING_MEGATON_HAMMER, RG_MEGATON_HAMMER },
    { "Item_Boomerang", RSK_STARTING_BOOMERANG, RG_BOOMERANG },
};

static_assert(std::ranges::equal(startingOptions, StartItems, {}, &StartingOption::actor, &StartItem::actor));

static std::vector<StartingOption> StartedOptions(const SeedSettings& settings) {
    std::vector<StartingOption> started;
    std::ranges::copy_if(startingOptions, std::back_inserter(started), [&](const StartingOption& option) {
        return std::ranges::find(settings.startWith, option.actor, &StartItem::actor) != settings.startWith.end();
    });
    return started;
}

std::vector<RandomizerGet> StartingItems(const SeedSettings& settings) {
    std::vector<RandomizerGet> items;
    std::ranges::transform(StartedOptions(settings), std::back_inserter(items), &StartingOption::item);
    return items;
}

// The game opens a hidden grotto to a bomb or the hammer, or the Song of Storms, and never checks the Echo Gem.
const std::set<RandomizerTrick> enabledTricks = { RT_GROTTOS_WITHOUT_AGONY };

// OTRGlobals::Initialize, then ShipInit::InitAll. InitAll would also register in-game hooks, so only the location
// registrations run here.
void InitializeRandomizer() {
    Lang::LoadLangs();
    OTRGlobals::Instance = new OTRGlobals();
    auto ctx = Rando::Context::CreateInstance();
    OTRGlobals::Instance->gRandoContext = ctx;
    OTRGlobals::Instance->gRandomizer = std::make_shared<Randomizer>();
    ctx->InitStaticData();
    Rando::Settings::GetInstance()->AssignContext(ctx);
    Rando::StaticData::InitItemTable();

    Rando::Settings::GetInstance()->CreateOptions();

    Rando::StaticData::RegisterSongLocations();
    Rando::StaticData::RegisterBeehiveLocations();
    Rando::StaticData::RegisterCowLocations();
    Rando::StaticData::RegisterFishLocations();
    Rando::StaticData::RegisterFairyLocations();
    Rando::StaticData::RegisterPotLocations();
    Rando::StaticData::RegisterFreestandingLocations();
    Rando::StaticData::RegisterSilverLocations();
    Rando::StaticData::RegisterGrassLocations();
    Rando::StaticData::RegisterCrateLocations();
    Rando::StaticData::RegisterRockLocations();
    Rando::StaticData::RegisterTreeLocations();
    Rando::StaticData::RegisterSignLocations();
    Rando::StaticData::RegisterWonderItemLocations();
    Rando::StaticData::RegisterBeggarLocations();
    Rando::StaticData::RegisterFishermansHatLocation();
    Rando::StaticData::RegisterIcicleLocations();
    Rando::StaticData::RegisterRedIceLocations();
    Rando::StaticData::RegisterFireGoronLocations();
    Rando::StaticData::InitHashMaps();
    ctx->AddExcludedOptions();
}

template <typename... Calls>
struct Overloaded : Calls... {
    using Calls::operator()...;
};

void SetOptions(const SeedSettings& settings) {
    Rando::Settings::GetInstance()->SetAllToContext();
    auto ctx = Rando::Context::GetInstance();
    for (auto [key, value] : unmodifiedGame) {
        ctx->GetOption(key).Set(value);
    }
    if (settings.keys == DungeonItems::GivenAtTheStart) {
        ctx->GetOption(RSK_KEYSANITY).Set(RO_DUNGEON_ITEM_LOC_STARTWITH);
        ctx->GetOption(RSK_BOSS_KEYSANITY).Set(RO_DUNGEON_ITEM_LOC_STARTWITH);
        ctx->GetOption(RSK_GANONS_BOSS_KEY).Set(RO_GANON_BOSS_KEY_STARTWITH);
    }
    if (settings.forest == Forest::ClosedDeku) {
        ctx->GetOption(RSK_FOREST).Set(RO_CLOSED_FOREST_DEKU_ONLY);
    } else if (settings.forest == Forest::Open) {
        ctx->GetOption(RSK_FOREST).Set(RO_CLOSED_FOREST_OFF);
    }
    if (settings.bridge == Bridge::AlwaysOpen) {
        ctx->GetOption(RSK_RAINBOW_BRIDGE).Set(RO_BRIDGE_ALWAYS_OPEN);
    }
    if (settings.trials == Trials::None) {
        ctx->GetOption(RSK_GANONS_TRIALS).Set(RO_GANONS_TRIALS_SKIP);
    }
    if (settings.shops) {
        auto& prices = ctx->GetOption(RSK_SHOPSANITY_PRICES);
        // SoH's options hold one price and the ends of a range in steps.
        std::visit(Overloaded{ [&](VanillaPrices) { prices.Set(RO_PRICE_VANILLA); },
                               [&](CheapPrices) { prices.Set(RO_PRICE_CHEAP_BALANCED); },
                               [&](BalancedPrices) { prices.Set(RO_PRICE_BALANCED); },
                               [&](OnePrice one) {
                                   prices.Set(RO_PRICE_FIXED);
                                   ctx->GetOption(RSK_SHOPSANITY_PRICES_FIXED_PRICE).Set(one.price / ShopPriceStep);
                               },
                               [&](PricesBetween between) {
                                   prices.Set(RO_PRICE_RANGE);
                                   ctx->GetOption(RSK_SHOPSANITY_PRICES_RANGE_1).Set(between.lowest / ShopPriceStep);
                                   ctx->GetOption(RSK_SHOPSANITY_PRICES_RANGE_2).Set(between.highest / ShopPriceStep);
                               } },
                   settings.shops->prices);
    }
    for (const StartingOption& started : StartedOptions(settings)) {
        ctx->GetOption(started.option).Set(RO_GENERIC_ON);
    }
    ctx->GetLogic()->LensNeeded = settings.lens == Lens::Needed;
    ctx->GetLogic()->SongsShuffled = settings.songs == Songs::ShuffledAmongThemselves;
}
