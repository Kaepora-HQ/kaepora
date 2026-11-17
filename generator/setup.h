// SoH's randomizer start-up, shared by the generator and the logic test program.
#pragma once

#include "seed_settings.h"

void InitializeRandomizer();

// The options of the unmodified game into the seed context, then whether a rule may ask for the Lens of Truth.
// Generating a seed rewrites some options, so each seed starts with these, as in SoH's menu.
void SetOptions(Lens lens);
