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
    int number; // the location's item number in its shop, 1 to 8, as SoH numbers them; a shop's slots come in turn
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

// A SoH location whose item a character hands over, and each string parameter of the remake's event flows that gives
// it. Every site gives the location's one item.
struct RemakeGift {
    struct Site {
        std::string flow; // romfs path of the event flow
        std::string event;
        std::string param;
    };

    RandomizerCheck location;
    std::vector<Site> sites;
    // What the sites give in the unmodified game, by its remake name, and the SoH item it stands for.
    std::string item;
    RandomizerGet sohItem;
};

// The gifts of remake/gifts.tsv, read from the copy built into the program, one per location, in table order. Throws
// when a row names an unknown location, a location of the chest table, a flow that is not an event flow under
// Event/EventFlow/, or an item that is not one SoH item of remake/items.tsv, or when the rows of a location name
// different items.
std::vector<RemakeGift> LoadRemakeGifts(const std::vector<RemakeChest>& chests);

// Throws when an item a shop slot can hold has no row in remake/shop_goods.tsv, read from the copy built into the
// program, or a row names any other item. A slot can hold the item of a shuffled chest or of a gift that KeptFromShops
// lets into shops, the token a shuffled skulltula adds, and the shop filler.
void CheckShopGoods(const std::vector<RemakeChest>& chests, const std::vector<RemakeGift>& gifts);

struct RemakeSkulltula {
    RandomizerCheck location;
    RemakeChest::Actor actor;
};

// The skulltulas of remake/skulltulas.tsv, read from the copy built into the program. A skulltula that stands outside
// its location's SoH area moves the location into the area of its place, so hints and the barren areas name where it
// is. Throws, naming the row, on a table that is not in the form remake/README.md gives.
std::vector<RemakeSkulltula> LoadRemakeSkulltulas();

inline constexpr std::string_view TokenName = "Item_Token_Gold";

struct RemakeStone {
    std::string label;
    RandomizerCheck location;
};

// A sentence in SoH's markup, split where it names an item or a place.
struct Sentence {
    std::string before;
    std::string after;
};

// How much text a message's box holds: the widest line in bytes, one for each character of plain English, and the lines
// in a box.
struct Fit {
    size_t lineLength;
    size_t boxLines;
};

// A line in which a character names what a place gives. Every seed rewrites it to name what the seed places there.
struct PrizeLine {
    RandomizerCheck location;
    std::string ownItem; // what the place gives in the unmodified game, by its remake name
    std::string file;    // the message file in the text packs
    std::string label;
    Sentence sentence;
    Fit fit;
};

// A message in which a character tells where an item lies, as SoH's hint of that name does.
struct CharacterHint {
    RandomizerCheck hint; // the spoiler log names the hint by SoH's location for it
    RandomizerGet item;
    std::string file; // the message file in the text packs
    std::string label;
    Sentence sentence; // [[1]] is the place
    Fit fit;
};

// Places and items as a hint names them, with their article, the lines that name a prize, and the characters' hints.
struct RemakeHintTables {
    std::vector<RemakeStone> stones;
    std::map<RandomizerCheckArea, std::string> places;
    std::map<std::string, std::string> itemsByRemakeName;
    std::map<RandomizerCheck, Sentence> chestSentences;
    std::vector<std::string> junk;
    std::vector<PrizeLine> prizeLines;
    std::vector<CharacterHint> characterHints;
};

// The hint tables, read from the copies built into the program. Throws when a table names an unknown location, area or
// item, repeats one, or leaves out a shuffled chest of the chest table, its area or its item, a shop slot that can be
// shuffled or its area, a gift, its area or its item, or a skulltula or its area, or when a prize line names a place
// that is no chest of the chest table, or one whose own item has no name, or a character's hint names an unknown hint
// location or item.
RemakeHintTables LoadRemakeHintTables(const std::vector<RemakeChest>& chests, const std::vector<RemakeShopSlot>& shops,
                                      const std::vector<RemakeGift>& gifts,
                                      const std::vector<RemakeSkulltula>& skulltulas);

// The remake name of the item each shuffled chest, shop slot, gift and skulltula gives. Where several remake names
// stand for one SoH item, the chests in table order, then the slots, the gifts and the skulltulas give the names the
// shuffled chests held, the slots' fillers, the gifts held and the skulltulas' tokens, each as often as it occurred.
std::map<RandomizerCheck, std::string> PlacedRemakeItems(const std::vector<RemakeChest>& chests,
                                                         const std::vector<RemakeShopSlot>& shops,
                                                         const std::vector<RemakeGift>& gifts,
                                                         const std::vector<RemakeSkulltula>& skulltulas);

struct StoneHint {
    RemakeStone stone;
    std::string tagged;
    std::string plain;
};

// A message the seed rewrites, and its text in the patcher's tags.
struct MessageText {
    std::string file;
    std::string label;
    std::string tagged;
};

// The plan the patcher reads: each actor of each shuffled chest, in table order, with the remake item placed there.
// Then the event flow strings every seed changes, each site of each shuffled gift with its item and the text that keeps
// the Stone of Agony's guide shut after the 20-token reward, the saved values a new game starts with that the rules or
// the settings count on, the gossip stones' texts, the messages the seed rewrites, each shuffled shop slot with its
// item and price, and each shuffled skulltula with its item.
nlohmann::ordered_json RemakePlan(const std::string& seed, const std::vector<RemakeChest>& chests,
                                  const std::vector<RemakeShopSlot>& shops, const std::vector<RemakeGift>& gifts,
                                  const std::vector<RemakeSkulltula>& skulltulas, const SeedSettings& settings,
                                  const std::map<RandomizerCheck, std::string>& items,
                                  const std::vector<StoneHint>& hints, const std::vector<MessageText>& messages);
