#include "browser.h"
#include "fake_lichii.h"
#include "tracker_painter.h"
#include "tracker_window.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QColorDialog>
#include <QContextMenuEvent>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QTest>
#include <QToolTip>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

static const QColor Red(200, 40, 40);
static const QColor Blue(40, 60, 210);
static const QColor Green(0, 177, 64);
static const TrackerLook FirstLook{ .cell = 48, .background = QColor(0x1c, 0x1f, 0x27), .hidden = {} };

static QImage Flat(const QColor& colour) {
    QImage image(8, 8, QImage::Format_ARGB32_Premultiplied);
    image.fill(colour);
    return image;
}

static CellView Cell(const std::string& block, const std::string& picture, bool lit) {
    return { .block = block, .row = 0, .column = 0, .name = picture, .picture = picture, .lit = lit };
}

// The grid drawn over an opaque colour no cell uses, so a pixel the painter leaves alone shows.
static QImage Painted(const TrackerLook& look, const std::vector<CellView>& cells,
                      const QHash<QString, QImage>& pictures) {
    QImage image(TrackerSize(look, cells), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::magenta);
    QPainter painter(&image);
    PaintTracker(painter, look, cells, pictures);
    return image;
}

static QPoint Centre(const TrackerLook& look, const std::vector<CellView>& cells) {
    return TrackerBoxes(look, cells).front().center();
}

static bool IsGrey(const QColor& colour) {
    return colour.red() == colour.green() && colour.green() == colour.blue();
}

static int Count(const QImage& image, const std::function<bool(const QColor&)>& counted) {
    int count = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            count += counted(image.pixelColor(x, y)) ? 1 : 0;
        }
    }
    return count;
}

static QColor ColourOf(const std::string& picture) {
    const quint32 hash = Murmur3(picture);
    return QColor(hash & 0xFF, hash >> 8 & 0xFF, hash >> 16 & 0xFF);
}

static QHash<QString, QImage> Pictures(const Tracker& tracker) {
    QHash<QString, QImage> pictures;
    for (const std::string& name : tracker.pictures()) {
        pictures.insert(QString::fromStdString(name), Flat(ColourOf(name)));
    }
    return pictures;
}

static QPoint CentreOf(const std::string& name) {
    const std::vector<CellView> cells = Tracker().cells();
    const auto cell = std::ranges::find(cells, name, &CellView::name);
    return TrackerBoxes(FirstLook, cells)[cell - cells.begin()].center();
}

static QColor ColourAt(const QImage& shot, const std::string& name) {
    return shot.pixelColor(CentreOf(name) * shot.devicePixelRatio());
}

static quint16 ClosedPort() {
    QTcpServer server;
    server.listen(QHostAddress::LocalHost);
    return server.serverPort();
}

static QAction* Entry(QWidget& window, const QStringList& path) {
    QMenu* menu = window.findChild<QMenu*>(Qt::FindDirectChildrenOnly);
    QAction* entry = nullptr;
    for (const QString& step : path) {
        const QList<QAction*> actions = menu->actions();
        const auto found = std::ranges::find(actions, step, &QAction::text);
        if (found == actions.end()) {
            return nullptr;
        }
        entry = *found;
        menu = entry->menu();
    }
    return entry;
}

static bool Choose(QWidget& window, const QStringList& path) {
    QContextMenuEvent click(QContextMenuEvent::Mouse, QPoint(1, 1), window.mapToGlobal(QPoint(1, 1)));
    QApplication::sendEvent(&window, &click);
    QMenu* menu = window.findChild<QMenu*>(Qt::FindDirectChildrenOnly);
    QAction* entry = Entry(window, path);
    if (menu == nullptr || !menu->isVisible() || entry == nullptr) {
        return false;
    }
    entry->trigger();
    menu->hide();
    return true;
}

static QUrl PageAt(quint16 port, const QString& host = "127.0.0.1") {
    return QUrl(QString("http://%1:%2/").arg(host).arg(port));
}

