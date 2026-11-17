#include "item_override.h"

namespace Rando {
ItemOverride::ItemOverride() : mLocation(RC_UNKNOWN_CHECK), mLooksLike(RG_NONE) {
}

RandomizerCheck ItemOverride::GetLocation() const {
    return mLocation;
}

void ItemOverride::SetLocation(const RandomizerCheck location) {
    mLocation = location;
}

RandomizerGet ItemOverride::LooksLike() const {
    return mLooksLike;
}

RandomizerGet& ItemOverride::RefLooksLike() {
    return mLooksLike;
}
} // namespace Rando