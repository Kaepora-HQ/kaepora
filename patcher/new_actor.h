// Actors the mod adds to the game. Each has its own pack and a row in each of the game's actor tables.
#pragma once

#include "byml.h"
#include "game_file.h"
#include "sarc.h"

#include <array>
#include <map>
#include <string>
#include <string_view>

// Every actor that a scene places has a row in each of these, sorted by __RowId.
constexpr std::string_view ActorInfoPath = "RSDB/ActorInfo.Product.111.rstbl.byml.zs";
constexpr std::string_view GameActorInfoPath = "RSDB/GameActorInfo.Product.111.rstbl.byml.zs";
constexpr std::string_view PouchActorInfoPath = "RSDB/PouchActorInfo.Product.111.rstbl.byml.zs";
constexpr std::array<std::string_view, 3> ActorTablePaths = { ActorInfoPath, GameActorInfoPath, PouchActorInfoPath };

const std::string& RowId(const byml::Node& row);

// How the game's tables and packs name an actor.
std::string WorkPath(std::string_view actor);

// The actor a work path names. Throws when the path names none.
std::string WorkPathActor(std::string_view path);

// The actor whose records a find of the item writes: the one its actor's ActorNameRef names. A chest's or a drop's
// form of an item names another actor than its own, as Item_Range_Bow names Weapon_Range_Bow.
std::string RecordActor(GameFile& game, const std::string& packDictionary, std::string_view item);

// The dict's value at the key. Throws when it has none.
byml::Node& Value(byml::Node& dict, std::string_view key);

// The table's row whose __RowId is the actor. Throws when the table has none.
byml::Node& Row(byml::Node& table, std::string_view actor);

// The actor tables by path.
using ActorTables = std::map<std::string, byml::Document, std::less<>>;

struct NewActor {
    std::string name;
    sarc::Files pack;
    std::map<std::string, byml::Node, std::less<>> rows; // by the table's path
};

// A goods actor that sells the item: the item's model, animation and name on a goods actor's shelf, and what the
// item's own actor gives when bought. Its base price is 0, so a slot's PriceOffset is its whole price.
NewActor GoodsActor(GameFile& game, const std::string& packDictionary, ActorTables& tables, const std::string& item);

// A copy of the Gold Skulltula token that gives the item: the token's model, pick-up and record of the skulltula's
// kill, with the item's name and what the item's own actor gives.
NewActor TokenCopy(GameFile& game, const std::string& packDictionary, ActorTables& tables, const std::string& item);

// A copy of the game's Hylian Shield pickup that gives the item: taken by touch, with the pickup's record of being
// taken and its body sunk by the model's depth under its origin, the item's name, model, animation and parameters, and
// what the item's own actor gives.
NewActor PickupCopy(GameFile& game, const std::string& packDictionary, ActorTables& tables, const std::string& item);

// How far an actor's model reaches under its origin, in metres, from its ActorInfo row's bounds, and 0 for a model
// whose bounds start above it. The bounds are trusted only that way: seen in the game, they put a copy of the token,
// the map and the Megaton Hammer on the floor, and one of the Lens of Truth, whose bounds start 0.92 m above its
// origin, out of sight under it.
float DepthUnderOrigin(byml::Node& actorInfoRow);