static QImage Served(quint16 port) {
    return QImage::fromData(Read(Ask(PageAt(port), Get(Picture(Current(PageAt(port)))))).body, "PNG");
}

static int Alpha(int strength) {
    return qRound(255 * strength / 100.0);
}

static QDialog* BrowserSource(QWidget& window) {
    if (!Choose(window, { "Browser source…" })) {
        return nullptr;
    }
    auto* dialog = window.findChild<QDialog*>();
    return dialog != nullptr && dialog->isVisible() && dialog->windowTitle() == "Browser source" ? dialog : nullptr;
}

static QStringList Links(const QDialog& dialog) {
    QList<QLineEdit*> fields = dialog.findChildren<QLineEdit*>();
    fields.removeIf([](const QLineEdit* field) { return !field->isVisible(); });
    std::ranges::sort(fields, {}, [&](const QLineEdit* field) { return field->mapTo(&dialog, QPoint()).y(); });
    QStringList links;
    for (const QLineEdit* field : fields) {
        links << field->text();
    }
    return links;
}

static bool Shows(const QDialog& dialog, const QString& text) {
    return std::ranges::any_of(dialog.findChildren<QLabel*>(),
                               [&](const QLabel* label) { return label->isVisible() && label->text() == text; });
}

static bool ChooseBackground(QWidget& window, const QColor& colour) {
    if (!Choose(window, { "Background colour…" })) {
        return false;
    }
    auto* dialog = window.findChild<QColorDialog*>();
    if (dialog == nullptr || !dialog->isVisible()) {
        return false;
    }
    dialog->setCurrentColor(colour);
    dialog->accept();
    return true;
}

class TrackerWindowTest : public QObject {
    Q_OBJECT

private slots:
    void init() {
        QSettings().clear();
    }

    void aLitCellShowsItsPicture() {
        const std::vector cells = { Cell("items", "red", true) };
        QCOMPARE(Painted(FirstLook, cells, { { "red", Flat(Red) } }).pixelColor(Centre(FirstLook, cells)), Red);
    }

    void anUnlitCellShowsItsPictureInAFaintGrey() {
        const TrackerLook black{ .cell = 48, .background = Qt::black, .hidden = {} };
        const std::vector cells = { Cell("items", "red", false) };
        const QColor shown = Painted(black, cells, { { "red", Flat(Red) } }).pixelColor(Centre(black, cells));
        QVERIFY2(IsGrey(shown) && shown.red() > 0 && shown.red() < qGray(Red.rgb()), qPrintable(shown.name()));
    }

    void aTintedCellShowsItsTint() {
        std::vector cells = { Cell("songs", "white", true) };
        cells.front().tint = "#6fd65c";
        QCOMPARE(Painted(FirstLook, cells, { { "white", Flat(Qt::white) } }).pixelColor(Centre(FirstLook, cells)),
                 QColor("#6fd65c"));
    }

    void aCountIsDrawnAtTheCellsLowerRight() {
        std::vector cells = { Cell("items", "red", true) };
        const QImage plain = Painted(FirstLook, cells, { { "red", Flat(Red) } });
        cells.front().count = 7;
        const QImage counted = Painted(FirstLook, cells, { { "red", Flat(Red) } });
        const QPoint centre = Centre(FirstLook, cells);
        int drawn = 0;
        for (int y = 0; y < plain.height(); ++y) {
            for (int x = 0; x < plain.width(); ++x) {
                if (plain.pixel(x, y) != counted.pixel(x, y)) {
                    ++drawn;
                    QVERIFY2(x > centre.x() && y > centre.y(), qPrintable(QString("%1, %2").arg(x).arg(y)));
                }
            }
        }
        QVERIFY(drawn > 0);
    }

