#pragma once

#include "generator/seed_settings.h"

#include <QString>

#include <functional>

class QWidget;

QString Summary(const SeedSettings& settings);

QString Details(const SeedSettings& settings);

// Opens the dialog over the window, showing the settings given, and returns at once. OK calls chosen with the settings
// the dialog shows. Cancel calls nothing.
void ChooseSettings(QWidget* window, const SeedSettings& settings,
                    const std::function<void(const SeedSettings&)>& chosen);
