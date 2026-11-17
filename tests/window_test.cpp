// End-to-end test of the window. It makes real seeds in this process, from the game file in KAEPORA_GAME_NUP, and
// compares them with what kaepora-make-seed, in KAEPORA_MAKE_SEED, makes in a process of its own.
#include "kaepora/window.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDirListing>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeWidget>

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

static QJsonObject ReadJson(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object() : QJsonObject();
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

    QTreeWidget* list() const {
        return window->findChild<QTreeWidget*>();
    }

    QStringList listed(int column = 0) const {
        QStringList texts;
        for (int row = 0; row < list()->topLevelItemCount(); ++row) {
            texts << list()->topLevelItem(row)->text(column);
        }
        return texts;
    }

    bool shows(const QString& text) const {
        return label(text) != nullptr;
    }

    QLabel* label(const QString& text) const {
        for (QLabel* label : window->findChildren<QLabel*>()) {
            if (label->text() == text) {
                return label;
            }
        }
        return nullptr;
    }

    QTreeWidgetItem* entry(const QString& seed) const {
        for (int row = 0; row < list()->topLevelItemCount(); ++row) {
            if (list()->topLevelItem(row)->text(0).split(' ').front() == seed) {
                return list()->topLevelItem(row);
            }
        }
        return nullptr;
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

    QDialog* dialog() const {
        for (QDialog* dialog : window->findChildren<QDialog*>()) {
            if (dialog->isVisible() && dialog->windowTitle() == "Seed settings") {
                return dialog;
            }
        }
        return nullptr;
    }

    QPushButton* dialogButton(const QString& text) const {
        for (QPushButton* button : dialog()->findChildren<QPushButton*>()) {
            if (button->text() == text) {
                return button;
            }
        }
        return nullptr;
    }

    QCheckBox* dungeon(const QString& name) const {
        for (QCheckBox* box : dialog()->findChildren<QCheckBox*>()) {
            if (box->text() == name) {
                return box;
            }
        }
        return nullptr;
    }

    QComboBox* choice(const QString& label) const {
        for (QLabel* text : dialog()->findChildren<QLabel*>()) {
            if (text->text() == label) {
                return qobject_cast<QComboBox*>(text->buddy());
            }
        }
        return nullptr;
    }

    void chooseForestOutKeysInChestsLensNeverNeeded() {
        button("Settings…")->click();
        QVERIFY(dialog() != nullptr);
        dungeon("Forest Temple")->click();
        choice("Small and boss keys")->setCurrentText("In their own chests");
        choice("Lens of Truth")->setCurrentText("Never needed");
    }

    void answer(const QString& text) {
        for (QAbstractButton* answer : box()->buttons()) {
            if (answer->text() == text) {
                answer->click();
            }
        }
    }

    void select(const QString& seed) {
        list()->setCurrentItem(entry(seed));
    }

    void leaveOut(const QStringList& dungeons) {
        button("Settings…")->click();
        for (const QString& name : dungeons) {
            dungeon(name)->setChecked(false);
        }
        dialogButton("OK")->click();
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
        QCOMPARE(list()->currentItem()->text(0), QString("7"));
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
        // What making a seed leaves when the program ends half way: a folder with no spoiler log.
        Write(library + "/7/romfs/Pack/Other.pack.zs", "other");
        window = std::make_unique<Window>();
        window->show();
        QCOMPARE(listed(), QStringList{});

        QVERIFY(create("7"));
        QCOMPARE(listed(), QStringList{ "7" });
        QVERIFY(QFileInfo(library + "/7/spoiler.json").isFile());
        QVERIFY(!QFileInfo::exists(library + "/7/romfs/Pack/Other.pack.zs"));
    }

    void settingsGoIntoTheSeed() {
        QVERIFY(shows("Default settings"));
        chooseForestOutKeysInChestsLensNeverNeeded();
        dialogButton("OK")->click();
        QVERIFY(dialog() == nullptr);
        const QString summary = "Forest Temple not shuffled, keys in their own chests, Lens never needed";
        const QString details = "Shuffled dungeons: All but Forest Temple\n"
                                "Small and boss keys: In their own chests\n"
                                "Maps and compasses: Anywhere in their dungeon\n"
                                "Lens of Truth: Never needed";
        QVERIFY(shows(summary));
        QCOMPARE(label(summary)->toolTip(), details);

        QVERIFY(create("7"));
        const QJsonObject settings = ReadJson(library + "/7/spoiler.json")["settings"].toObject();
        QCOMPARE(settings["Small and boss keys"].toString(), QString("In their own chests"));
        QCOMPARE(settings["Maps and compasses"].toString(), QString("Anywhere in their dungeon"));
        QCOMPARE(settings["Lens of Truth"].toString(), QString("Never needed"));
        QCOMPARE(settings["Shuffled dungeons"].toArray().size(), 11);
        QVERIFY(!settings["Shuffled dungeons"].toArray().contains(QString("Forest Temple")));
        QCOMPARE(listed(1), QStringList{ summary });
        QCOMPARE(entry("7")->toolTip(1), details);
    }

    void cancelChangesNothing() {
        chooseForestOutKeysInChestsLensNeverNeeded();
        dialogButton("Cancel")->click();
        QVERIFY(dialog() == nullptr);
        QVERIFY(shows("Default settings"));

        button("Settings…")->click();
        QVERIFY(dungeon("Forest Temple")->isChecked());
        QCOMPARE(choice("Small and boss keys")->currentText(), QString("Anywhere in their dungeon"));
        QCOMPARE(choice("Lens of Truth")->currentText(), QString("Needed"));
        dialogButton("Cancel")->click();
        QVERIFY(create("7"));
        QCOMPARE(listed(1), QStringList{ "Default settings" });
    }

    void defaultsSetsEveryControlBack() {
        chooseForestOutKeysInChestsLensNeverNeeded();
        choice("Maps and compasses")->setCurrentText("In their own chests");
        dialogButton("Defaults")->click();
        for (QCheckBox* box : dialog()->findChildren<QCheckBox*>()) {
            QVERIFY(box->isChecked());
        }
        for (const QString label : { "Small and boss keys", "Maps and compasses" }) {
            QCOMPARE(choice(label)->currentText(), QString("Anywhere in their dungeon"));
        }
        QCOMPARE(choice("Lens of Truth")->currentText(), QString("Needed"));
        dialogButton("OK")->click();
        QVERIFY(shows("Default settings"));
    }

    void theSummaryNamesUpToThreeDungeonsLeftOut() {
        leaveOut({ "Water Temple" });
        QVERIFY(shows("Water Temple not shuffled"));
        leaveOut({ "Fire Temple" });
        QVERIFY(shows("Fire Temple and Water Temple not shuffled"));
        leaveOut({ "Shadow Temple" });
        QVERIFY(shows("Fire Temple, Water Temple and Shadow Temple not shuffled"));
        leaveOut({ "Deku Tree" });
        QVERIFY(shows("8 of 12 dungeons shuffled"));
    }

    void longSettingsLeaveTheWindowWidth() {
        const int least = window->minimumSizeHint().width();
        const int width = window->width();
        chooseForestOutKeysInChestsLensNeverNeeded();
        choice("Maps and compasses")->setCurrentText("In their own chests");
        dialogButton("OK")->click();
        QVERIFY(shows("Forest Temple not shuffled, keys in their own chests, maps and compasses in their own chests, "
                      "Lens never needed"));
        // The layout takes the new text into account once the window's events run.
        QCoreApplication::processEvents();
        QCOMPARE(window->minimumSizeHint().width(), least);
        QCOMPARE(window->width(), width);
    }

    void settingsAreRemembered() {
        chooseForestOutKeysInChestsLensNeverNeeded();
        dialogButton("OK")->click();
        window = std::make_unique<Window>();
        window->show();
        QVERIFY(shows("Forest Temple not shuffled, keys in their own chests, Lens never needed"));
        button("Settings…")->click();
        QVERIFY(!dungeon("Forest Temple")->isChecked());
        QCOMPARE(choice("Small and boss keys")->currentText(), QString("In their own chests"));
        QCOMPARE(choice("Maps and compasses")->currentText(), QString("Anywhere in their dungeon"));
        QCOMPARE(choice("Lens of Truth")->currentText(), QString("Never needed"));
    }

    void aLogThisVersionCannotReadListsAsAnotherVersion() {
        QFile soh(qEnvironmentVariable("KAEPORA_SOH_LOG"));
        QVERIFY(soh.open(QIODevice::ReadOnly));
        const QByteArray sohLog = soh.readAll();
        QVERIFY(create("7"));
        QJsonObject later = ReadJson(library + "/7/spoiler.json");
        QJsonObject laterSettings = later["settings"].toObject();
        laterSettings["Forest"] = "Open";
        later["settings"] = laterSettings;
        const QMap<QString, QByteArray> logs = { { "SoH", sohLog },
                                                 { "Later", QJsonDocument(later).toJson() },
                                                 { "Empty", "" },
                                                 { "Short", sohLog.left(1024) } };
        for (const auto& [seed, log] : logs.asKeyValueRange()) {
            Write(library + "/" + seed + "/romfs/Pack/Other.pack.zs", seed.toUtf8());
            Write(library + "/" + seed + "/spoiler.json", log);
        }

        window = std::make_unique<Window>();
        window->show();
        QVERIFY(window->isVisible());
        for (const QString& seed : logs.keys()) {
            QVERIFY2(entry(seed) != nullptr, qPrintable(seed));
            QCOMPARE(entry(seed)->text(1), QString("Made by another version"));
        }
        QCOMPARE(entry("7")->text(1), QString("Default settings"));

        select("SoH");
        button("Use this seed")->click();
        QCOMPARE(FilesIn(mods + "/Kaepora/romfs"), FilesIn(library + "/SoH/romfs"));
        QCOMPARE(entry("SoH")->text(0), QString("SoH (in use)"));
        button("Delete")->click();
        answer("Delete");
        QVERIFY(entry("SoH") == nullptr);
        QVERIFY(!QFileInfo::exists(library + "/SoH"));
    }

    void unreadableRememberedSettingsStartAsTheDefaults() {
        for (const QString saved : { "Lens never needed", "{\"Forest\": \"Open\"}" }) {
            QSettings().setValue("seedSettings", saved);
            window = std::make_unique<Window>();
            window->show();
            QVERIFY2(shows("Default settings"), qPrintable(saved));
        }
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
