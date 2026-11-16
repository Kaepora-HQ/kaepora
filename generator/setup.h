#pragma once

// The randomizer steps of SoH's start-up, then the options of the unmodified game into the seed context.
void InitializeRandomizer();

// The options of the unmodified game into the seed context. Generating a seed rewrites some of them, so each seed
// starts with these, as in SoH's menu.
void SetUnmodifiedGameOptions();