    void aCountOfAllIsDrawnInAColourOfItsOwn() {
        const TrackerLook black{ .cell = 128, .background = Qt::black, .hidden = {} };
        std::vector cells = { Cell("dungeons", "clear", true) };
        cells.front().total = 5;
        const auto coloured = [&](int count) {
            cells.front().count = count;
            return Count(Painted(black, cells, { { "clear", Flat(Qt::transparent) } }),
                         [](const QColor& colour) { return !IsGrey(colour); });
        };
        QCOMPARE(coloured(2), 0);
        QVERIFY(coloured(5) > 0);
    }

    void aHiddenBlockTakesNoRoom() {
        const std::vector items = { Cell("items", "red", true) };
        const std::vector both = { Cell("items", "red", true), Cell("songs", "blue", true) };
        TrackerLook hiding = FirstLook;
        hiding.hidden = { "songs" };
        QCOMPARE(TrackerSize(hiding, both), TrackerSize(FirstLook, items));
        QVERIFY(TrackerSize(FirstLook, both).height() > TrackerSize(FirstLook, items).height());
        QCOMPARE(Count(Painted(hiding, both, { { "red", Flat(Red) }, { "blue", Flat(Blue) } }),
                       [](const QColor& colour) { return colour == Blue; }),
                 0);
    }

    void aPictureFillsACellOfTheLooksSide() {
        for (const int side : { 36, 48, 64 }) {
            TrackerLook look = FirstLook;
            look.cell = side;
            const QImage image = Painted(look, { Cell("items", "red", true) }, { { "red", Flat(Red) } });
            QCOMPARE(Count(image, [](const QColor& colour) { return colour == Red; }), side * side);
        }
    }

    void theBackgroundFillsTheGrid() {
        const QImage image = Painted(FirstLook, { Cell("items", "red", true) }, { { "red", Flat(Red) } });
        QCOMPARE(image.pixelColor(0, 0), FirstLook.background);
        QCOMPARE(image.pixelColor(image.width() - 1, image.height() - 1), FirstLook.background);
    }

    void aSeeThroughBackgroundLeavesSeeThroughPixels() {
        const TrackerLook clear{ .cell = 48, .background = Qt::transparent, .hidden = {} };
        const QImage image = Painted(clear, { Cell("items", "red", true) }, { { "red", Flat(Red) } });
        QCOMPARE(image.pixelColor(0, 0).alpha(), 0);
    }

    void aWindowLightsWhatTheGameHolds() {
        const Tracker tracker;
        FakeLichii lichii(
            TrackerGame(tracker, { { MenuState.name, Murmur3("Title") }, { "PouchData.Item_Hookshot.StockNum", 1 } }));
        TrackerWindow window(lichii.port, ClosedPort(), Pictures(tracker));
        QStringList statuses;
        connect(&window, &TrackerWindow::statusChanged, this, [&](const QString& status) { statuses << status; });
        window.show();
        QTRY_COMPARE(statuses, QStringList{ "The tracker is waiting for a game to start." });
        const QImage waiting = window.grab().toImage();
        for (const CellView& cell : tracker.cells()) {
            QVERIFY2(ColourAt(waiting, cell.name) != ColourOf(cell.picture), cell.name.c_str());
        }
        lichii.set(MenuState.name, Murmur3("None"));
        QTRY_COMPARE(ColourAt(window.grab().toImage(), "Hookshot"), ColourOf("Item_Hookshot"));
        QCOMPARE(statuses.back(), "The tracker follows the game.");
        lichii.set("PouchData.Item_Longshot.StockNum", 1);
        QTRY_COMPARE(ColourAt(window.grab().toImage(), "Hookshot"), ColourOf("Item_Longshot"));
    }

