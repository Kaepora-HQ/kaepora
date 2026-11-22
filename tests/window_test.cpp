// End-to-end test of the window. It makes real seeds in this process, from the game file in KAEPORA_GAME_NUP, and
// compares them with what kaepora-make-seed, in KAEPORA_MAKE_SEED, makes in a process of its own.
#include "kaepora/window.h"
#include "version.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDirListing>
#include <QFormLayout>
#include <QGroupBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QProcess>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QSettings>
#include <QSignalSpy>
#include <QSpinBox>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslKey>
#include <QSslSocket>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeWidget>

#include <functional>
#include <memory>

using Files = QMap<QString, QByteArray>;

constexpr char LatestPath[] = "/Kaepora-HQ/kaepora/releases/latest/download/latest.json";
constexpr char ReleasePage[] = "https://github.com/Kaepora-HQ/kaepora/releases/tag/v99.0.0";

static QByteArray Latest(const QString& version, const QString& page = ReleasePage) {
    return QJsonDocument(QJsonObject{ { "version", version }, { "url", page } }).toJson();
}

static QByteArray Answer(const QByteArray& status, const QByteArray& body, const QByteArray& headers = {}) {
    return "HTTP/1.1 " + status + "\r\n" + headers + "Content-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" +
           body;
}

static QByteArray Ok(const QByteArray& body) {
    return Answer("200 OK", body);
}

// What GitHub sends Qt for a release file that does not exist.
static const QByteArray NotFound = Answer("404 Not Found", R"({"error":"Not Found"})");

// GitHub's first redirect for the newest release's file, to the same file under the release's tag.
static const QByteArray ToTheTag =
    Answer("302 Found", {}, "Location: https://github.com/Kaepora-HQ/kaepora/releases/download/v99.0.0/latest.json\r\n");

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

static QByteArray Read(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

static QJsonObject ReadJson(const QString& path) {
    return QJsonDocument::fromJson(Read(path)).object();
}

static void Write(const QString& path, const QByteArray& data) {
    QDir().mkpath(QFileInfo(path).path());
    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(data);
    }
}

// Stands in for GitHub as Qt's proxy for this process. An HTTPS request opens a tunnel to it with CONNECT, and it
// answers over TLS with a certificate for github.com that this process trusts.
class FakeGitHub : public QTcpServer {
public:
    QByteArray answer;
    int requests = 0;
    // Runs when the request for latest.json arrives, before its answer goes out.
    std::function<void()> requested;

    explicit FakeGitHub(const QString& folder) {
        // An RSA key, since Qt's Schannel backend, the one Windows uses, can serve only an RSA or a DSA key. macOS
        // trusts a server's certificate only when it names the server's use. The key is left unencrypted with -nodes,
        // since macOS's own openssl, LibreSSL, does not know OpenSSL 3's -noenc.
        QProcess openssl;
        openssl.start("openssl", { "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-subj", "/CN=github.com",
                                   "-addext", "subjectAltName=DNS:github.com", "-addext",
                                   "extendedKeyUsage=serverAuth", "-keyout", folder + "/github.key", "-out",
                                   folder + "/github.pem" });
        if (!openssl.waitForFinished() || openssl.exitCode() != 0) {
            qFatal("openssl made no certificate: %s", openssl.readAllStandardError().constData());
        }
        certificate = QSslCertificate(Read(folder + "/github.pem"));
        key = QSslKey(Read(folder + "/github.key"), QSsl::Rsa);
        listen(QHostAddress::LocalHost);
        QNetworkProxy::setApplicationProxy({ QNetworkProxy::HttpProxy, serverAddress().toString(), serverPort() });
        QSslConfiguration tls = QSslConfiguration::defaultConfiguration();
        tls.addCaCertificate(certificate);
        QSslConfiguration::setDefaultConfiguration(tls);
    }

protected:
    void incomingConnection(qintptr descriptor) override {
        auto* socket = new QSslSocket(this);
        socket->setSocketDescriptor(descriptor);
        socket->setLocalCertificate(certificate);
        socket->setPrivateKey(key);
        connect(socket, &QSslSocket::readyRead, socket, [this, socket] {
            // A request ends with an empty line.
            if (!socket->peek(socket->bytesAvailable()).contains("\r\n\r\n")) {
                return;
            }
            const QByteArray request = socket->readAll();
            const bool offline = answer.isEmpty();
            if (!socket->isEncrypted() && offline) {
                socket->disconnectFromHost();
            } else if (!socket->isEncrypted()) {
                socket->write("HTTP/1.1 200 Connection established\r\n\r\n");
                socket->startServerEncryption();
            } else {
                ++requests;
                const bool latest = request.startsWith(QByteArray("GET ") + LatestPath + ' ');
                if (latest && requested) {
                    requested();
                }
                socket->write(latest ? answer : NotFound);
            }
        });
    }

private:
    QSslCertificate certificate;
    QSslKey key;
};

class WindowTest : public QObject {
    Q_OBJECT

public:
    explicit WindowTest(const QString& home) : mods(home + "/mods"), github(home) {
        QDesktopServices::setUrlHandler("https", this, "openUrl");
    }

    ~WindowTest() override {
        QDesktopServices::unsetUrlHandler("https");
    }

    Q_INVOKABLE void openUrl(const QUrl& url) {
        opened << url;
    }

private:
    // Making a seed takes about a second and a half.
    static constexpr int MakeTimeout = 60'000;
    static constexpr int NoRequestWait = 1000;

    const QString game = qEnvironmentVariable("KAEPORA_GAME_NUP");
    const QString library = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/kaepora/seeds";
    const QString mods;
    FakeGitHub github;
    QList<QUrl> opened;
    std::unique_ptr<Window> window;

