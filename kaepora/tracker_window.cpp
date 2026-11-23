#include "tracker_window.h"

#include <QActionGroup>
#include <QColorDialog>
#include <QContextMenuEvent>
#include <QHelpEvent>
#include <QMenu>
#include <QPainter>
#include <QSettings>
#include <QToolTip>

#include <algorithm>

// OBS finds a captured window again by its title.
constexpr char Title[] = "Kaepora Tracker";

constexpr char Waiting[] = "The tracker is waiting for a game to start.";
constexpr char Following[] = "The tracker follows the game.";

struct Size {
    const char* name;
    int cell;
};
constexpr Size Sizes[] = { { "Small", 36 }, { "Medium", 48 }, { "Large", 64 } };
constexpr int FirstCell = 48;
constexpr QColor FirstBackground(0x1c, 0x1f, 0x27);

struct Block {
    const char* block;
    const char* name;
};
constexpr Block Blocks[] = { { "items", "Items" },
                             { "equipment", "Equipment" },
                             { "quest", "Quest items" },
                             { "songs", "Songs" },
                             { "dungeons", "Dungeon keys" } };

constexpr char CellKey[] = "tracker/cell";
constexpr char BackgroundKey[] = "tracker/background";
constexpr char HiddenKey[] = "tracker/hidden";
constexpr char PlaceKey[] = "tracker/place";

static TrackerLook RememberedLook() {
    const QSettings settings;
    const int cell = settings.value(CellKey).toInt();
    const QColor background = QColor::fromString(settings.value(BackgroundKey).toString());
    std::set<std::string> hidden;
    for (const QString& block : settings.value(HiddenKey).toStringList()) {
        hidden.insert(block.toStdString());
    }
    return { .cell = std::ranges::find(Sizes, cell, &Size::cell) != std::end(Sizes) ? cell : FirstCell,
             .background = background.isValid() ? background : FirstBackground,
             .hidden = hidden };
}

TrackerWindow::TrackerWindow(quint16 port, QHash<QString, QImage> pictures)
    : pictures(std::move(pictures)), cells(tracker.cells()), look(RememberedLook()), menu(new QMenu(this)),
      watch(port, tracker.values()) {
    setWindowTitle(Title);

    QMenu* sizes = menu->addMenu("Icon size");
    auto* sizeGroup = new QActionGroup(sizes);
    for (const auto& [name, cell] : Sizes) {
        QAction* size = sizeGroup->addAction(name);
        size->setCheckable(true);
        size->setChecked(cell == look.cell);
        sizes->addAction(size);
        connect(size, &QAction::triggered, this, [this, cell] {
            look.cell = cell;
            lookChanged();
        });
    }
    menu->addAction("Background colour…", this, &TrackerWindow::chooseBackground);
    QMenu* show = menu->addMenu("Show");
    for (const auto& [block, name] : Blocks) {
        QAction* shown = show->addAction(name);
        shown->setCheckable(true);
        shown->setChecked(!look.hidden.contains(block));
        connect(shown, &QAction::toggled, this, [this, block](bool on) {
            if (on) {
                look.hidden.erase(block);
            } else {
                look.hidden.insert(block);
            }
            lookChanged();
        });
    }

    restoreGeometry(QSettings().value(PlaceKey).toByteArray());
    setFixedSize(TrackerSize(look, cells));

    connect(&watch, &GameWatch::changed, this, [this](const std::vector<quint32>& read) {
        if (tracker.take(read)) {
            cells = tracker.cells();
            update();
        }
        setStatus(tracker.inPlay() ? Following : Waiting);
    });
    connect(&watch, &GameWatch::unreadable, this, &TrackerWindow::setStatus);
    watch.start();
}

bool TrackerWindow::event(QEvent* event) {
    if (event->type() != QEvent::ToolTip) {
        return QWidget::event(event);
    }
    const auto* help = static_cast<QHelpEvent*>(event);
    const std::vector<QRect> boxes = TrackerBoxes(look, cells);
    const auto box = std::ranges::find_if(boxes, [&](const QRect& box) { return box.contains(help->pos()); });
    if (box == boxes.end()) {
        QToolTip::hideText();
    } else {
        QToolTip::showText(help->globalPos(), QString::fromStdString(cells[box - boxes.begin()].name), this, *box);
    }
    return true;
}

void TrackerWindow::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    PaintTracker(painter, look, cells, pictures);
}

void TrackerWindow::contextMenuEvent(QContextMenuEvent* event) {
    menu->popup(event->globalPos());
}

void TrackerWindow::closeEvent(QCloseEvent* event) {
    QSettings().setValue(PlaceKey, saveGeometry());
    QWidget::closeEvent(event);
}

void TrackerWindow::lookChanged() {
    QSettings settings;
    settings.setValue(CellKey, look.cell);
    settings.setValue(BackgroundKey, look.background.name());
    QStringList hidden;
    for (const std::string& block : look.hidden) {
        hidden << QString::fromStdString(block);
    }
    settings.setValue(HiddenKey, hidden);
    setFixedSize(TrackerSize(look, cells));
    update();
}

void TrackerWindow::chooseBackground() {
    auto* dialog = new QColorDialog(look.background, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &QColorDialog::colorSelected, this, [this](const QColor& colour) {
        look.background = colour;
        lookChanged();
    });
    dialog->open();
}

void TrackerWindow::setStatus(const QString& status) {
    if (status != lastStatus) {
        lastStatus = status;
        emit statusChanged(status);
    }
}