    void aWindowSaysWhatItDoesUntilItCloses() {
        const Tracker tracker;
        FakeLichii lichii(TrackerGame(tracker, { { MenuState.name, Murmur3("Title") } }), {});
        auto window = std::make_unique<TrackerWindow>(lichii.port, ClosedPort(), Pictures(tracker));
        QStringList statuses;
        connect(window.get(), &TrackerWindow::statusChanged, this, [&](const QString& status) { statuses << status; });
        const QString another =
            "Lichii runs another game, or another version of this one. Kaepora works only with version 2.0.0.";
        QTRY_COMPARE(statuses, QStringList{ another });
        lichii.quit();
        lichii.start({ ProgramAt });
        QTRY_COMPARE(statuses, QStringList({ another, "The tracker is waiting for a game to start." }));
        lichii.set(MenuState.name, Murmur3("None"));
        QTRY_COMPARE(statuses, QStringList({ another, "The tracker is waiting for a game to start.",
                                             "The tracker follows the game." }));
        window.reset();
        QTRY_COMPARE(lichii.connections(), 0);
    }

    void iconSizeChangesTheCells() {
        const Tracker tracker;
        TrackerWindow window(ClosedPort(), ClosedPort(), Pictures(tracker));
        window.show();
        QCOMPARE(window.windowTitle(), "Kaepora Tracker");
        QCOMPARE(window.size(), TrackerSize(FirstLook, tracker.cells()));
        QVERIFY(Entry(window, { "Icon size", "Medium" })->isChecked());
        QVERIFY(Choose(window, { "Icon size", "Large" }));
        QCOMPARE(window.size(),
                 TrackerSize({ .cell = 64, .background = FirstLook.background, .hidden = {} }, tracker.cells()));
        QVERIFY(Choose(window, { "Icon size", "Small" }));
        QCOMPARE(window.size(),
                 TrackerSize({ .cell = 36, .background = FirstLook.background, .hidden = {} }, tracker.cells()));
    }

    void showHidesABlock() {
        const Tracker tracker;
        TrackerWindow window(ClosedPort(), ClosedPort(), Pictures(tracker));
        window.show();
        QVERIFY(Choose(window, { "Show", "Songs" }));
        QCOMPARE(window.size(), TrackerSize({ .cell = 48, .background = FirstLook.background, .hidden = { "songs" } },
                                            tracker.cells()));
        QVERIFY(Choose(window, { "Show", "Songs" }));
        QCOMPARE(window.size(), TrackerSize(FirstLook, tracker.cells()));
    }

    void backgroundColourSetsTheColour() {
        TrackerWindow window(ClosedPort(), ClosedPort(), Pictures(Tracker()));
        window.show();
        QVERIFY(ChooseBackground(window, Green));
        QCOMPARE(window.grab().toImage().pixelColor(0, 0), Green);
    }

    void aNewWindowOpensAsTheLastWasLeft() {
        const Tracker tracker;
        const quint16 port = ClosedPort();
        auto first = std::make_unique<TrackerWindow>(port, ClosedPort(), Pictures(tracker));
        first->show();
        QVERIFY(Choose(*first, { "Icon size", "Small" }));
        QVERIFY(Choose(*first, { "Show", "Songs" }));
        QVERIFY(ChooseBackground(*first, Green));
        first->move(123, 45);
        first->close();
        first.reset();

        TrackerWindow second(port, ClosedPort(), Pictures(tracker));
        second.show();
        QCOMPARE(second.size(),
                 TrackerSize({ .cell = 36, .background = Green, .hidden = { "songs" } }, tracker.cells()));
        QCOMPARE(second.pos(), QPoint(123, 45));
        QCOMPARE(second.grab().toImage().pixelColor(0, 0), Green);
        QVERIFY(Entry(second, { "Icon size", "Small" })->isChecked());
        QVERIFY(!Entry(second, { "Show", "Songs" })->isChecked());
    }

    void pointingAtACellShowsItsName() {
        TrackerWindow window(ClosedPort(), ClosedPort(), Pictures(Tracker()));
        window.show();
        const QPoint hammer = CentreOf("Megaton Hammer");
        QHelpEvent help(QEvent::ToolTip, hammer, window.mapToGlobal(hammer));
        QApplication::sendEvent(&window, &help);
        QTRY_COMPARE(QToolTip::text(), "Megaton Hammer");
    }

