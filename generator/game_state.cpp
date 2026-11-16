// Game and engine state that the generator sources read or link against.
#include "soh/OTRGlobals.h"
#include "soh/Enhancements/randomizer/randomizer.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/item-tables/ItemTableManager.h"

extern "C" {
#include "z64.h"
#include "variables.h"
#include "message_data_static.h"

// Zero-filled, so the language is LANGUAGE_ENG, as on a fresh File Select screen.
SaveContext gSaveContext;
// No scene is loaded while a seed generates.
PlayState* gPlayState = nullptr;

// Game data that only the in-game halves of the compiled files reference.
f32 sFontWidths[144];
const char* fontTbl[140];
int gMapLoading;
MessageTableEntry* sNesMessageEntryTablePtr;
MessageTableEntry* sGerMessageEntryTablePtr;
MessageTableEntry* sFraMessageEntryTablePtr;
Vec3f sTagPointsFree[9];
Vec3f sTagPointsOrdered[9];
}

// SoH sets these from the archives it finds at start-up. The reference spoiler came from a game with the
// original archive and no Master Quest archive.
OTRGlobals* OTRGlobals::Instance;
OTRGlobals::OTRGlobals() : context(nullptr), hasMasterQuest(false), hasOriginal(true) {
}
OTRGlobals::~OTRGlobals() = default;
bool OTRGlobals::HasMasterQuest() {
    return hasMasterQuest;
}
bool OTRGlobals::HasOriginal() {
    return hasOriginal;
}

GameInteractor* GameInteractor::Instance = nullptr;
ItemTableManager* ItemTableManager::Instance = nullptr;

// The generator reads settings through the in-game Randomizer object; the rest of it is game-only.
Randomizer::Randomizer() {
}
Randomizer::~Randomizer() {
}
u8 Randomizer::GetRandoSettingValue(RandomizerSettingKey randoSettingKey) {
    return Rando::Context::GetInstance()->GetOption(randoSettingKey).Get();
}
