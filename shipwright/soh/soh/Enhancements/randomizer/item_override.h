#pragma once

#include "randomizerTypes.h"

namespace Rando {
/// @brief Class representing overrides of individual items. Used for trick names and models for ice traps.
class ItemOverride {
  public:
    ItemOverride();
    RandomizerCheck GetLocation() const;
    void SetLocation(RandomizerCheck location);
    RandomizerGet LooksLike() const;
    RandomizerGet& RefLooksLike();

  private:
    RandomizerCheck mLocation;
    RandomizerGet mLooksLike;
};
} // namespace Rando