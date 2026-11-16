// End-to-end test of the window. It makes real seeds in this process, from the game file in KAEPORA_GAME_NUP, and
// compares them with what kaepora-make-seed, in KAEPORA_MAKE_SEED, makes in a process of its own.
#include "kaepora/window.h"

#include <QApplication>
#include <QDirListing>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <memory>

using Files = QMap<QString, QByteArray>;

static Files FilesIn(const QString& folder) {
    Files files;
    for (const auto& file :
         QDirListing(folder, QDirListing::IteratorFlag::FilesOnly | QDirListing::IteratorFlag::Recursive)) {
        QFile data(file.filePath());
        if (data.open(QIODevice::ReadOnly)) {
            files.insert(QDir(folder).relativeFilePath(file.filePath()), data.readAll());
        }
    }
    return files;
}

static void Write(const QString& path, const QByteArray& data) {
    QDir().mkpath(QFileInfo(path).path());
    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(data);
    }
}

class WindowTest : public QObject {
    Q_OBJECT

public:
    explicit WindowTest(const QString& home) : mods(home + "/mods") {
    }

private:
    // Making a seed takes about a second and a half.
    static constexpr int MakeTimeout = 60'000;

    const QString game = qEnvironmentVariable("KAEPORA_GAME_NUP");
    const QString library = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/kaepora/seeds";
    const QString mods;
    std::unique_ptr<Window> window;

    QPushButton* button(const QString& text) const {
        for (QPushButton* button : window->findChildren<QPushButton*>()) {
            if (button->window() == window.get() && button->text() == text) {
                return button;
            }
        }
        return nullptr;
    }

    QListWidget* list() const {
        return window->findChild<QListWidget*>();
    }

    QStringList listed() const {
        QStringList texts;
        for (int row = 0; row < list()->count(); ++row) {
            texts << list()->item(row)->text();
        }
        return texts;
    }

    bool shows(const QString& text) const {
        const QList<QLabel*> labels = window->findChildren<QLabel*>();
        return std::ranges::any_of(labels, [&](const QLabel* label) { return label->text() == text; });
    }

    QMessageBox* box() const {
        for (QMessageBox* box : window->findChildren<QMessageBox*>()) {
            if (box->isVisible()) {
                return box;
            }
        }
        return nullptr;
    }

    // Types the seed and presses "Create seed". True once the window has made it.
    bool create(const QString& seed) {
        QLineEdit* field = nullptr;
        for (QLineEdit* candidate : window->findChildren<QLineEdit*>()) {
            if (!candidate->isReadOnly()) {
                field = candidate;
            }
        }
        field->clear();
        QTest::keyClicks(field, seed);
        QPushButton* create = button("Create seed");
        create->click();
        return !create->isEnabled() && shows("Creating seed…") &&
               QTest::qWaitFor([&] { return create->isEnabled(); }, MakeTimeout);
    }

    void answer(const QString& text) {
        for (QAbstractButton* answer : box()->buttons()) {
            if (answer->text() == text) {
                answer->click();
            }
        }
    }

    void select(const QString& seed) {
        for (int row = 0; row < list()->count(); ++row) {
            if (list()->item(row)->text().split(' ').front() == seed) {
                list()->setCurrentRow(row);
            }
        }
    }

private slots:
    void init() {
        QDir(library).removeRecursively();
        QDir(mods).removeRecursively();
        QVERIFY(QDir().mkpath(mods));
        QSettings settings;
        settings.clear();
        settings.setValue("gameFile", game);
        settings.setValue("modsFolder", mods);
        window = std::make_unique<Window>();
        window->show();
    }

    void cleanup() {
        window.reset();
    }

    void createSeed() {
        QVERIFY(create("7"));
        QCOMPARE(listed(), QStringList{ "7" });
        QCOMPARE(list()->currentItem()->text(), QString("7"));
        QVERIFY(shows("No seed in use"));
        QVERIFY(QFileInfo(library + "/7/romfs").isDir());
        QVERIFY(QFileInfo(library + "/7/spoiler.json").isFile());
    }

    void useSeed() {
        QVERIFY(create("7"));
        QVERIFY(create("8"));
        const Files seven = FilesIn(library + "/7/romfs");
        const Files eight = FilesIn(library + "/8/romfs");
        QString shared;
        for (const QString& file : seven.keys()) {
            if (eight.contains(file)) {
                shared = file;
            }
        }
        QVERIFY(!shared.isEmpty());
        Write(mods + "/Same files/romfs/" + shared, "other");
        Write(mods + "/Other files/romfs/Pack/Other.pack.zs", "other");
        const Files same = FilesIn(mods + "/Same files");
        const Files other = FilesIn(mods + "/Other files");

        select("7");
        button("Use this seed")->click();
        QVERIFY(box() != nullptr);
        QVERIFY(box()->text().contains("Same files"));
        QVERIFY(!box()->text().contains("Other files"));
        box()->close();
        QCOMPARE(FilesIn(mods + "/Kaepora/romfs"), seven);
        QCOMPARE(listed(), (QStringList{ "8", "7 (in use)" }));

        select("8");
        button("Use this seed")->click();
        QVERIFY(box() != nullptr);
        QVERIFY(box()->text().contains("Same files"));
        box()->close();
        QCOMPARE(FilesIn(mods + "/Kaepora/romfs"), eight);
        QCOMPARE(listed(), (QStringList{ "8 (in use)", "7" }));

        QCOMPARE(FilesIn(mods + "/Same files"), same);
        QCOMPARE(FilesIn(mods + "/Other files"), other);
    }