    void thePageShowsTheGridOverTheBackgroundAtItsStrength() {
        const Tracker tracker;
        FakeLichii lichii(
            TrackerGame(tracker, { { MenuState.name, Murmur3("None") }, { "PouchData.Item_Hookshot.StockNum", 1 } }));
        const quint16 port = ClosedPort();
        TrackerWindow window(lichii.port, port, Pictures(tracker));
        window.show();
        QTRY_COMPARE(window.grab().toImage().pixelColor(CentreOf("Hookshot")), ColourOf("Item_Hookshot"));
        const QImage grid = window.grab().toImage();
        const QImage page = Served(port);
        QCOMPARE(page.size(), grid.size());
        QCOMPARE(page.pixelColor(CentreOf("Hookshot")), ColourOf("Item_Hookshot"));
        QCOMPARE(page.pixelColor(0, 0).alpha(), Alpha(70));

        QDialog* dialog = BrowserSource(window);
        QVERIFY(dialog != nullptr);
        dialog->findChild<QSlider*>()->setValue(100);
        QCOMPARE(Served(port).pixelColor(0, 0), grid.pixelColor(0, 0));
        dialog->findChild<QSlider*>()->setValue(0);
        QCOMPARE(Served(port).pixelColor(0, 0).alpha(), 0);
    }

    void aListenerIsToldWhenACellLightsOrTheLookChangesWithTheWindowHidden() {
        const Tracker tracker;
        FakeLichii lichii(TrackerGame(tracker, { { MenuState.name, Murmur3("None") } }));
        const quint16 port = ClosedPort();
        TrackerWindow window(lichii.port, port, Pictures(tracker));
        QStringList statuses;
        connect(&window, &TrackerWindow::statusChanged, this, [&](const QString& status) { statuses << status; });
        QTRY_COMPARE(statuses, QStringList{ "The tracker follows the game." });
        QTcpSocket stream;
        Open(stream, PageAt(port), Get(Changes));
        QTRY_COMPARE(Told(stream).size(), 1);
        const int reads = 4 * int(GameWatch::ReadPause.count());
        QTest::qWait(reads);
        QCOMPARE(Told(stream).size(), 1);
        lichii.set("PouchData.Item_Hookshot.StockNum", 1);
        QTRY_COMPARE(Told(stream).size(), 2);
        QTest::qWait(reads);
        QCOMPARE(Told(stream).size(), 2);
        QVERIFY(Choose(window, { "Icon size", "Large" }));
        QTRY_COMPARE(Told(stream).size(), 3);
    }

    void browserSourceGivesTheLinkAndThePagesSize() {
        const quint16 port = ClosedPort();
        TrackerWindow window(ClosedPort(), port, Pictures(Tracker()));
        window.show();
        QVERIFY(Choose(window, { "Icon size", "Large" }));
        QVERIFY(Choose(window, { "Show", "Songs" }));
        QDialog* dialog = BrowserSource(window);
        QVERIFY(dialog != nullptr);
        QCOMPARE(Links(*dialog), QStringList{ PageAt(port).toString() });
        const QSize size = Served(port).size();
        QVERIFY(
            Shows(*dialog,
                  QString("Set the source's width to %1 and its height to %2.").arg(size.width()).arg(size.height())));
        QCOMPARE(size, window.size());
        const QList<QPushButton*> buttons = dialog->findChildren<QPushButton*>();
        const auto copy = std::ranges::find(buttons, QString("Copy"), &QPushButton::text);
        QVERIFY(copy != buttons.end());
        (*copy)->click();
        QCOMPARE(QApplication::clipboard()->text(), PageAt(port).toString());
    }