    QPushButton* button(const QString& text) const {
        for (QPushButton* button : window->findChildren<QPushButton*>()) {
            if (button->window() == window.get() && button->isVisible() && button->text() == text) {
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
        const QLabel* found = label(text);
        return found != nullptr && found->isVisible();
    }

    QStringList texts() const {
        QStringList texts;
        for (const QLabel* label : window->findChildren<QLabel*>()) {
            if (label->isVisible()) {
                texts << label->text();
            }
        }
        return texts;
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

    QStringList buttonTexts(const QMessageBox* box) const {
        QStringList texts;
        for (const QAbstractButton* button : box->buttons()) {
            texts << button->text();
        }
        texts.sort();
        return texts;
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
            if (!candidate->isReadOnly() && candidate->window() == window.get()) {
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

    QPushButton* listButton(const QString& label) const {
        for (QLabel* text : dialog()->findChildren<QLabel*>()) {
            if (text->text() == label) {
                return qobject_cast<QPushButton*>(text->buddy());
            }
        }
        return nullptr;
    }

    QPushButton* itemsToStartWith() const {
        return listButton("Items");
    }

    QDialog* openList(const QString& title) const {
        for (QDialog* dialog : window->findChildren<QDialog*>()) {
            if (dialog->isVisible() && dialog->windowTitle() == title) {
                return dialog;
            }
        }
        return nullptr;
    }

    QDialog* startWithDialog() const {
        return openList("Start with");
    }

    // Qt deletes a closed list later. Deleting it now keeps its boxes out of the settings dialog's children.
    void closeList(const QString& title, const QString& text) {
        for (QPushButton* button : openList(title)->findChildren<QPushButton*>()) {
            if (button->text() == text) {
                button->click();
                break;
            }
        }
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }

    void closeStartWith(const QString& text) {
        closeList("Start with", text);
    }

    // The choice of an open list's row.
    QComboBox* listRow(const QString& title, const QString& row) const {
        for (QLabel* text : openList(title)->findChildren<QLabel*>()) {
            if (text->text() == row) {
                return qobject_cast<QComboBox*>(text->buddy());
            }
        }
        return nullptr;
    }

    QComboBox* stepsOf(const QString& item) const {
        return listRow("Start with", item);
    }

    void startWith(const QStringList& names, const QMap<QString, QString>& steps = {}) {
        itemsToStartWith()->click();
        for (QCheckBox* box : startWithDialog()->findChildren<QCheckBox*>()) {
            box->setChecked(names.contains(box->text()));
        }
        for (const auto& [item, step] : steps.asKeyValueRange()) {
            stepsOf(item)->setCurrentText(step);
        }
        closeStartWith("OK");
    }

    void shuffleGifts(const QStringList& groups, const QString& dekuScrubs = "Vanilla") {
        listButton("Gifts")->click();
        for (QCheckBox* box : openList("Gifts")->findChildren<QCheckBox*>()) {
            box->setChecked(groups.contains(box->text()));
        }
        listRow("Gifts", "Deku Scrubs")->setCurrentText(dekuScrubs);
        closeList("Gifts", "OK");
    }

    // The labels of a group's rows, in order.
    QStringList rows(const QString& title) const {
        QStringList labels;
        for (QGroupBox* group : dialog()->findChildren<QGroupBox*>()) {
            if (group->title() == title) {
                auto* form = qobject_cast<QFormLayout*>(group->layout());
                for (int row = 0; row < form->rowCount(); ++row) {
                    labels << qobject_cast<QLabel*>(form->itemAt(row, QFormLayout::LabelRole)->widget())->text();
                }
            }
        }
        return labels;
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

    // Opens the window with "Check for new versions" as it is by default. True once its request has finished.
    bool openAndAsk(const QByteArray& answer) {
        github.answer = answer;
        QSettings().remove("checkForNewVersions");
        window = std::make_unique<Window>();
        window->show();
        QSignalSpy replies(window->findChild<QNetworkAccessManager*>(), &QNetworkAccessManager::finished);
        return replies.wait();
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
        // The window asks GitHub on every open while this box is ticked. The tests of that tick it.
        settings.setValue("checkForNewVersions", false);
        github.answer = NotFound;
        github.requests = 0;
        github.requested = nullptr;
        opened.clear();
        window = std::make_unique<Window>();
        window->show();
    }

    void cleanup() {
        window.reset();
    }

    void lichiisModsFolderIsTheDefault() {
#ifdef Q_OS_WIN
        const QDir data(qEnvironmentVariable("APPDATA"));
#else
        const QDir data(QDir::home().filePath(".local/share"));
#endif
        // A fresh Lichii has its load folder, but no folder in it for the game until a mod is put there.
        QVERIFY(QDir().mkpath(data.filePath("lichii/load")));
        const QString lichii = data.filePath("lichii/load/0400486024DEC000");
        QSettings().remove("modsFolder");
        window = std::make_unique<Window>();
        QStringList paths;
        for (QLineEdit* field : window->findChildren<QLineEdit*>()) {
            paths << field->text();
        }
        QVERIFY2(paths.contains(lichii), qPrintable(paths.join(", ")));
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
        const Files patch = FilesIn(library + "/7/exefs");
        QCOMPARE(patch.keys(), QStringList{ "E0F3CCFAF50065AE4EA01EB5AEB13B1A45DA3348.ips" });
        QCOMPARE(FilesIn(mods + "/Kaepora/exefs"), patch);
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

    void seedTextIgnoresCase() {
        QVERIFY(create("owl"));
        QCOMPARE(listed(), QStringList{ "OWL" });
        QCOMPARE(ReadJson(library + "/OWL/spoiler.json")["seed"].toString(), QString("OWL"));
        QVERIFY(!create("OWL"));
        QVERIFY(box()->text().contains("seed named OWL"));
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

    void settingsGoIntoTheSeed() {
        QVERIFY(shows("Default settings"));
        chooseForestOutKeysInChestsLensNeverNeeded();
        dialogButton("OK")->click();
        QVERIFY(dialog() == nullptr);
        const QString summary = "Forest Temple not shuffled, keys in their own chests, Lens never needed";
        const QString details = "Shuffled dungeons: All but Forest Temple\n"
                                "Small and boss keys: In their own chests\n"
                                "Maps and compasses: Anywhere in their dungeon\n"
                                "Kokiri Forest: Closed\n"
                                "Rainbow Bridge: After the Light Arrows\n"
                                "Ganon's trials: All six\n"
                                "Opening: Skipped\n"
                                "Cutscenes: Skipped\n"
                                "Start with: None\n"
                                "Shop goods: Vanilla\n"
                                "Gold Skulltulas: Vanilla\n"
                                "Gifts: Vanilla\n"
                                "Songs: Vanilla\n"
                                "Loose items: Vanilla\n"
                                "Dungeon rewards: Vanilla\n"
                                "Lens of Truth: Never needed\n"
                                "Gossip stones: Give hints\n"
                                "Hints from characters: Give hints";
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

    void vanillaStonesGoIntoTheSeed() {
        button("Settings…")->click();
        choice("Gossip stones")->setCurrentText("Vanilla");
        dialogButton("OK")->click();
        QVERIFY(create("7"));
        const QJsonObject spoiler = ReadJson(library + "/7/spoiler.json");
        QCOMPARE(spoiler["settings"].toObject()["Gossip stones"].toString(), QString("Vanilla"));
        QVERIFY(spoiler["hints"].toObject().isEmpty());
    }

    void vanillaCharactersGoIntoTheSeed() {
        button("Settings…")->click();
        choice("Hints from characters")->setCurrentText("Vanilla");
        dialogButton("OK")->click();
        QVERIFY(create("7"));
        const QJsonObject spoiler = ReadJson(library + "/7/spoiler.json");
        QCOMPARE(spoiler["settings"].toObject()["Hints from characters"].toString(), QString("Vanilla"));
        QVERIFY(spoiler["hints from characters"].toObject().isEmpty());
        QVERIFY(!spoiler["hints"].toObject().isEmpty());
    }

    void aClosedDekuForestGoesIntoTheSeed() {
        button("Settings…")->click();
        choice("Kokiri Forest")->setCurrentText("Closed Deku");
        dialogButton("OK")->click();
        QVERIFY(create("7"));
        const QJsonObject settings = ReadJson(library + "/7/spoiler.json")["settings"].toObject();
        QCOMPARE(settings["Kokiri Forest"].toString(), QString("Closed Deku"));
        QCOMPARE(listed(1), QStringList{ "Forest exit open" });
        QVERIFY(entry("7")->toolTip(1).split('\n').contains("Kokiri Forest: Closed Deku"));
    }

    void aPlayedOpeningGoesIntoTheSeed() {
        button("Settings…")->click();
        choice("Opening")->setCurrentText("Played");
        dialogButton("OK")->click();
        QVERIFY(create("7"));
        const QJsonObject settings = ReadJson(library + "/7/spoiler.json")["settings"].toObject();
        QCOMPARE(settings["Opening"].toString(), QString("Played"));
        QCOMPARE(listed(1), QStringList{ "Opening played" });
        QVERIFY(entry("7")->toolTip(1).split('\n').contains("Opening: Played"));
    }

    void playedCutscenesGoIntoTheSeed() {
        button("Settings…")->click();
        choice("Cutscenes")->setCurrentText("Played");
        dialogButton("OK")->click();
        QVERIFY(create("7"));
        const QJsonObject settings = ReadJson(library + "/7/spoiler.json")["settings"].toObject();
        QCOMPARE(settings["Cutscenes"].toString(), QString("Played"));
        QCOMPARE(listed(1), QStringList{ "Cutscenes played" });
        QVERIFY(entry("7")->toolTip(1).split('\n').contains("Cutscenes: Played"));
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
        choice("Gossip stones")->setCurrentText("Vanilla");
        choice("Hints from characters")->setCurrentText("Vanilla");
        choice("Shop goods")->setCurrentText("Four per shop shuffled");
        choice("Gold Skulltulas")->setCurrentText("All");
        shuffleGifts({ "Characters" });
        choice("Songs")->setCurrentText("Shuffled among themselves");
        choice("Loose items")->setCurrentText("Shuffled");
        choice("Dungeon rewards")->setCurrentText("Shuffled");
        choice("Opening")->setCurrentText("Played");
        choice("Cutscenes")->setCurrentText("Played");
        dialogButton("Defaults")->click();
        for (QCheckBox* box : dialog()->findChildren<QCheckBox*>()) {
            QVERIFY(box->isChecked());
        }
        for (const QString label : { "Small and boss keys", "Maps and compasses" }) {
            QCOMPARE(choice(label)->currentText(), QString("Anywhere in their dungeon"));
        }
        QCOMPARE(choice("Kokiri Forest")->currentText(), QString("Closed"));
        QCOMPARE(choice("Rainbow Bridge")->currentText(), QString("After the Light Arrows"));
        QCOMPARE(choice("Ganon's trials")->currentText(), QString("All six"));
        QCOMPARE(choice("Opening")->currentText(), QString("Skipped"));
        QCOMPARE(choice("Cutscenes")->currentText(), QString("Skipped"));
        QCOMPARE(choice("Shop goods")->currentText(), QString("Vanilla"));
        QCOMPARE(choice("Gold Skulltulas")->currentText(), QString("Vanilla"));
        QCOMPARE(listButton("Gifts")->text(), QString("Vanilla"));
        QCOMPARE(choice("Songs")->currentText(), QString("Vanilla"));
        QCOMPARE(choice("Loose items")->currentText(), QString("Vanilla"));
        QCOMPARE(choice("Dungeon rewards")->currentText(), QString("Vanilla"));
        QCOMPARE(choice("Lens of Truth")->currentText(), QString("Needed"));
        QCOMPARE(choice("Gossip stones")->currentText(), QString("Give hints"));
        QCOMPARE(choice("Hints from characters")->currentText(), QString("Give hints"));
        dialogButton("OK")->click();
        QVERIFY(shows("Default settings"));
    }

    void shopGoodsOffersEachCountUpToSevenAndARandomNumber() {
        button("Settings…")->click();
        QStringList offered;
        for (int index = 0; index < choice("Shop goods")->count(); ++index) {
            offered << choice("Shop goods")->itemText(index);
        }
        QCOMPARE(offered, (QStringList{ "Vanilla", "One per shop shuffled", "Two per shop shuffled",
                                        "Three per shop shuffled", "Four per shop shuffled", "Five per shop shuffled",
                                        "Six per shop shuffled", "Seven per shop shuffled",
                                        "A random number per shop" }));
        dialogButton("Cancel")->click();
    }

    QSpinBox* number(const QString& name) const {
        for (QSpinBox* box : dialog()->findChildren<QSpinBox*>()) {
            if (box->accessibleName() == name) {
                return box;
            }
        }
        return nullptr;
    }

    void shopPricesWaitForShopGoodsAndShowOnlyTheNumbersOfTheirChoice() {
        button("Settings…")->click();
        QStringList offered;
        for (int index = 0; index < choice("Shop prices")->count(); ++index) {
            offered << choice("Shop prices")->itemText(index);
        }
        QCOMPARE(offered, (QStringList{ "Vanilla", "Cheap", "Balanced", "One price", "Between two prices" }));
        QCOMPARE(choice("Shop prices")->currentText(), QString("Cheap"));
        const QStringList numbers{ "Shop price", "Lowest shop price", "Highest shop price" };
        for (const QString& name : numbers) {
            QCOMPARE(number(name)->minimum(), 0);
            QCOMPARE(number(name)->maximum(), 995);
            QCOMPARE(number(name)->singleStep(), 5);
        }
        const auto shown = [&] {
            QStringList on;
            for (const QString& name : numbers) {
                if (number(name)->isVisibleTo(dialog())) {
                    on << name;
                }
            }
            if (label("to")->isVisibleTo(dialog())) {
                on << "to";
            }
            return on;
        };
        const auto right = [&](const QWidget* widget) {
            QCoreApplication::processEvents();
            return widget->mapTo(dialog(), QPoint(widget->width(), 0)).x();
        };
        QVERIFY(!choice("Shop prices")->isEnabled());
        choice("Shop prices")->setCurrentText("One price");
        QCOMPARE(shown(), QStringList{});
        choice("Shop goods")->setCurrentText("Four per shop shuffled");
        QVERIFY(choice("Shop prices")->isEnabled());
        QCOMPARE(shown(), QStringList{ "Shop price" });
        choice("Shop prices")->setCurrentText("Between two prices");
        QCOMPARE(shown(), (QStringList{ "Lowest shop price", "Highest shop price", "to" }));
        choice("Shop prices")->setCurrentText("Balanced");
        QCOMPARE(shown(), QStringList{});
        // With no number, the choice spans the row as every other row's does.
        QCOMPARE(right(choice("Shop prices")), right(choice("Shop goods")));
        choice("Shop goods")->setCurrentText("Vanilla");
        QVERIFY(!choice("Shop prices")->isEnabled());
        QCOMPARE(right(choice("Shop prices")), right(choice("Shop goods")));
        dialogButton("Cancel")->click();
    }

    void shopPricesAreRememberedAndShowInTheDetails() {
        button("Settings…")->click();
        choice("Shop goods")->setCurrentText("Four per shop shuffled");
        choice("Shop prices")->setCurrentText("Between two prices");
        number("Lowest shop price")->setValue(300);
        number("Highest shop price")->setValue(150);
        number("Shop price")->setValue(45);
        dialogButton("OK")->click();
        QVERIFY(shows("Shop goods shuffled"));
        const QStringList details = label("Shop goods shuffled")->toolTip().split('\n');
        QVERIFY(details.contains("Shop prices: Between 150 and 300 rupees"));
        window = std::make_unique<Window>();
        window->show();
        button("Settings…")->click();
        QCOMPARE(choice("Shop prices")->currentText(), QString("Between two prices"));
        QCOMPARE(number("Lowest shop price")->value(), 150);
        QCOMPARE(number("Highest shop price")->value(), 300);
        // Only the prices in effect are kept, so the single price is back at its default.
        QCOMPARE(number("Shop price")->value(), 100);
        choice("Shop prices")->setCurrentText("One price");
        number("Shop price")->setValue(45);
        dialogButton("OK")->click();
        QVERIFY(label("Shop goods shuffled")->toolTip().split('\n').contains("Shop prices: One price of 45 rupees"));
    }

    void goodsBackAtVanillaLeaveNoPriceChoice() {
        button("Settings…")->click();
        choice("Shop goods")->setCurrentText("Four per shop shuffled");
        choice("Shop prices")->setCurrentText("One price");
        choice("Shop goods")->setCurrentText("Vanilla");
        dialogButton("OK")->click();
        QVERIFY(shows("Default settings"));
        window = std::make_unique<Window>();
        window->show();
        QVERIFY(shows("Default settings"));
    }

    void rememberedPricesWithVanillaGoodsStartAsTheDefaults() {
        QSettings().setValue("seedSettings", R"({"Shop goods": "Vanilla", "Shop prices": "One price", "Shop price": 45})");
        window = std::make_unique<Window>();
        window->show();
        QVERIFY(shows("Default settings"));
    }

    void theChoicesOfEachColumnLineUp() {
        button("Settings…")->click();
        QCoreApplication::processEvents();
        QMap<int, QSet<int>> columns;
        for (QComboBox* box : dialog()->findChildren<QComboBox*>()) {
            columns[box->parentWidget()->x()] << box->mapTo(dialog(), QPoint(0, 0)).x();
        }
        QCOMPARE(columns.size(), 2);
        for (const QSet<int>& lefts : std::as_const(columns)) {
            QCOMPARE(lefts.size(), 1);
        }
    }

    void theButtonsStayInAShortWindow() {
        button("Settings…")->click();
        const int height = dialog()->height() / 2;
        dialog()->resize(dialog()->width(), height);
        QCoreApplication::processEvents();
        QCOMPARE(dialog()->height(), height);
        for (const QString text : { "Defaults", "OK", "Cancel" }) {
            const QPushButton* shown = dialogButton(text);
            QVERIFY(dialog()->rect().contains(QRect(shown->mapTo(dialog(), QPoint(0, 0)), shown->size())));
        }
        QVERIFY(dialog()->findChild<QScrollArea*>()->verticalScrollBar()->isVisible());
    }

    void whatJoinsTheShuffleHasItsOwnGroup() {
        button("Settings…")->click();
        QStringList titles;
        for (QGroupBox* group : dialog()->findChildren<QGroupBox*>()) {
            titles << group->title();
        }
        QCOMPARE(titles, (QStringList{ "Dungeons", "Shuffle", "World", "Start", "Logic", "Hints" }));
        QCOMPARE(rows("Shuffle"), (QStringList{ "Shop goods", "Shop prices", "Gold Skulltulas", "Gifts", "Songs",
                                                "Loose items", "Dungeon rewards" }));
        QCOMPARE(rows("World"), (QStringList{ "Kokiri Forest", "Rainbow Bridge", "Ganon's trials" }));
        QCOMPARE(rows("Start"), (QStringList{ "Opening", "Cutscenes", "Items" }));
    }

    void theStartWithListHasTheItems() {
        button("Settings…")->click();
        QCOMPARE(itemsToStartWith()->text(), QString("None"));
        itemsToStartWith()->click();
        QCOMPARE(startWithDialog()->windowTitle(), QString("Start with"));
        QStringList names;
        for (QGroupBox* column : startWithDialog()->findChildren<QGroupBox*>()) {
            names << column->title();
            for (QCheckBox* box : column->findChildren<QCheckBox*>()) {
                QVERIFY(!box->isChecked());
                names << box->text();
            }
        }
        QCOMPARE(names,
                 (QStringList{ "Items", "Kokiri Sword", "Deku Shield", "Hylian Shield", "Mirror Shield", "Iron Boots",
                               "Hover Boots", "Goron Tunic", "Zora Tunic", "Megaton Hammer", "Boomerang" }));
        QList<QStringList> rows;
        for (QLabel* text : startWithDialog()->findChildren<QLabel*>()) {
            auto* steps = qobject_cast<QComboBox*>(text->buddy());
            QCOMPARE(steps->currentText(), QString("None"));
            rows << QStringList{ text->text() };
            for (int index = 0; index < steps->count(); ++index) {
                rows.back() << steps->itemText(index);
            }
        }
        QCOMPARE(rows, (QList<QStringList>{
                           { "Fairy Slingshot", "None", "30 seeds", "40 seeds", "50 seeds" },
                           { "Fairy Bow", "None", "30 arrows", "40 arrows", "50 arrows" },
                           { "Bomb Bag", "None", "20 bombs", "30 bombs", "40 bombs" },
                           { "Hookshot", "None", "Hookshot", "Longshot" },
                           { "Strength", "None", "Goron Bracelet", "Silver Gauntlets", "Golden Gauntlets" },
                           { "Scale", "None", "Silver Scale", "Golden Scale" },
                           { "Wallet", "None", "Adult's Wallet", "Giant's Wallet" },
                           { "Deku Sticks", "None", "10 sticks", "20 sticks", "30 sticks" },
                           { "Deku Nuts", "None", "20 nuts", "30 nuts", "40 nuts" },
                           { "Ocarina", "None", "Fairy Ocarina" },
                       }));
        QStringList buttons;
        for (QPushButton* button : startWithDialog()->findChildren<QPushButton*>()) {
            buttons << button->text();
        }
        buttons.sort();
        QCOMPARE(buttons, (QStringList{ "Cancel", "OK" }));
        closeStartWith("Cancel");
    }

    void itemsToStartWithGoIntoTheSeed() {
        button("Settings…")->click();
        startWith({ "Megaton Hammer", "Kokiri Sword", "Hover Boots", "Boomerang", "Hylian Shield" });
        QVERIFY(startWithDialog() == nullptr);
        QCOMPARE(itemsToStartWith()->text(), QString("5 items"));
        dialogButton("OK")->click();
        QVERIFY(shows("Starts with 5 items"));
        const QString line = "Start with: Kokiri Sword, Hylian Shield, Hover Boots, Megaton Hammer and Boomerang";
        QVERIFY(label("Starts with 5 items")->toolTip().split('\n').contains(line));

        QVERIFY(create("7"));
        const QJsonObject settings = ReadJson(library + "/7/spoiler.json")["settings"].toObject();
        QCOMPARE(settings["Start with"].toArray(),
                 (QJsonArray{ "Kokiri Sword", "Hylian Shield", "Hover Boots", "Megaton Hammer", "Boomerang" }));
        QCOMPARE(listed(1), QStringList{ "Starts with 5 items" });
        QVERIFY(entry("7")->toolTip(1).split('\n').contains(line));
    }

    void stepsToStartWithGoIntoTheSeed() {
        button("Settings…")->click();
        startWith({ "Kokiri Sword" }, { { "Fairy Bow", "40 arrows" }, { "Hookshot", "Longshot" } });
        QCOMPARE(itemsToStartWith()->text(), QString("3 items"));
        dialogButton("OK")->click();
        const QString line = "Start with: Kokiri Sword, Fairy Bow (40 arrows) and Longshot";
        QVERIFY(label("Starts with 3 items")->toolTip().split('\n').contains(line));

        QVERIFY(create("8"));
        const QJsonObject settings = ReadJson(library + "/8/spoiler.json")["settings"].toObject();
        QCOMPARE(settings["Start with"].toArray(), (QJsonArray{ "Kokiri Sword", "Fairy Bow (40 arrows)", "Longshot" }));
    }

    void oneItemToStartWithIsOneItem() {
        button("Settings…")->click();
        startWith({ "Megaton Hammer" });
        QCOMPARE(itemsToStartWith()->text(), QString("1 item"));
        dialogButton("OK")->click();
        QVERIFY(shows("Starts with 1 item"));
    }

    void cancelLeavesTheItemsToStartWith() {
        button("Settings…")->click();
        startWith({ "Iron Boots", "Zora Tunic" });
        itemsToStartWith()->click();
        for (QCheckBox* box : startWithDialog()->findChildren<QCheckBox*>()) {
            QCOMPARE(box->isChecked(), box->text() == "Iron Boots" || box->text() == "Zora Tunic");
            box->setChecked(false);
        }
        closeStartWith("Cancel");
        QCOMPARE(itemsToStartWith()->text(), QString("2 items"));
        dialogButton("Cancel")->click();
        QVERIFY(shows("Default settings"));
    }

    void defaultsClearsTheItemsToStartWith() {
        button("Settings…")->click();
        startWith({ "Boomerang" }, { { "Scale", "Golden Scale" } });
        dialogButton("Defaults")->click();
        QCOMPARE(itemsToStartWith()->text(), QString("None"));
        dialogButton("OK")->click();
        QVERIFY(shows("Default settings"));
    }

    void theGiftsListHasTheGroups() {
        button("Settings…")->click();
        QCOMPARE(listButton("Gifts")->text(), QString("Vanilla"));
        listButton("Gifts")->click();
        QStringList names;
        for (QGroupBox* group : openList("Gifts")->findChildren<QGroupBox*>()) {
            names << group->title();
            for (QCheckBox* box : group->findChildren<QCheckBox*>()) {
                QVERIFY(!box->isChecked());
                names << box->text();
            }
        }
        QCOMPARE(names, (QStringList{ "Gifts", "Minigames", "Characters", "House of Skulltula", "Great Fairies",
                                      "Story gifts", "Malon's Egg", "Anju's Egg" }));
        QComboBox* scrubs = listRow("Gifts", "Deku Scrubs");
        QCOMPARE(scrubs->currentText(), QString("Vanilla"));
        QStringList choices;
        for (int index = 0; index < scrubs->count(); ++index) {
            choices << scrubs->itemText(index);
        }
        QCOMPARE(choices, (QStringList{ "Vanilla", "One Time Rewards" }));
        QStringList buttons;
        for (QPushButton* button : openList("Gifts")->findChildren<QPushButton*>()) {
            buttons << button->text();
        }
        buttons.sort();
        QCOMPARE(buttons, (QStringList{ "Cancel", "OK" }));
        closeList("Gifts", "Cancel");
    }

    void giftGroupsGoIntoTheSeed() {
        button("Settings…")->click();
        shuffleGifts({ "House of Skulltula", "Minigames", "Great Fairies", "Anju's Egg", "Malon's Egg" });
        QCOMPARE(listButton("Gifts")->text(), QString("5 of 8 groups"));
        dialogButton("OK")->click();
        QVERIFY(shows("5 of 8 gift groups shuffled"));
        QVERIFY(label("5 of 8 gift groups shuffled")
                    ->toolTip()
                    .split('\n')
                    .contains("Gifts: All but Characters, Story gifts and Deku Scrubs"));

        QVERIFY(create("7"));
        const QJsonObject settings = ReadJson(library + "/7/spoiler.json")["settings"].toObject();
        QCOMPARE(settings["Gifts"].toArray(),
                 (QJsonArray{ "Minigames", "House of Skulltula", "Great Fairies", "Malon's Egg", "Anju's Egg" }));
    }

    void theDekuScrubsChoiceGoesIntoTheSeed() {
        button("Settings…")->click();
        shuffleGifts({ "Characters" }, "One Time Rewards");
        QCOMPARE(listButton("Gifts")->text(), QString("2 of 8 groups"));
        dialogButton("OK")->click();
        QVERIFY(label("2 of 8 gift groups shuffled")
                    ->toolTip()
                    .split('\n')
                    .contains("Gifts: Characters and Deku Scrubs (One Time Rewards)"));
        button("Settings…")->click();
        listButton("Gifts")->click();
        QCOMPARE(listRow("Gifts", "Deku Scrubs")->currentText(), QString("One Time Rewards"));
        closeList("Gifts", "Cancel");
        dialogButton("OK")->click();

        QVERIFY(create("7"));
        const QJsonObject settings = ReadJson(library + "/7/spoiler.json")["settings"].toObject();
        QCOMPARE(settings["Gifts"].toArray(), (QJsonArray{ "Characters", "Deku Scrubs (One Time Rewards)" }));
    }

    void everyGiftGroupShowsAsGiftsShuffled() {
        button("Settings…")->click();
        listButton("Gifts")->click();
        for (QCheckBox* box : openList("Gifts")->findChildren<QCheckBox*>()) {
            box->setChecked(true);
        }
        listRow("Gifts", "Deku Scrubs")->setCurrentText("One Time Rewards");
        closeList("Gifts", "OK");
        QCOMPARE(listButton("Gifts")->text(), QString("All groups"));
        dialogButton("OK")->click();
        QVERIFY(shows("Gifts shuffled"));
        QVERIFY(label("Gifts shuffled")->toolTip().split('\n').contains("Gifts: All"));
    }

    void theHintsChoicesShareAGroup() {
        button("Settings…")->click();
        QCOMPARE(rows("Hints"), (QStringList{ "Gossip stones", "Hints from characters" }));
        QCOMPARE(choice("Hints from characters")->currentText(), QString("Give hints"));
        QStringList choices;
        for (int index = 0; index < choice("Hints from characters")->count(); ++index) {
            choices << choice("Hints from characters")->itemText(index);
        }
        QCOMPARE(choices, (QStringList{ "Give hints", "Vanilla" }));
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

    void aChoiceShowsInTheSummary_data() {
        QTest::addColumn<QString>("setting");
        QTest::addColumn<QString>("value");
        QTest::addColumn<QString>("summary");
        QTest::newRow("keys") << "Small and boss keys" << "Not needed" << "Keys not needed";
        QTest::newRow("forest exit") << "Kokiri Forest" << "Closed Deku" << "Forest exit open";
        QTest::newRow("forest") << "Kokiri Forest" << "Open" << "Forest open";
        QTest::newRow("bridge") << "Rainbow Bridge" << "Always open" << "Bridge always open";
        QTest::newRow("trials") << "Ganon's trials" << "None" << "No trials";
        QTest::newRow("opening") << "Opening" << "Played" << "Opening played";
        QTest::newRow("cutscenes") << "Cutscenes" << "Played" << "Cutscenes played";
        QTest::newRow("shops") << "Shop goods" << "Four per shop shuffled" << "Shop goods shuffled";
        QTest::newRow("shops at random") << "Shop goods" << "A random number per shop" << "Shop goods shuffled";
        QTest::newRow("dungeon skulltulas") << "Gold Skulltulas" << "In dungeons" << "Dungeon Gold Skulltulas shuffled";
        QTest::newRow("overworld skulltulas") << "Gold Skulltulas" << "In the overworld"
                                              << "Overworld Gold Skulltulas shuffled";
        QTest::newRow("skulltulas") << "Gold Skulltulas" << "All" << "Gold Skulltulas shuffled";
        QTest::newRow("songs") << "Songs" << "Shuffled among themselves" << "Songs shuffled";
        QTest::newRow("loose items") << "Loose items" << "Shuffled" << "Loose items shuffled";
        QTest::newRow("dungeon rewards") << "Dungeon rewards" << "Shuffled" << "Dungeon rewards shuffled";
        QTest::newRow("maps") << "Maps and compasses" << "Given at the start"
                              << "Maps and compasses given at the start";
        QTest::newRow("stones") << "Gossip stones" << "Vanilla" << "No gossip stone hints";
        QTest::newRow("characters") << "Hints from characters" << "Vanilla" << "No hints from characters";
    }

    void aChoiceShowsInTheSummary() {
        QFETCH(QString, setting);
        QFETCH(QString, value);
        QFETCH(QString, summary);
        button("Settings…")->click();
        choice(setting)->setCurrentText(value);
        QCOMPARE(choice(setting)->currentText(), value);
        dialogButton("OK")->click();
        QVERIFY(shows(summary));
        QVERIFY(label(summary)->toolTip().split('\n').contains(setting + ": " + value));
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

    void aSeedMadeBeforeHintsFromCharactersHasNone() {
        // The settings of a seed made by Kaepora 0.1.0, whose log ends at the stones.
        QVERIFY(QDir().mkpath(library + "/OWL/romfs"));
        Write(library + "/OWL/spoiler.json",
              R"({"seed": "OWL", "settings": {"Shuffled dungeons": ["Deku Tree", "Dodongo's Cavern",
              "Jabu-Jabu's Belly", "Forest Temple", "Fire Temple", "Water Temple", "Shadow Temple", "Spirit Temple",
              "Bottom of the Well", "Ice Cavern", "Gerudo Training Ground", "Ganon's Castle"],
              "Small and boss keys": "Anywhere in their dungeon", "Maps and compasses": "Anywhere in their dungeon",
              "Kokiri Forest": "Closed", "Rainbow Bridge": "After the Light Arrows", "Ganon's trials": "All six",
              "Shop goods": "Vanilla", "Lens of Truth": "Needed", "Gossip stones": "Give hints"}})");
        window = std::make_unique<Window>();
        window->show();
        QCOMPARE(listed(1), QStringList{ "Opening played, cutscenes played, no hints from characters" });
        QVERIFY(entry("OWL")->toolTip(1).split('\n').contains("Hints from characters: Vanilla"));
        QVERIFY(shows("Default settings"));
    }

    void aSeedMadeBeforeTheOpeningSettingPlaysIt() {
        // The settings of a seed made by Kaepora 0.3.0 with its defaults.
        QVERIFY(QDir().mkpath(library + "/OWL/romfs"));
        Write(library + "/OWL/spoiler.json",
              R"({"seed": "OWL", "settings": {"Shuffled dungeons": ["Deku Tree", "Dodongo's Cavern",
              "Jabu-Jabu's Belly", "Forest Temple", "Fire Temple", "Water Temple", "Shadow Temple", "Spirit Temple",
              "Bottom of the Well", "Ice Cavern", "Gerudo Training Ground", "Ganon's Castle"],
              "Small and boss keys": "Anywhere in their dungeon", "Maps and compasses": "Anywhere in their dungeon",
              "Kokiri Forest": "Closed", "Rainbow Bridge": "After the Light Arrows", "Ganon's trials": "All six",
              "Start with": [], "Shop goods": "Vanilla", "Gold Skulltulas": "Vanilla", "Gifts": "Vanilla",
              "Songs": "Vanilla", "Loose items": "Vanilla", "Dungeon rewards": "Vanilla", "Lens of Truth": "Needed",
              "Gossip stones": "Give hints", "Hints from characters": "Give hints"}})");
        window = std::make_unique<Window>();
        window->show();
        QCOMPARE(listed(1), QStringList{ "Opening played, cutscenes played" });
        QVERIFY(entry("OWL")->toolTip(1).split('\n').contains("Opening: Played"));
    }

    void aSeedMadeBeforeTheCutscenesSettingPlaysThem() {
        // The settings of a seed made by Kaepora 0.4.0 with its defaults.
        QVERIFY(QDir().mkpath(library + "/OWL/romfs"));
        Write(library + "/OWL/spoiler.json",
              R"({"seed": "OWL", "settings": {"Shuffled dungeons": ["Deku Tree", "Dodongo's Cavern",
              "Jabu-Jabu's Belly", "Forest Temple", "Fire Temple", "Water Temple", "Shadow Temple", "Spirit Temple",
              "Bottom of the Well", "Ice Cavern", "Gerudo Training Ground", "Ganon's Castle"],
              "Small and boss keys": "Anywhere in their dungeon", "Maps and compasses": "Anywhere in their dungeon",
              "Kokiri Forest": "Closed", "Rainbow Bridge": "After the Light Arrows", "Ganon's trials": "All six",
              "Opening": "Skipped", "Start with": [], "Shop goods": "Vanilla", "Gold Skulltulas": "Vanilla",
              "Gifts": "Vanilla", "Songs": "Vanilla", "Loose items": "Vanilla", "Dungeon rewards": "Vanilla",
              "Lens of Truth": "Needed", "Gossip stones": "Give hints", "Hints from characters": "Give hints"}})");
        window = std::make_unique<Window>();
        window->show();
        QCOMPARE(listed(1), QStringList{ "Cutscenes played" });
        QVERIFY(entry("OWL")->toolTip(1).split('\n').contains("Cutscenes: Played"));
    }

    void aSeedMadeBeforeTheGiftGroupsWithGiftsShuffledHasEveryGroupOfItsTime() {
        // The settings of a seed made by Kaepora 0.4.0 with gifts shuffled. No release shuffled an egg or a Deku Scrub.
        QVERIFY(QDir().mkpath(library + "/OWL/romfs"));
        Write(library + "/OWL/spoiler.json",
              R"({"seed": "OWL", "settings": {"Shuffled dungeons": ["Deku Tree", "Dodongo's Cavern",
              "Jabu-Jabu's Belly", "Forest Temple", "Fire Temple", "Water Temple", "Shadow Temple", "Spirit Temple",
              "Bottom of the Well", "Ice Cavern", "Gerudo Training Ground", "Ganon's Castle"],
              "Small and boss keys": "Anywhere in their dungeon", "Maps and compasses": "Anywhere in their dungeon",
              "Kokiri Forest": "Closed", "Rainbow Bridge": "After the Light Arrows", "Ganon's trials": "All six",
              "Opening": "Skipped", "Start with": [], "Shop goods": "Vanilla", "Gold Skulltulas": "Vanilla",
              "Gifts": "Shuffled", "Songs": "Vanilla", "Loose items": "Vanilla", "Dungeon rewards": "Vanilla",
              "Lens of Truth": "Needed", "Gossip stones": "Give hints", "Hints from characters": "Give hints"}})");
        window = std::make_unique<Window>();
        window->show();
        QCOMPARE(listed(1), QStringList{ "Cutscenes played, 4 of 8 gift groups shuffled" });
        QVERIFY(entry("OWL")->toolTip(1).split('\n').contains(
            "Gifts: All but Great Fairies, Malon's Egg, Anju's Egg and Deku Scrubs"));
    }

    void aSeedWhoseSpoilerLogCannotBeReadIsListed() {
        // What an editor leaves when a letter is typed in front of the log and saved.
        QVERIFY(QDir().mkpath(library + "/OWL/romfs"));
        Write(library + "/OWL/spoiler.json", R"(d{"seed": "OWL", "settings": {}})");
        window = std::make_unique<Window>();
        window->show();
        QCOMPARE(listed(), QStringList{ "OWL" });
        QCOMPARE(listed(1), QStringList{ "Spoiler log cannot be read" });
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

    void rememberedSettingsWithGiftsShuffledTickEveryGroupOfTheirTime() {
        QSettings().setValue("seedSettings", R"({"Gifts": "Shuffled"})");
        window = std::make_unique<Window>();
        window->show();
        QVERIFY(shows("4 of 8 gift groups shuffled"));
        button("Settings…")->click();
        listButton("Gifts")->click();
        QStringList ticked;
        for (QCheckBox* box : openList("Gifts")->findChildren<QCheckBox*>()) {
            if (box->isChecked()) {
                ticked << box->text();
            }
        }
        QCOMPARE(ticked, (QStringList{ "Minigames", "Characters", "House of Skulltula", "Story gifts" }));
        QCOMPARE(listRow("Gifts", "Deku Scrubs")->currentText(), QString("Vanilla"));
        closeList("Gifts", "Cancel");
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

    void theWindowShowsTheVersionRunning() {
        QVERIFY(shows("Version " KAEPORA_VERSION));
    }

    void aNewerVersionShowsTheNotice() {
        QVERIFY(openAndAsk(Ok(Latest("99.0.0"))));
        QVERIFY(shows("Kaepora 99.0.0 is out."));
        button("Download")->click();
        QCOMPARE(opened, QList<QUrl>{ QUrl(ReleasePage) });
        QCOMPARE(github.requests, 1);
    }

    void aNewerVersionAsksInAPopup() {
        QVERIFY(openAndAsk(Ok(Latest("99.0.0"))));
        QMessageBox* popup = box();
        QVERIFY(popup != nullptr);
        QCOMPARE(popup->parent(), window.get());
        QCOMPARE(popup->text(), QString("Kaepora 99.0.0 is out. You have " KAEPORA_VERSION "."));
        QCOMPARE(buttonTexts(popup), (QStringList{ "Download", "Later" }));
    }

    void thePopupsDownloadOpensTheReleasePage() {
        QVERIFY(openAndAsk(Ok(Latest("99.0.0"))));
        QVERIFY(box() != nullptr);
        answer("Download");
        QCOMPARE(opened, QList<QUrl>{ QUrl(ReleasePage) });
        QVERIFY(box() == nullptr);
    }

    // The box comes unasked, so a key meant for the window closes it: "Later" is its default and its escape button.
    void returnOrEscapeClosesThePopupWithoutDownloading_data() {
        QTest::addColumn<int>("key");
        QTest::newRow("Return") << int(Qt::Key_Return);
        QTest::newRow("Escape") << int(Qt::Key_Escape);
    }

    void returnOrEscapeClosesThePopupWithoutDownloading() {
        QFETCH(int, key);
        QVERIFY(openAndAsk(Ok(Latest("99.0.0"))));
        QMessageBox* popup = box();
        QVERIFY(popup != nullptr);
        QPushButton* later = popup->defaultButton();
        QVERIFY(later != nullptr);
        QCOMPARE(later->text(), QString("Later"));
        QCOMPARE(popup->focusWidget(), later);
        QTest::keyClick(popup, Qt::Key(key));
        // On macOS Qt answers Escape with an animated click, which lets the button go 100 ms later.
        QTRY_VERIFY(box() == nullptr);
        QVERIFY(opened.isEmpty());
    }

    void laterLeavesTheNotice() {
        QVERIFY(openAndAsk(Ok(Latest("99.0.0"))));
        QVERIFY(box() != nullptr);
        answer("Later");
        QVERIFY(box() == nullptr);
        QVERIFY(opened.isEmpty());
        QVERIFY(shows("Kaepora 99.0.0 is out."));
        button("Download")->click();
        QCOMPARE(opened, QList<QUrl>{ QUrl(ReleasePage) });
    }

    void thePopupComesOncePerOpen() {
        QVERIFY(openAndAsk(Ok(Latest("99.0.0"))));
        QVERIFY(box() != nullptr);
        answer("Later");
        auto* check = window->findChild<QCheckBox*>();
        check->click();
        QSignalSpy replies(window->findChild<QNetworkAccessManager*>(), &QNetworkAccessManager::finished);
        check->click();
        QVERIFY(replies.wait());
        QCOMPARE(github.requests, 2);
        QVERIFY(box() == nullptr);
    }

    void aFailedRequestOffersTheNewerVersionSaved_data() {
        QTest::addColumn<QByteArray>("answer");
        QTest::newRow("offline") << QByteArray();
        QTest::newRow("no release yet") << NotFound;
    }

    void aFailedRequestOffersTheNewerVersionSaved() {
        QFETCH(QByteArray, answer);
        QSettings().setValue("latestRelease", QString::fromUtf8(Latest("99.0.0")));
        QVERIFY(openAndAsk(answer));
        QVERIFY(box() != nullptr);
        QCOMPARE(box()->text(), QString("Kaepora 99.0.0 is out. You have " KAEPORA_VERSION "."));
    }

    void anUntickedBoxAsksNothingUntilTicked() {
        QSettings settings;
        settings.setValue("checkForNewVersions", false);
        settings.setValue("latestRelease", QString::fromUtf8(Latest("99.0.0")));
        github.answer = Ok(Latest("99.0.0"));
        window = std::make_unique<Window>();
        window->show();
        QTest::qWait(NoRequestWait);
        QCOMPARE(github.requests, 0);
        QVERIFY(box() == nullptr);
        QVERIFY(button("Download") == nullptr);

        QSignalSpy replies(window->findChild<QNetworkAccessManager*>(), &QNetworkAccessManager::finished);
        window->findChild<QCheckBox*>()->click();
        QVERIFY(replies.wait());
        QCOMPARE(github.requests, 1);
        QVERIFY(box() != nullptr);
    }

    void quietOutcomesShowNothing_data() {
        QTest::addColumn<QByteArray>("answer");
        QTest::newRow("offline") << QByteArray();
        QTest::newRow("no release yet") << NotFound;
        QTest::newRow("not JSON") << Ok("<!DOCTYPE html>");
        QTest::newRow("a version that is not a number") << Ok(Latest("v99.0.0"));
        QTest::newRow("a page on another site")
            << Ok(Latest("99.0.0", "https://example.com/Kaepora-HQ/kaepora/releases/tag/v99.0.0"));
        QTest::newRow("a page of another repository")
            << Ok(Latest("99.0.0", "https://github.com/someone/kaepora/releases/tag/v99.0.0"));
        QTest::newRow("a path out of the releases")
            << Ok(Latest("99.0.0", "https://github.com/Kaepora-HQ/kaepora/releases/tag/../../../someone/kaepora"));
        QTest::newRow("the version running") << Ok(Latest(KAEPORA_VERSION));
        QTest::newRow("an older version") << Ok(Latest("0.0.1"));
    }

    void quietOutcomesShowNothing() {
        QFETCH(QByteArray, answer);
        const QStringList quiet = texts();
        QVERIFY(openAndAsk(answer));
        QCOMPARE(texts(), quiet);
        QVERIFY(button("Download") == nullptr);
        QVERIFY(box() == nullptr);
    }

    void theBoxTurnsTheCheckOff() {
        QVERIFY(openAndAsk(Ok(Latest("99.0.0"))));
        auto* check = window->findChild<QCheckBox*>();
        QCOMPARE(check->text(), QString("Check for new versions"));
        QVERIFY(check->isChecked());
        check->click();
        QVERIFY(!shows("Kaepora 99.0.0 is out."));
        QVERIFY(button("Download") == nullptr);

        window = std::make_unique<Window>();
        window->show();
        QVERIFY(!window->findChild<QCheckBox*>()->isChecked());
        QVERIFY(button("Download") == nullptr);
        QTest::qWait(NoRequestWait);
        QCOMPARE(github.requests, 1);
    }

    void everyOpenAsks() {
        QVERIFY(openAndAsk(NotFound));
        QVERIFY(openAndAsk(NotFound));
        QCOMPARE(github.requests, 2);
    }

    void aNewerVersionIsRememberedUntilItRuns() {
        QVERIFY(openAndAsk(Ok(Latest("99.0.0"))));
        github.answer = NotFound;
        window = std::make_unique<Window>();
        window->show();
        QVERIFY(shows("Kaepora 99.0.0 is out."));

        QSettings().setValue("latestRelease", QString::fromUtf8(Latest(KAEPORA_VERSION)));
        window = std::make_unique<Window>();
        window->show();
        QVERIFY(button("Download") == nullptr);
    }

    void untickingStopsARequestInFlight() {
        github.requested = [this] {
            // The offscreen platform warns when a window shows. From the request on, nothing may.
            QTest::failOnWarning();
            window->findChild<QCheckBox*>()->click();
        };
        QVERIFY(openAndAsk(ToTheTag));
        QTest::qWait(NoRequestWait);
        QCOMPARE(github.requests, 1);
    }

    // On macOS the emulator is an app, a folder, and Play starts the program its Info.plist names.
    void playStartsTheProgramLichiisAppNames() {
#ifdef Q_OS_MACOS
        const QString app = QDir::home().filePath("Applications/Lichii.app");
        const QString program = app + "/Contents/MacOS/lichii-program";
        const QString arguments = QDir::home().filePath("arguments");
        Write(app + "/Contents/Info.plist", R"(<?xml version="1.0" encoding="UTF-8"?>
<plist version="1.0"><dict><key>CFBundleExecutable</key><string>lichii-program</string>
<key>CFBundlePackageType</key><string>APPL</string></dict></plist>
)");
        Write(program, "#!/bin/sh\nprintf '%s\\n' \"$@\" > '" + arguments.toUtf8() + "'\n");
        QVERIFY(QFile::setPermissions(program, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        const QString game = QDir::home().filePath("game.nup");
        Write(game, {});
        Write(mods + "/Kaepora/seed.txt", "7");
        QSettings().setValue("emulator", app);
        QSettings().setValue("gameFile", game);
        window = std::make_unique<Window>();
        window->show();
        QVERIFY(button("Play")->isEnabled());
        button("Play")->click();
        QTRY_COMPARE(QString::fromUtf8(Read(arguments)), "-g\n" + game + "\n");
#else
        QSKIP("The emulator is an app only on macOS");
#endif
    }
};

int main(int argc, char** argv) {
    // Qt's test mode keeps the app data and the settings under $HOME/.qttest, so HOME is a temporary folder.
    QTemporaryDir home;
    if (!home.isValid()) {
        return 1;
    }
    qputenv("HOME", home.path().toUtf8());
    // On macOS Qt asks the system for the user's folders, and the system reads its home from CFFIXED_USER_HOME.
    qputenv("CFFIXED_USER_HOME", home.path().toUtf8());
    // Qt's Secure Transport backend, the one macOS uses, imports the fake server's key into the default keychain, and
    // this HOME has none. With this set, it makes a keychain of its own in the temporary folder.
    qputenv("QT_SSL_USE_TEMPORARY_KEYCHAIN", "1");
    QStandardPaths::setTestModeEnabled(true);
    // Test mode does not move the Windows registry, so the settings go to a file in the temporary folder. Lichii's data
    // is looked for there too: in APPDATA on Windows, and under HOME elsewhere.
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, home.path());
    qputenv("APPDATA", home.path().toUtf8());
    qunsetenv("XDG_DATA_HOME");
    QApplication app(argc, argv);
    QApplication::setOrganizationName("kaepora");
    QApplication::setApplicationName("kaepora");
    WindowTest test(home.path());
    return QTest::qExec(&test, argc, argv);
}

#include "window_test.moc"
