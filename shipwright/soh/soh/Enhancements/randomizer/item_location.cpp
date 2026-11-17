#include "item_location.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "SeedContext.h"

namespace Rando {
ItemLocation::ItemLocation() : rc(RC_UNKNOWN_CHECK) {
}
ItemLocation::ItemLocation(const RandomizerCheck rc_) : rc(rc_) {
}

RandomizerCheck ItemLocation::GetRandomizerCheck() const {
    return rc;
}

bool ItemLocation::IsAddedToPool() const {
    return addedToPool;
}

void ItemLocation::AddToPool() {
    addedToPool = true;
}

void ItemLocation::RemoveFromPool() {
    addedToPool = false;
}

const Item& ItemLocation::GetPlacedItem() const {
    return StaticData::RetrieveItem(placedItem);
}

RandomizerGet& ItemLocation::RefPlacedItem() {
    return placedItem;
}

const Text& ItemLocation::GetPlacedItemName() const {
    return StaticData::RetrieveItem(placedItem).GetName();
}

RandomizerGet ItemLocation::GetPlacedRandomizerGet() const {
    return placedItem;
}

void ItemLocation::SetPlacedItem(const RandomizerGet item) {
    placedItem = item;
}

void ItemLocation::DelayItem() {
    delayedItem = placedItem;
    placedItem = RG_NONE;
}

void ItemLocation::SaveDelayedItem() {
    placedItem = delayedItem;
    delayedItem = RG_NONE;
}

void ItemLocation::SetParentRegion(const RandomizerRegion region) {
    parentRegion = region;
}

// RANDOTODO only used in tracker now, could possibly be removed
RandomizerRegion ItemLocation::GetParentRegionKey() const {
    return parentRegion;
}

void ItemLocation::PlaceVanillaItem() {
    placedItem = StaticData::GetLocation(rc)->GetVanillaItem();
    SetPrice(StaticData::GetLocation(rc)->GetVanillaPrice());
}

void ItemLocation::ApplyPlacedItemEffect() const {
    StaticData::RetrieveItem(placedItem).ApplyEffect();
}

uint16_t ItemLocation::GetPrice() const {
    return price;
}

void ItemLocation::SetPrice(const uint16_t price_) {
    if (hasCustomPrice) {
        return;
    }
    price = price_;
}

bool ItemLocation::CanBePurchased() const {
    const RandomizerCheckType checkType = StaticData::GetLocation(rc)->GetRCType();
    return checkType == RCTYPE_SHOP || checkType == RCTYPE_SCRUB || checkType == RCTYPE_MERCHANT;
}

void ItemLocation::SetCustomPrice(const uint16_t price_) {
    price = price_;
    hasCustomPrice = true;
}

bool ItemLocation::HasObtained() const {
    return status == RCSHOW_COLLECTED || status == RCSHOW_SAVED;
}

void ItemLocation::SetCheckStatus(RandomizerCheckStatus status_) {
    status = status_;
    GameInteractor::Instance->ExecuteHooks<GameInteractor::OnRandoSetCheckStatus>(rc, status);
}

RandomizerCheckStatus ItemLocation::GetCheckStatus() {
    return status;
}

void ItemLocation::SetIsSkipped(bool isSkipped_) {
    isSkipped = isSkipped_;
    GameInteractor::Instance->ExecuteHooks<GameInteractor::OnRandoSetIsSkipped>(rc, isSkipped);
}

bool ItemLocation::GetIsSkipped() {
    return isSkipped;
}

bool ItemLocation::IsExcluded() {
    return excludedOption.Is(RO_LOCATION_EXCLUDE);
}

void ItemLocation::SetExcludedOption(uint8_t val) {
    excludedOption.Set(val);
}

bool ItemLocation::IsVisible() const {
    return visibleInImGui;
}
void ItemLocation::SetVisible(const bool visibleInImGui_) {
    visibleInImGui = visibleInImGui_;
}

void ItemLocation::ResetVariables() {
    addedToPool = false;
    placedItem = RG_NONE;
    delayedItem = RG_NONE;
    price = 0;
    hasCustomPrice = false;
    status = RCSHOW_UNCHECKED;
    isSkipped = false;
    isAvailable = false;
}

bool ItemLocation::IsAvailable() const {
    return isAvailable;
}

void ItemLocation::SetAvailable(bool isAvailable_) {
    isAvailable = isAvailable_;
}
} // namespace Rando