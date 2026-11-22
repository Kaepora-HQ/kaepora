#include "tracker_painter.h"

#include <QPainter>
#include <QTest>

#include <functional>
#include <vector>

static const QColor Red(200, 40, 40);
static const QColor Blue(40, 60, 210);
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

class TrackerWindowTest : public QObject {
    Q_OBJECT

private slots:
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
};

QTEST_MAIN(TrackerWindowTest)

#include "tracker_window_test.moc"
