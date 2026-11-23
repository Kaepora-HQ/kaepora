#include "tracker_window.h"

#include <QActionGroup>
#include <QCheckBox>
#include <QClipboard>
#include <QColorDialog>
#include <QContextMenuEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHelpEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QStyle>
#include <QToolTip>
#include <QVBoxLayout>

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
constexpr int FirstStrength = 70;

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
constexpr char StrengthKey[] = "tracker/strength";
constexpr char NetworkKey[] = "tracker/network";

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

static int RememberedStrength() {
    bool read = false;
    const int strength = QSettings().value(StrengthKey).toInt(&read);
    return read && strength >= 0 && strength <= 100 ? strength : FirstStrength;
}

static QLabel* Words(const QString& words) {
    auto* label = new QLabel(words);
    label->setWordWrap(true);
    return label;
}

static QWidget* Link(const QString& link) {
    auto* row = new QWidget;
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins({});
    auto* field = new QLineEdit(link);
    field->setReadOnly(true);
    auto* copy = new QPushButton("Copy");
    QObject::connect(copy, &QPushButton::clicked, field,
                     [field] { QGuiApplication::clipboard()->setText(field->text()); });
    layout->addWidget(field);
    layout->addWidget(copy);
    return row;
}

TrackerWindow::TrackerWindow(quint16 port, quint16 pagePort, QHash<QString, QImage> pictures)
    : pictures(std::move(pictures)), cells(tracker.cells()), look(RememberedLook()),
      backgroundStrength(RememberedStrength()), network(QSettings().value(NetworkKey).toBool()), menu(new QMenu(this)),
      pagePort(pagePort), page(pagePort), watch(port, tracker.values()) {
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
    menu->addSeparator();
    menu->addAction("Browser source…", this, &TrackerWindow::openBrowserSource);

    restoreGeometry(QSettings().value(PlaceKey).toByteArray());
    setFixedSize(TrackerSize(look, cells));
    showOnPage();
    page.listen(network);

    connect(&watch, &GameWatch::changed, this, [this](const std::vector<quint32>& read) {
        if (tracker.take(read)) {
            cells = tracker.cells();
            update();
            showOnPage();
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

// The strength is remembered here, not as the slider moves: a write of the settings takes about 20 ms.
void TrackerWindow::closeEvent(QCloseEvent* event) {
    QSettings settings;
    settings.setValue(PlaceKey, saveGeometry());
    settings.setValue(StrengthKey, backgroundStrength);
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
    showOnPage();
}

void TrackerWindow::showOnPage() {
    TrackerLook shown = look;
    shown.background.setAlphaF(backgroundStrength / 100.0);
    QImage picture(TrackerSize(look, cells), QImage::Format_ARGB32_Premultiplied);
    QPainter painter(&picture);
    PaintTracker(painter, shown, cells, pictures);
    painter.end();
    page.show(picture);
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

void TrackerWindow::openBrowserSource() {
    if (page.links().isEmpty()) {
        page.listen(network);
    }
    auto* dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle("Browser source");
    auto* layout = new QVBoxLayout(dialog);
    layout->addWidget(Words("OBS can show the tracker with the game showing through behind it. In OBS, add a Browser "
                            "source and paste this link."));
    auto* local = new QWidget;
    new QVBoxLayout(local);
    local->layout()->setContentsMargins({});
    layout->addWidget(local);
    const QSize size = TrackerSize(look, cells);
    layout->addWidget(
        Words(QString("Set the source's width to %1 and its height to %2.").arg(size.width()).arg(size.height())));

    auto* background = new QSlider(Qt::Horizontal);
    background->setRange(0, 100);
    background->setValue(backgroundStrength);
    connect(background, &QSlider::valueChanged, this, [this](int value) {
        backgroundStrength = value;
        showOnPage();
    });
    auto* ends = new QGridLayout;
    ends->addWidget(background, 0, 0, 1, 2);
    ends->addWidget(new QLabel("See-through"), 1, 0, Qt::AlignLeft);
    ends->addWidget(new QLabel("Solid"), 1, 1, Qt::AlignRight);
    auto* form = new QFormLayout;
    form->addRow("Background", ends);
    layout->addLayout(form);

    auto* box = new QCheckBox("Let other computers on my network show the tracker");
    box->setChecked(network);
    layout->addWidget(box);
    auto* otherComputers = new QWidget;
    new QVBoxLayout(otherComputers);
    const int boxTextIndent =
        style()->pixelMetric(QStyle::PM_IndicatorWidth) + style()->pixelMetric(QStyle::PM_CheckBoxLabelSpacing);
    otherComputers->layout()->setContentsMargins(boxTextIndent, 0, 0, 0);
    layout->addWidget(otherComputers);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    layout->addWidget(buttons);

    const auto showLinks = [this, local, otherComputers] {
        qDeleteAll(local->findChildren<QWidget*>(Qt::FindDirectChildrenOnly));
        qDeleteAll(otherComputers->findChildren<QWidget*>(Qt::FindDirectChildrenOnly));
        const QStringList links = page.links();
        otherComputers->setVisible(network && !links.isEmpty());
        if (links.isEmpty()) {
            local->layout()->addWidget(
                Words(QString("Kaepora cannot serve the page. Another program is using port %1.").arg(pagePort)));
            return;
        }
        local->layout()->addWidget(Link(links.front()));
        if (network) {
            otherComputers->layout()->addWidget(Words("In OBS on the other computer, use one of these links."));
            for (const QString& link : links.sliced(1)) {
                otherComputers->layout()->addWidget(Link(link));
            }
            otherComputers->layout()->addWidget(
                Words(QString("Both computers must be on the same network. If the page does "
                              "not load there, allow port %1 through this computer's firewall.")
                          .arg(pagePort)));
        }
    };
    connect(box, &QCheckBox::toggled, dialog, [this, showLinks](bool on) {
        network = on;
        QSettings().setValue(NetworkKey, network);
        page.listen(network);
        showLinks();
    });
    showLinks();
    dialog->open();
}

void TrackerWindow::setStatus(const QString& status) {
    if (status != lastStatus) {
        lastStatus = status;
        emit statusChanged(status);
    }
}
