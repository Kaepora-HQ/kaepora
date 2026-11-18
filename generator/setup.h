#pragma once

#include "seed_settings.h"

void InitializeRandomizer();

// The options of the unmodified game into the seed context, then the settings the player chose over them. Generating a
// seed rewrites some options, so each seed starts with these, as in SoH's menu.
void SetOptions(const SeedSettings& settings);
