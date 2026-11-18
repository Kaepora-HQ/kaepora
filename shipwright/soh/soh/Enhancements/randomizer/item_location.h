#pragma once

#include "soh/Enhancements/custom-message/text.h"
#include "static_data.h"
#include "option.h"

namespace Rando {
class ItemLocation {
  public:
    ItemLocation();
    explicit ItemLocation(RandomizerCheck rc_);
    RandomizerCheck GetRandomizerCheck() const;
    bool IsAddedToPool() const;
    void AddToPool();
    void RemoveFromPool();
    const Item& GetPlacedItem() const;
    const Text& GetPlacedItemName() const;
    RandomizerGet GetPlacedRandomizerGet() const;
    void SetPlacedItem(RandomizerGet item);
    RandomizerGet& RefPlacedItem();
    void DelayItem();
    RandomizerRegion GetParentRegionKey() const;
    void SetParentRegion(RandomizerRegion region);
    void PlaceVanillaItem();
    void ApplyPlacedItemEffect() const;
    void SaveDelayedItem();
    uint16_t GetPrice() const;
    void SetPrice(uint16_t price_);
    void SetCustomPrice(uint16_t price_);
    bool CanBePurchased() const;
    bool HasObtained() const;
    void SetCheckStatus(RandomizerCheckStatus status_);
    RandomizerCheckStatus GetCheckStatus();
    void SetIsSkipped(bool isSkipped_);
    bool GetIsSkipped();
    bool IsHintable() const;
    void SetAsHintable();
    bool IsExcluded();
    void SetExcludedOption(uint8_t val);
    bool IsVisible() const;
    void SetVisible(bool visibleInImGui_);
    bool IsWothCandidate() const;
    void SetWothCandidate();
    bool IsFoolishCandidate() const;
    void SetBarrenCandidate();
    void ResetVariables();
    bool IsAvailable() const;
    void SetAvailable(bool isAvailable_);

  private:
    RandomizerCheck rc;
    bool addedToPool = false;
    RandomizerGet placedItem = RG_NONE;
    RandomizerGet delayedItem = RG_NONE;
    bool isHintable = false;
    OptionValue excludedOption = OptionValue(RO_LOCATION_INCLUDE);
    uint16_t price = 0;
    RandomizerRegion parentRegion = RR_NONE;
    bool hasCustomPrice = false;
    bool visibleInImGui = false;
    RandomizerCheckStatus status = RCSHOW_UNCHECKED;
    bool isSkipped = false;
    bool isAvailable = false;
    bool wothCandidate = false;
    bool barrenCandidate = false;
};
} // namespace Rando