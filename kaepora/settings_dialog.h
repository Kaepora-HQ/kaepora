#pragma once

#include "generator/seed_settings.h"

#include <QString>

#include <functional>

class QWidget;

// How a seed's spoiler log names its gifts. Every release from 0.2.0 to 0.5.0 wrote "Shuffled" for the same 21 places,
// which no set of today's groups names; the settings read from such a log hold the groups "Shuffled" means today.
enum class LoggedGifts { AsGroups, ShuffledByARelease };

QString Summary(const SeedSettings& settings, LoggedGifts gifts = LoggedGifts::AsGroups);

QString Details(const SeedSettings& settings, LoggedGifts gifts = LoggedGifts::AsGroups);

// Opens the dialog over the window, showing the settings given, and returns at once. OK calls chosen with the settings
// the dialog shows. Cancel calls nothing.
void ChooseSettings(QWidget* window, const SeedSettings& settings,
                    const std::function<void(const SeedSettings&)>& chosen);
