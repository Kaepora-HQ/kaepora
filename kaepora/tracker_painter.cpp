#include "tracker_painter.h"

#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QPaintDevice>

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

namespace {

constexpr int Across = 6;

constexpr qreal Faint = 0.3;
constexpr QColor Outline(0, 0, 0, 230);
constexpr QColor CountColour(255, 255, 255);
constexpr QColor FullColour(122, 227, 95);
constexpr QColor LabelColour(226, 230, 245);

struct Layout {
    QSize size;
    std::vector<QRect> boxes;
    std::vector<std::pair<QRect, QString>> labels;
};

struct Block {
    std::string name;
    int rows = 0;
    int columns = 0;
    bool labelled = false;
};

Layout LayOut(const TrackerLook& look, std::span<const CellView> cells) {
    const int gap = look.cell / 12;
    const int between = look.cell / 4;
    const int edge = look.cell / 4;
    const int labelHeight = look.cell * 3 / 10;
    const int width = Across * look.cell + (Across - 1) * gap;

    std::vector<Block> blocks;
    for (const CellView& cell : cells) {
        if (look.hidden.contains(cell.block)) {
            continue;
        }
        auto block = std::ranges::find(blocks, cell.block, &Block::name);
        if (block == blocks.end()) {
            block = blocks.insert(blocks.end(), { .name = cell.block });
        }
        block->rows = std::max(block->rows, cell.row + 1);
        block->columns = std::max(block->columns, cell.column + 1);
        block->labelled = block->labelled || !cell.label.empty();
    }

    Layout layout{ .boxes = std::vector<QRect>(cells.size()) };
    int top = edge;
    for (const Block& block : blocks) {
        const int side = std::min(look.cell, (width - (block.columns - 1) * gap) / block.columns);
        const int left = edge + (width - block.columns * side - (block.columns - 1) * gap) / 2;
        if (block.labelled) {
            top += labelHeight;
        }
        for (size_t i = 0; i < cells.size(); ++i) {
            const CellView& cell = cells[i];
            if (cell.block != block.name) {
                continue;
            }
            const QRect box(left + cell.column * (side + gap), top + cell.row * (side + gap), side, side);
            layout.boxes[i] = box;
            if (!cell.label.empty()) {
                layout.labels.emplace_back(QRect(box.x() - gap, top - labelHeight, side + 2 * gap, labelHeight),
                                           QString::fromStdString(cell.label));
            }
        }
        top += block.rows * side + (block.rows - 1) * gap + between;
    }
    layout.size = QSize(width + 2 * edge, top - (blocks.empty() ? 0 : between) + edge);
    return layout;
}

void Grey(QImage& image) {
    for (int y = 0; y < image.height(); ++y) {
        auto* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            const int level = qGray(line[x]);
            line[x] = qRgba(level, level, level, qAlpha(line[x]));
        }
    }
}

void Multiply(QImage& image, const QColor& tint) {
    for (int y = 0; y < image.height(); ++y) {
        auto* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            const QRgb pixel = line[x];
            line[x] = qRgba(qRed(pixel) * tint.red() / 255, qGreen(pixel) * tint.green() / 255,
                            qBlue(pixel) * tint.blue() / 255, qAlpha(pixel));
        }
    }
}

void DrawText(QPainter& painter, const QPainterPath& text, const QColor& fill, qreal outline) {
    painter.strokePath(text, QPen(Outline, outline, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.fillPath(text, fill);
}

void PaintPicture(QPainter& painter, const QRect& box, const CellView& cell, const QImage& picture) {
    const qreal ratio = painter.device()->devicePixelRatio();
    QImage shown = picture.scaled(box.size() * ratio, Qt::KeepAspectRatio, Qt::SmoothTransformation)
                       .convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (!cell.tint.empty()) {
        Multiply(shown, QColor::fromString(cell.tint));
    }
    if (!cell.lit) {
        Grey(shown);
    }
    shown.setDevicePixelRatio(ratio);
    const QSizeF size = shown.deviceIndependentSize();
    painter.setOpacity(cell.lit ? 1 : Faint);
    painter.drawImage(QPointF(box.x() + std::floor((box.width() - size.width()) / 2),
                              box.y() + std::floor((box.height() - size.height()) / 2)),
                      shown);
    painter.setOpacity(1);
}

void PaintCount(QPainter& painter, const QRect& box, int cell, int count, std::optional<int> total) {
    const QString text = total ? QString("%1/%2").arg(count).arg(*total) : QString::number(count);
    QFont font;
    font.setPixelSize(cell * 3 / 10);
    font.setBold(true);
    const QFontMetricsF metrics(font);
    QPainterPath path;
    path.addText(box.x() + box.width() + cell / 24.0 - metrics.horizontalAdvance(text),
                 box.y() + box.height() + cell / 12.0 - metrics.descent(), font, text);
    DrawText(painter, path, total && count >= *total ? FullColour : CountColour, cell * 0.075);
}

void PaintLabel(QPainter& painter, const QRect& box, const QString& label) {
    QFont font;
    font.setPixelSize(box.height() * 7 / 10);
    font.setWeight(QFont::DemiBold);
    const QFontMetricsF metrics(font);
    QPainterPath path;
    path.addText(box.x() + (box.width() - metrics.horizontalAdvance(label)) / 2, box.y() + metrics.ascent(), font,
                 label);
    DrawText(painter, path, LabelColour, box.height() * 0.15);
}

} // namespace

QSize TrackerSize(const TrackerLook& look, std::span<const CellView> cells) {
    return LayOut(look, cells).size;
}

std::vector<QRect> TrackerBoxes(const TrackerLook& look, std::span<const CellView> cells) {
    return LayOut(look, cells).boxes;
}

void PaintTracker(QPainter& painter, const TrackerLook& look, std::span<const CellView> cells,
                  const QHash<QString, QImage>& pictures) {
    const Layout layout = LayOut(look, cells);
    painter.save();
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.fillRect(QRect(QPoint(), layout.size), look.background);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    painter.setRenderHint(QPainter::Antialiasing);
    for (size_t i = 0; i < cells.size(); ++i) {
        const QRect& box = layout.boxes[i];
        if (box.isEmpty()) {
            continue;
        }
        const CellView& cell = cells[i];
        if (const auto picture = pictures.find(QString::fromStdString(cell.picture)); picture != pictures.end()) {
            PaintPicture(painter, box, cell, *picture);
        }
        if (cell.count > 0) {
            PaintCount(painter, box, look.cell, *cell.count, cell.total);
        }
    }
    for (const auto& [box, label] : layout.labels) {
        PaintLabel(painter, box, label);
    }
    painter.restore();
}