    void theBoxLetsOtherComputersShowTheTracker() {
        const std::optional<QHostAddress> address = NetworkAddress();
        if (!address) {
            QSKIP("This computer has no network address.");
        }
        const quint16 port = ClosedPort();
        const QUrl there = PageAt(port, address->toString());
        TrackerWindow window(ClosedPort(), port, Pictures(Tracker()));
        window.show();
        QDialog* dialog = BrowserSource(window);
        QVERIFY(dialog != nullptr);
        auto* box = dialog->findChild<QCheckBox*>();
        QCOMPARE(box->text(), "Let other computers on my network show the tracker");
        QVERIFY(!box->isChecked());
        QVERIFY(Ask(there, Get("/")).isEmpty());

        box->click();
        QCOMPARE(Read(Ask(there, Get("/"))).code, 200);
        const QStringList links = Links(*dialog);
        QCOMPARE(links.front(), PageAt(port).toString());
        QVERIFY2(links.contains(there.toString()), qPrintable(links.join(' ')));
        for (const QString& link : links.mid(1, links.size() - 2)) {
            QVERIFY2(!QHostAddress(QUrl(link).host()).isNull(), qPrintable(links.join(' ')));
        }
        QVERIFY2(QHostAddress(QUrl(links.back()).host()).isNull(), qPrintable(links.join(' ')));
        QVERIFY(Shows(*dialog, "In OBS on the other computer, use one of these links."));
        QVERIFY(Shows(*dialog, QString("Both computers must be on the same network. If the page does not load there, "
                                       "allow port %1 through this computer's firewall.")
                                   .arg(port)));

        box->click();
        QVERIFY(Ask(there, Get("/")).isEmpty());
        QCOMPARE(Links(*dialog), QStringList{ PageAt(port).toString() });
        QCOMPARE(Read(Ask(PageAt(port), Get("/"))).code, 200);
    }

    void aNewWindowServesThePageAsTheLastLeftIt() {
        const std::optional<QHostAddress> address = NetworkAddress();
        if (!address) {
            QSKIP("This computer has no network address.");
        }
        const quint16 port = ClosedPort();
        auto first = std::make_unique<TrackerWindow>(ClosedPort(), port, Pictures(Tracker()));
        first->show();
        QDialog* dialog = BrowserSource(*first);
        QVERIFY(dialog != nullptr);
        dialog->findChild<QCheckBox*>()->click();
        dialog->findChild<QSlider*>()->setValue(30);
        first->close();
        first.reset();

        TrackerWindow second(ClosedPort(), port, Pictures(Tracker()));
        second.show();
        QCOMPARE(Read(Ask(PageAt(port, address->toString()), Get("/"))).code, 200);
        QCOMPARE(Served(port).pixelColor(0, 0).alpha(), Alpha(30));
        dialog = BrowserSource(second);
        QVERIFY(dialog != nullptr);
        QVERIFY(dialog->findChild<QCheckBox*>()->isChecked());
        QCOMPARE(dialog->findChild<QSlider*>()->value(), 30);
    }

    void browserSourceSaysWhenAnotherProgramHoldsThePort() {
        QTcpServer holder;
        QVERIFY(holder.listen(QHostAddress::LocalHost));
        const quint16 port = holder.serverPort();
        TrackerWindow window(ClosedPort(), port, Pictures(Tracker()));
        window.show();
        QDialog* dialog = BrowserSource(window);
        QVERIFY(dialog != nullptr);
        QVERIFY(Shows(*dialog, QString("Kaepora cannot serve the page. Another program is using port %1.").arg(port)));
        QCOMPARE(Links(*dialog), QStringList());

        holder.close();
        dialog->close();
        QTRY_COMPARE(window.findChild<QDialog*>(), nullptr);
        dialog = BrowserSource(window);
        QVERIFY(dialog != nullptr);
        QCOMPARE(Links(*dialog), QStringList{ PageAt(port).toString() });
        QCOMPARE(Read(Ask(PageAt(port), Get("/"))).code, 200);
    }
};

int main(int argc, char** argv) {
    // The window remembers its look in the settings, so they go to a file in a temporary folder.
    QTemporaryDir settings;
    if (!settings.isValid()) {
        return 1;
    }
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    QApplication app(argc, argv);
    QApplication::setOrganizationName("kaepora");
    QApplication::setApplicationName("kaepora");
    TrackerWindowTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tracker_window_test.moc"
