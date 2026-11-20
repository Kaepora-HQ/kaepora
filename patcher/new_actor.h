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

// The table's row whose __RowId is the actor. Throws when the table has none.
byml::Node& Row(byml::Node& table, std::string_view actor);

// The actor tables by path, each read once for every new actor of a plan.
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

// A copy of the game's Hylian Shield pickup that gives the item: taken by touch, with the pickup's body and record of
// being taken, the item's name, model, animation and parameters, and what the item's own actor gives.
NewActor PickupCopy(GameFile& game, const std::string& packDictionary, ActorTables& tables, const std::string& item);
