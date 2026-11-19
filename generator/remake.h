#pragma once

#include "seed_settings.h"
#include "soh/Enhancements/randomizer/randomizerEnums.h"
#include <nlohmann/json.hpp>

#include <map>
#include <string>
#include <string_view>
#include <vector>

// A SoH location and the remake chest it is. Where the remake has no chest, actors is empty.
struct RemakeChest {
    // One actor that places the chest: its hash, and the romfs paths of the scene files that hold it, all in one Banc
    // folder.
    struct Actor {
        std::string hash;
        std::vector<std::string> files;

        std::string Folder() const;
    };

    RandomizerCheck location;
    // Usually one. The files keep a few chests twice, under two hashes, and both give the location's item: once in
    // each of two versions of a scene, once in each of two scenes that show the same spot, or as the two chests one
    // puzzle shows one of.
    std::vector<Actor> actors;
    // What the first actor gives, by its remake name (remake/README.md), empty when the table names nothing, and the
    // SoH item it stands for. RG_NONE means the logic sees nothing there.
    std::string item;
    RandomizerGet sohItem;
    bool shuffled;

    // The first actor's Banc folder, the remake's name for the scene the chest stands in.
    std::string Folder() const;
};

// A SoH shop location and the remake shop slot it is: the slot's index in the goods list of each shopkeeper that sells
// it, and the shelf actor that shows it in each scene.
struct RemakeShopSlot {
    struct Shopkeeper {
        std::string actor;
        std::string goodsList; // the goods list in the shopkeeper's pack
    };
    struct Shelf {
        std::string file; // romfs path of the scene file
        std::string hash;
    };

    RandomizerCheck location;
    int number; // the location's item number in its shop, 1 to 8, as SoH numbers them
    int slot;
    std::vector<Shopkeeper> shopkeepers;
    std::vector<Shelf> shelves;
};

// The chests of remake/chests.tsv that stand for a location, read from the copy built into the program, one per
// location. Throws when a table names an unknown location or item, repeats a hash, repeats a location for anything but
// another copy of its chest, gives an unshuffled row no note, or leaves out a chest location of SoH's vanilla layouts.
std::vector<RemakeChest> LoadRemakeChests();

// What each shuffled shop slot adds to the pool, and the remake's name for it.
inline constexpr RandomizerGet ShopFiller = RG_RED_RUPEE;
inline constexpr std::string_view ShopFillerName = "TBox_Rupee_Red";

// The slots of remake/shops.tsv, read from the copy built into the program. Throws when a row names anything but a SoH
// shop location, repeats one, or gives a number outside 1 to 8 or a slot outside 0 to 7, or when the table leaves one
// out.
std::vector<RemakeShopSlot> LoadRemakeShops();

// Throws when an item a shop slot can hold has no row in remake/shop_goods.tsv, read from the copy built into the
// program, or a row names any other item. A slot can hold the item of a shuffled chest that KeptFromShops lets into
// shops, and the shop filler.
void CheckShopGoods(const std::vector<RemakeChest>& chests);

struct RemakeStone {
    std::string label;
    RandomizerCheck location;
};

struct ChestSentence {
    std::string beforeItem;
    std::string afterItem;
};

// Places and items as a hint names them, with their article.
struct RemakeHintTables {
    std::vector<RemakeStone> stones;
    std::map<RandomizerCheckArea, std::string> places;
    std::map<std::string, std::string> itemsByRemakeName;
    std::map<RandomizerCheck, ChestSentence> chestSentences;
    std::vector<std::string> junk;
};

// The hint tables, read from the copies built into the program. Throws when a table names an unknown location, area or
// item, repeats one, or leaves out a shuffled chest of the chest table, its area or its item, or a shop slot that can
// be shuffled, or its area.
RemakeHintTables LoadRemakeHintTables(const std::vector<RemakeChest>& chests, const std::vector<RemakeShopSlot>& shops);

// The remake name of the item each shuffled chest and shop slot gives. Where several remake names stand for one SoH
// item, the chests in table order and then the slots give the names the shuffled chests held and the slots' fillers,
// each as often as it occurred.
std::map<RandomizerCheck, std::string> PlacedRemakeItems(const std::vector<RemakeChest>& chests,
                                                         const std::vector<RemakeShopSlot>& shops);

struct StoneHint {
    RemakeStone stone;
    std::string tagged;
    std::string plain;
};

// The plan the patcher reads: each actor of each shuffled chest, in table order, with the remake item placed there.
// Then the event flow strings every seed changes, the saved values a new game starts with that the rules or the
// settings count on, the gossip stones' texts, and each shuffled shop slot with its item and price.
nlohmann::ordered_json RemakePlan(const std::string& seed, const std::vector<RemakeChest>& chests,
                                  const std::vector<RemakeShopSlot>& shops, const SeedSettings& settings,
                                  const std::map<RandomizerCheck, std::string>& items,
                                  const std::vector<StoneHint>& hints);
