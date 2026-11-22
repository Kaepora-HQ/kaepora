#pragma once

#include "seed_settings.h"
#include "soh/Enhancements/randomizer/randomizerEnums.h"
#include <nlohmann/json.hpp>

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// A SoH location and the remake chest it is, or the item that lies loose there. Where the remake has no chest for a
// chest location, actors is empty.
struct RemakeChest {
    // One actor that places the chest or the loose item: its hash, and the romfs paths of the scene files that hold it,
    // all in one Banc folder.
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

// A SoH song location and the remake scene that teaches its song. The lesson's flow passes the song's melody type to
// the shared lesson in Dm_SY_Ocarina from each example event, for the tune the teacher plays after the line the event
// names, and from each follow event, for the notes Link plays. The gift event names the song.
struct RemakeSongPlace {
    RandomizerCheck location;
    std::string song; // the remake name of the song the place teaches in the game
    RandomizerGet sohSong;
    int melody;
    std::string lesson; // romfs path of the lesson's event flow
    std::vector<std::string> examples;
    std::vector<std::string> follows;
    std::string line; // the message the example events show, by its file and label
    std::string giftFlow;
    std::string giftEvent;
};

// The chests of remake/chests.tsv that stand for a location, read from the copy built into the program, one per
// location. Throws when a table names an unknown location or item, repeats a hash, repeats a location for anything but
// another copy of its chest, gives an unshuffled row no note, or leaves out a chest location of SoH's vanilla layouts.
std::vector<RemakeChest> LoadRemakeChests();

// The items of remake/loose_items.tsv, read from the copy built into the program, each one actor that gives its
// location's item, all of them shuffled. Throws, naming the row, on a table that is not in the form remake/README.md
// gives.
std::vector<RemakeChest> LoadRemakeLooseItems();

// The pool's filler, and its remake name.
inline constexpr RandomizerGet Filler = RG_RED_RUPEE;
inline constexpr std::string_view FillerName = "TBox_Rupee_Red";

// The slots of remake/shops.tsv, read from the copy built into the program. Throws when a row names anything but a SoH
// shop location, repeats one, or gives a number outside 1 to 8 or a slot outside 0 to 7, or when the table leaves one
// out.
std::vector<RemakeShopSlot> LoadRemakeShops();

// One of the game's goods that SoH's shopsanity keeps on the shop slots a count leaves (remake/kept_goods.tsv): the
// goods actor that sells it in the remake, and its price there.
struct KeptGoods {
    std::string goods;
    int price;
};
// A row for every good of SoH's list, by its SoH item.
std::map<RandomizerGet, KeptGoods> LoadKeptGoods();
// The spoiler log's name for one of SoH's goods: SoH's own, without the price in brackets that tells apart goods the
// remake sells as one at one price, as "Buy Bombs (5) [25]" and "Buy Bombs (5) [35]".
std::string KeptGoodsName(RandomizerGet item);

// A seed's shop slots, as SoH's shopsanity splits them: the slots its count shuffles, and the slots it leaves, which
// sell SoH's list of the game's goods. "Shop goods" at "Vanilla" leaves every slot as the game has it, in neither list.
struct RemakeShops {
    std::vector<RemakeShopSlot> shuffled;
    std::vector<RemakeShopSlot> kept;
    std::map<RandomizerGet, KeptGoods> goods; // what a kept slot can sell
};

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
    // A Great Fairy asks on a return visit whether Link has got her own item, and sells it again if not. So a saved
    // Bool of the mod's own, named by her claim, records her paid visit: her paid path's write, which sets a value
    // nothing reads, sets it, and her question asks it.
    struct Claim {
        std::string name;
        Site paid;
        Site question;
    };
    std::optional<Claim> claim;
};

struct GroupedGift {
    GiftGroup group;
    RemakeGift gift;
};

// The gifts of remake/gifts.tsv, read from the copy built into the program, one per location, in table order, each
// with its claim of remake/fountain_claims.tsv if it has one. Throws when a row names an unknown location or group, a
// location of the chest table, a flow that is not an event flow under Event/EventFlow/, or an item that is not one SoH
// item of remake/items.tsv, when the rows of a location name different items or groups, when a group of "Gifts" has
// no row, or when a claim's location is no gift's or has a claim already, its flow is not its gift's, or its name is
// not Kaepora_Once_ and a number.
std::vector<GroupedGift> LoadRemakeGifts(const std::vector<RemakeChest>& chests);

// The places of remake/rewards.tsv that hand over a Spiritual Stone or a medallion, read and checked as the gifts are.
// The fill treats them as gifts.
std::vector<RemakeGift> LoadRemakeRewards(const std::vector<RemakeChest>& chests);

