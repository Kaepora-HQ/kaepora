#pragma once

#include "tracker.h"

#include <QColor>
#include <QHash>
#include <QImage>
#include <QRect>
#include <QSize>
#include <QString>

#include <set>
#include <span>
#include <string>
#include <vector>

class QPainter;

struct TrackerLook {
    int cell; // the side of an item's cell, in pixels
    QColor background;
    std::set<std::string> hidden;
};

QSize TrackerSize(const TrackerLook& look, std::span<const CellView> cells);

// Where each cell is drawn, in the order of cells: an empty box for a cell of a hidden block.
std::vector<QRect> TrackerBoxes(const TrackerLook& look, std::span<const CellView> cells);

// Fills TrackerSize from the painter's origin with the background, then draws each cell's picture by its name.
void PaintTracker(QPainter& painter, const TrackerLook& look, std::span<const CellView> cells,
                  const QHash<QString, QImage>& pictures);