    void deleteSeedInUse() {
        QVERIFY(create("7"));
        button("Use this seed")->click();
        QVERIFY(box() == nullptr);
        QVERIFY(QFileInfo(mods + "/Kaepora/romfs").isDir());

        button("Delete")->click();
        QVERIFY(box() != nullptr);
        QVERIFY(box()->text().contains("seed 7"));
        QVERIFY(box()->text().contains("in use"));
        answer("Delete");
        QVERIFY(!QFileInfo::exists(library + "/7"));
        QVERIFY(!QFileInfo::exists(mods + "/Kaepora"));
        QCOMPARE(listed(), QStringList{});
    }

    void existingSeedIsRefused() {
        QVERIFY(create("7"));
        const Files before = FilesIn(library);
        button("Create seed")->click();
        QVERIFY(box() != nullptr);
        QVERIFY(box()->text().contains("seed named 7"));
        QVERIFY(!shows("Creating seed…"));
        box()->close();
        QCOMPARE(FilesIn(library), before);
        QCOMPARE(listed(), QStringList{ "7" });
    }

    void deleteLeavesAnotherSeedInUse() {
        QVERIFY(create("7"));
        QVERIFY(create("8"));
        select("7");
        button("Use this seed")->click();
        const Files inUse = FilesIn(mods + "/Kaepora");

        select("8");
        button("Delete")->click();
        QVERIFY(!box()->text().contains("in use"));
        answer("Delete");
        QVERIFY(!QFileInfo::exists(library + "/8"));
        QCOMPARE(FilesIn(mods + "/Kaepora"), inUse);
        QCOMPARE(listed(), QStringList{ "7 (in use)" });
    }

    void useRefusesAFolderItDidNotMake() {
        QVERIFY(create("7"));
        Write(mods + "/Kaepora/romfs/Pack/Other.pack.zs", "other");
        const Files other = FilesIn(mods + "/Kaepora");

        button("Use this seed")->click();
        QVERIFY(box() != nullptr);
        QVERIFY(box()->text().contains("did not make"));
        box()->close();
        QCOMPARE(FilesIn(mods + "/Kaepora"), other);
        QCOMPARE(listed(), QStringList{ "7" });
    }

    void aCopyCutShortIsNotInUse() {
        QVERIFY(create("7"));
        button("Use this seed")->click();
        // What a copy leaves when the program ends half way: the first name of the file naming the seed, and some files.
        QVERIFY(QFile::rename(mods + "/Kaepora/seed.txt", mods + "/Kaepora/seed.copying"));
        const QStringList files = FilesIn(mods + "/Kaepora/romfs").keys();
        QVERIFY(QFile::remove(mods + "/Kaepora/romfs/" + files.front()));

        window = std::make_unique<Window>();
        window->show();
        QVERIFY(shows("No seed in use"));
        QCOMPARE(listed(), QStringList{ "7" });
        QVERIFY(!QFileInfo::exists(mods + "/Kaepora"));
    }

    void aSeedCutShortCanBeMadeAgain() {
        // What making a seed leaves when the program ends half way: its files, and its spoiler log cut short under the
        // name it has until it is written in full.
        Write(library + "/7/romfs/Pack/Other.pack.zs", "other");
        Write(library + "/7/spoiler.json.part", "{\"seed\": \"7\", \"sett");
        window = std::make_unique<Window>();
        window->show();
        QCOMPARE(listed(), QStringList{});

        QVERIFY(create("7"));
        QCOMPARE(listed(), QStringList{ "7" });
        QVERIFY(QFileInfo(library + "/7/spoiler.json").isFile());
        QVERIFY(!QFileInfo::exists(library + "/7/romfs/Pack/Other.pack.zs"));
        QVERIFY(!QFileInfo::exists(library + "/7/spoiler.json.part"));
    }

    void twoSeedsInARow() {
        QVERIFY(create("7"));
        QVERIFY(create("8"));
        QCOMPARE(listed(), (QStringList{ "8", "7" }));
        QTemporaryDir alone;
        for (const QString seed : { "7", "8" }) {
            QProcess make;
            make.start(qEnvironmentVariable("KAEPORA_MAKE_SEED"), { seed, game, alone.filePath(seed) });
            QVERIFY(make.waitForFinished(MakeTimeout));
            QCOMPARE(make.exitCode(), 0);
            QCOMPARE(FilesIn(library + "/" + seed), FilesIn(alone.filePath(seed)));
        }
    }
};

int main(int argc, char** argv) {
    // Qt's test mode keeps the app data and the settings under $HOME/.qttest, so HOME is a temporary folder.
    QTemporaryDir home;
    if (!home.isValid()) {
        return 1;
    }
    qputenv("HOME", home.path().toUtf8());
    QStandardPaths::setTestModeEnabled(true);
    QApplication app(argc, argv);
    QApplication::setOrganizationName("kaepora");
    QApplication::setApplicationName("kaepora");
    WindowTest test(home.path());
    return QTest::qExec(&test, argc, argv);
}

#include "window_test.moc"