// Throws when an item a shop slot can hold has no row in remake/shop_goods.tsv, read from the copy built into the
// program, or a row names any other item. A slot can hold the item of a shuffled chest that KeptFromShops and
// KeptInChests let into shops, of a gift that those and KeptInChestsAndCalls let in, the token a shuffled skulltula
// adds, and the filler.
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

// The places of remake/songs.tsv, read from the copy built into the program. Throws when a row names anything but a SoH
// song location, repeats one or its melody, names a song that is not the location's, or names a flow or a message
// outside the event flows' folders, or when the table leaves a song location out.
std::vector<RemakeSongPlace> LoadRemakeSongs();

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

// A message a seed adds under a label the game's files lack, as a copy of another, and the event parameter that shows
// it in place of the message that place showed.
struct MessageCopy {
    std::string of; // the label of the copied message
    std::string flow;
    std::string event;
    std::string param;
};

// An event of an event flow that goes on to another event of its flow: an action, a join or a sub-flow call by its next
// event, a switch by each of its cases.
struct EventNext {
    std::string flow;
    std::string event;
    std::string next;
};

// A line in which a character names what a place gives. A seed that shuffles the place rewrites it to name what the
// seed places there.
struct PrizeLine {
    RandomizerCheck location;
    std::string file; // the message file in the text packs
    std::string label;
    std::optional<MessageCopy> copy; // for a line under a label the file lacks
    Sentence sentence;
    Fit fit;
};

// A message in which a character tells where an item lies, as SoH's hint of that name does.
struct CharacterHint {
    RandomizerCheck hint; // the spoiler log names the hint by SoH's location for it
    RandomizerGet item;
    std::string file; // the message file in the text packs
    std::string label;
    std::optional<EventNext> sentOn; // for a line the game says once, the event that brings it on every talk
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
// shuffled or its area, a gift, its area or its item, a skulltula or its area, or a song place or its area, or when a
// prize line names a place that is no chest of the chest table and no gift, or a character's hint names an unknown hint
// location or item.
RemakeHintTables LoadRemakeHintTables(const std::vector<RemakeChest>& chests, const std::vector<RemakeShopSlot>& shops,
                                      const std::vector<RemakeGift>& gifts,
                                      const std::vector<RemakeSkulltula>& skulltulas,
                                      const std::vector<RemakeSongPlace>& songs);

// The remake name of the item each shuffled chest, shop slot, gift, skulltula and song place gives. Where several
// remake names stand for one SoH item, the chests in table order, then the slots, the gifts and the skulltulas give the
// names the shuffled chests held, the slots' fillers, the gifts held and the skulltulas' tokens, each as often as it
// occurred.
std::map<RandomizerCheck, std::string> PlacedRemakeItems(const std::vector<RemakeChest>& chests,
                                                         const std::vector<RemakeShopSlot>& shops,
                                                         const std::vector<RemakeGift>& gifts,
                                                         const std::vector<RemakeSkulltula>& skulltulas,
                                                         const std::vector<RemakeSongPlace>& songs);

struct StoneHint {
    RemakeStone stone;
    std::string tagged;
    std::string plain;
};

// A message the seed rewrites, and its text in the patcher's tags.
struct MessageText {
    std::string file;
    std::string label;
    std::optional<MessageCopy> copy;
    std::optional<EventNext> sentOn;
    std::string tagged;
};

// The plan the patcher reads: each actor of each shuffled chest, in table order, with the remake item placed there,
// with the songs shuffled the Desert Colossus's ways out that wait for the Spirit quest, and with Malon's place
// shuffled the vine climb's hatch that waits for the Weird Egg's record. Then the event flow
// strings every seed changes, and each site of each shuffled gift with its item and the text that keeps the Stone of
// Agony's guide shut after the 20-token reward. With the songs shuffled, the windmill's and the Bolero's lessons then
// name their own lesson where they named their song, and each song place's lesson takes the melody type, the line and
// the gift of the song it teaches. Then the saved values a new game starts with that the rules or the settings count
// on, the gossip stones' texts, the messages the seed rewrites or adds, with the event that shows each one added and the
// event that brings each one the game says once on every talk, each shuffled shop slot with its item and price, each
// slot a count leaves with the game's goods it sells, each shuffled skulltula with its item, each shuffled loose item
// that gives another item than its own, with that item, and for each step the seed starts with the actors of the finds
// that bring a new game to it.
nlohmann::ordered_json RemakePlan(const std::string& seed, const std::vector<RemakeChest>& chests,
                                  const std::vector<RemakeChest>& looseItems, const RemakeShops& shops,
                                  const std::vector<RemakeGift>& gifts,
                                  const std::vector<RemakeSkulltula>& skulltulas,
                                  const std::vector<RemakeSongPlace>& songs, const SeedSettings& settings,
                                  const std::map<RandomizerCheck, std::string>& items,
                                  const std::vector<StoneHint>& hints, const std::vector<MessageText>& messages);
