#include "window.h"

#include "mod.h"
#include "new_version.h"
#include "seed.h"
#include "settings_dialog.h"

#include <QDesktopServices>
#include <QDir>
#include <QDirListing>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QProcess>
#include <QPushButton>
#include <QRandomGenerator>
#include <QRegularExpressionValidator>
#include <QSettings>
#include <QStandardPaths>
#include <QStyle>
#include <QThread>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

#ifdef Q_OS_MACOS
#include <CoreFoundation/CoreFoundation.h>
#endif

#include <algorithm>
#include <exception>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <system_error>

// SoH's seed field takes letters and digits. A seed names its folder in the library, and a folder name holds at most
// 255 bytes.
constexpr char SeedPattern[] = "[A-Za-z0-9]{1,255}";

// The folder in the mods folder that holds the seed in use, and the file beside its romfs that names the seed. The
// file has its first name while the seed is copied in, so a copy that was cut short never counts as in use.
constexpr char ModName[] = "Kaepora";
constexpr char CopyingFile[] = "seed.copying";
constexpr char SeedFile[] = "seed.txt";

// Where Lichii looks for mods, under its data folder, and the folder there that holds this game's.
constexpr char LichiiLoad[] = "lichii/load";
constexpr char GameId[] = "0400486024DEC000";

constexpr char SettingsKey[] = "seedSettings";

static bool IsSeed(const QString& text) {
    static const QRegularExpression pattern(QRegularExpression::anchoredPattern(SeedPattern));
    return pattern.match(text).hasMatch();
}

// Lichii keeps its data in %APPDATA% on Windows. Elsewhere, macOS included, it keeps it in XDG_DATA_HOME when that is an
// absolute path, and in ~/.local/share otherwise. The game's folder in its load folder can be missing until a mod is put
// there, and putting a seed in use makes it.
static QString LichiiModsFolder() {
#ifdef Q_OS_WIN
    const QString data = qEnvironmentVariable("APPDATA");
#else
    const QString xdg = qEnvironmentVariable("XDG_DATA_HOME");
    const QString data = QDir::isAbsolutePath(xdg) ? xdg : QDir::home().filePath(".local/share");
#endif
    const QString load = QDir(data).filePath(LichiiLoad);
    return QFileInfo(load).isDir() ? QDir(load).filePath(GameId) : QString();
}

#ifdef Q_OS_MACOS
// On macOS the emulator is an app, which is a folder, and the program it starts is the one its Info.plist names.
bool (QFileInfo::*const IsEmulator)() const = &QFileInfo::isBundle;

// Empty when the app is gone.
static QString Program(const QString& app) {
    QString program;
    const CFURLRef url = QUrl::fromLocalFile(app).toCFURL();
    if (const CFBundleRef bundle = CFBundleCreate(kCFAllocatorDefault, url)) {
        if (const CFURLRef executable = CFBundleCopyExecutableURL(bundle)) {
            // The program's URL is relative to the app's folder of programs.
            const CFURLRef absolute = CFURLCopyAbsoluteURL(executable);
            program = QUrl::fromCFURL(absolute).toLocalFile();
            CFRelease(absolute);
            CFRelease(executable);
        }
        CFRelease(bundle);
    }
    CFRelease(url);
    return program;
}
#else
bool (QFileInfo::*const IsEmulator)() const = &QFileInfo::isFile;

static QString Program(const QString& emulator) {
    return emulator;
}
#endif

static std::filesystem::path Path(const QString& path) {
    return QFileInfo(path).filesystemFilePath();
}

// MakeSeed writes the spoiler log last, so a folder without one is not a whole seed.
static bool IsWholeSeed(const QFileInfo& folder) {
    return IsSeed(folder.fileName()) && QFileInfo(folder.filePath() + "/romfs").isDir() &&
           QFileInfo(folder.filePath() + "/spoiler.json").isFile();
}

// The seed's folder in the library, as the list found it.
static QString FolderOf(const QTreeWidgetItem* item) {
    return item->data(0, Qt::UserRole).toString();
}

static QString SeedOf(const QTreeWidgetItem* item) {
    return QFileInfo(FolderOf(item)).fileName();
}

// A seed's settings as its spoiler log holds them. A log that leaves out hints from characters is of a seed that has
// none, and one that leaves out the opening is of a seed whose mod plays it.
static SeedSettings SettingsOf(const QString& folder) {
    const nlohmann::json logged = nlohmann::json::parse(std::ifstream(Path(folder + "/spoiler.json"))).at("settings");
    SeedSettings settings = ParseSeedSettings(logged);
    if (!logged.contains(std::string(HintsFromCharactersSetting))) {
        settings.hintsFromCharacters = HintsFromCharacters::Vanilla;
    }
    if (!logged.contains(std::string(OpeningSetting))) {
        settings.opening = Opening::Played;
    }
    return settings;
}

// The settings the window remembers between runs, or the defaults when there are none it can read.
static SeedSettings RememberedSettings() {
    try {
        return ParseSeedSettings(nlohmann::json::parse(QSettings().value(SettingsKey).toByteArray()));
    } catch (const std::exception&) {
        return {};
    }
}

namespace {
// Seed text ignores case, so the field shows it as the generator reads it, in upper case.
class UpperCaseSeedValidator : public QRegularExpressionValidator {
public:
    using QRegularExpressionValidator::QRegularExpressionValidator;

    State validate(QString& input, int& pos) const override {
        for (QChar& c : input) {
            if (c >= u'a' && c <= u'z') {
                c = c.toUpper();
            }
        }
        return QRegularExpressionValidator::validate(input, pos);
    }
};

class ElidedLabel : public QLabel {
public:
    ElidedLabel() {
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        const QRect area = contentsRect();
        style()->drawItemText(&painter, area, alignment(), palette(), isEnabled(),
                              fontMetrics().elidedText(text(), Qt::ElideRight, area.width()), foregroundRole());
    }
};
}

// Message boxes open window-modal and return at once, so the window's events go on while one is open.
static void Tell(QWidget* window, QMessageBox::Icon icon, const QString& text) {
    auto* box = new QMessageBox(icon, "Kaepora", text, QMessageBox::Ok, window);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->open();
}

static void Ask(QWidget* window, const QString& question, const QString& action, const std::function<void()>& then) {
    auto* box = new QMessageBox(QMessageBox::Question, "Kaepora", question, QMessageBox::Cancel, window);
    box->setAttribute(Qt::WA_DeleteOnClose);
    QObject::connect(box->addButton(action, QMessageBox::AcceptRole), &QPushButton::clicked, window, then);
    box->open();
}

Window::Window() : library(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/kaepora/seeds") {
    setWindowTitle("Kaepora");
    auto* form = new QFormLayout;

    gameFile = addPathRow(form, "Game file", "gameFile", &QFileInfo::isFile, [this] {
        return QFileDialog::getOpenFileName(this, "Choose the game file", {}, "Game file (*.nup)");
    });
    modsFolder = addPathRow(form, "Mods folder", "modsFolder", &QFileInfo::isDir,
                            [this] { return QFileDialog::getExistingDirectory(this, "Choose the mods folder"); });
    if (modsFolder->text().isEmpty()) {
        modsFolder->setText(LichiiModsFolder());
    }
    emulator = addPathRow(form, "Emulator (optional)", "emulator", IsEmulator,
                          [this] { return QFileDialog::getOpenFileName(this, "Choose the emulator"); });

    seedField = new QLineEdit;
    seedField->setValidator(new UpperCaseSeedValidator(QRegularExpression(SeedPattern), seedField));
    auto* random = new QPushButton("Random");
    auto* settingsButton = new QPushButton("Settings…");
    createButton = new QPushButton("Create seed");
    auto* seedRow = new QHBoxLayout;
    seedRow->addWidget(seedField);
    seedRow->addWidget(random);
    seedRow->addWidget(settingsButton);
    seedRow->addWidget(createButton);
    form->addRow("Seed", seedRow);
    newSeedSettings = RememberedSettings();
    settingsSummary = new ElidedLabel;
    showSettings();
    form->addRow("Settings", settingsSummary);

    seeds = new QTreeWidget;
    seeds->setColumnCount(2);
    seeds->setHeaderHidden(true);
    seeds->setRootIsDecorated(false);
    useButton = new QPushButton("Use this seed");
    spoilerButton = new QPushButton("Open spoiler log");
    deleteButton = new QPushButton("Delete");
    playButton = new QPushButton("Play");
    auto* seedButtons = new QVBoxLayout;
    for (QPushButton* button : { useButton, spoilerButton, deleteButton, playButton }) {
        seedButtons->addWidget(button);
    }
    seedButtons->addStretch();
    auto* seedList = new QHBoxLayout;
    seedList->addWidget(seeds);
    seedList->addLayout(seedButtons);
    form->addRow("Seeds", seedList);

    status = new QLabel;
    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(status);
    layout->addWidget(NewVersionRow());

    connect(seedField, &QLineEdit::textChanged, this, &Window::updateButtons);
    connect(random, &QPushButton::clicked, this, [this] {
        QString seed;
        for (int i = 0; i < 10; ++i) {
            seed += QChar('0' + QRandomGenerator::global()->bounded(10));
        }
        seedField->setText(seed);
    });
    connect(settingsButton, &QPushButton::clicked, this, &Window::chooseSettings);
    connect(createButton, &QPushButton::clicked, this, &Window::createSeed);
    connect(seeds, &QTreeWidget::currentItemChanged, this, &Window::updateButtons);
    connect(useButton, &QPushButton::clicked, this, &Window::useSeed);
    connect(spoilerButton, &QPushButton::clicked, this, &Window::openSpoilerLog);
    connect(deleteButton, &QPushButton::clicked, this, &Window::deleteSeed);
    connect(playButton, &QPushButton::clicked, this, &Window::play);
    refresh();
}

// The thread writes into the library, so the window waits for it rather than leave half a seed behind.
Window::~Window() {
    if (making != nullptr) {
        making->wait();
    }
}

// A row with a read-only path, from the settings under the key, and a button that asks for another with the dialog
// the function opens. A path that is not what valid asks for counts as none.
QLineEdit* Window::addPathRow(QFormLayout* form, const QString& label, const QString& key,
                              bool (QFileInfo::*valid)() const, const std::function<QString()>& choose) {
    auto* field = new QLineEdit;
    field->setReadOnly(true);
    if (const QString saved = QSettings().value(key).toString(); (QFileInfo(saved).*valid)()) {
        field->setText(saved);
    }
    auto* button = new QPushButton("Choose…");
    connect(button, &QPushButton::clicked, this, [this, key, valid, choose, field] {
        if (const QString path = choose(); (QFileInfo(path).*valid)()) {
            QSettings().setValue(key, path);
            field->setText(path);
            refresh();
        }
    });
    auto* row = new QHBoxLayout;
    row->addWidget(field);
    row->addWidget(button);
    form->addRow(label, row);
    return field;
}

void Window::chooseSettings() {
    ChooseSettings(this, newSeedSettings, [this](const SeedSettings& chosen) {
        newSeedSettings = chosen;
        QSettings().setValue(SettingsKey, QString::fromStdString(ToJson(newSeedSettings).dump()));
        showSettings();
    });
}

void Window::showSettings() {
    settingsSummary->setText(Summary(newSeedSettings));
    settingsSummary->setToolTip(Details(newSeedSettings));
}

void Window::createSeed() {
    const QString seed = seedField->text();
    const std::filesystem::path nup = Path(gameFile->text());
    const QFileInfo inLibrary(QDir(library).filePath(seed));
    if (IsWholeSeed(inLibrary)) {
        Tell(this, QMessageBox::Information,
             QString("A seed named %1 is already in the list. Type another seed, or use that one.").arg(seed));
        return;
    }
    const std::filesystem::path folder = inLibrary.filesystemFilePath();
    // What is left here is part of a seed that was cut short while this program made it, in its own library.
    std::error_code ignored;
    std::filesystem::remove_all(folder, ignored);
    auto error = std::make_shared<QString>();
    making = QThread::create([seed, settings = newSeedSettings, nup, folder, error] {
        try {
            MakeSeed(seed.toStdString(), settings, nup, folder);
        } catch (const std::exception& e) {
            *error = QString::fromUtf8(e.what());
        }
    });
    making->setParent(this);
    connect(making, &QThread::finished, this, [this, seed, error] {
        making->deleteLater();
        making = nullptr;
        if (!error->isEmpty()) {
            Tell(this, QMessageBox::Critical, "The seed could not be created.\n\n" + *error);
        }
        refresh(error->isEmpty() ? seed : QString());
    });
    making->start();
    refresh();
}

void Window::useSeed() {
    const QString seed = SeedOf(seeds->currentItem());
    const QString folder = FolderOf(seeds->currentItem());
    const QDir mods(modsFolder->text());
    const QString kaepora = mods.filePath(ModName);
    if (QFileInfo::exists(kaepora) && seedNamedBy(SeedFile).isEmpty() && seedNamedBy(CopyingFile).isEmpty()) {
        Tell(this, QMessageBox::Warning,
             QString("The mods folder holds a folder named %1 that this program did not make. Move it out of the "
                     "mods folder, then use the seed again.")
                 .arg(ModName));
        return;
    }
    const std::filesystem::path target = Path(kaepora);
    try {
        // The folder is gone or holds the file naming a seed, checked above, so this program made it.
        std::filesystem::remove_all(target);
        WriteFile(target / CopyingFile, seed.toStdString());
        for (const char* part : { "romfs", "exefs" }) {
            std::filesystem::copy(Path(folder + "/" + part), target / part, std::filesystem::copy_options::recursive);
        }
        std::filesystem::rename(target / CopyingFile, target / SeedFile);
    } catch (const std::exception& e) {
        // Part of a seed would change some of its files and not others. The folder is this call's.
        std::error_code ignored;
        std::filesystem::remove_all(target, ignored);
        Tell(this, QMessageBox::Critical,
             "The seed could not be put in the mods folder.\n\n" + QString::fromUtf8(e.what()));
        refresh();
        return;
    }
    refresh();

    // Each folder in the mods folder replaces the game's files it holds, so two that hold the same file conflict.
    const QDir romfs(folder + "/romfs");
    QStringList files;
    for (const auto& file : QDirListing(romfs.path(), QDirListing::IteratorFlag::FilesOnly |
                                                          QDirListing::IteratorFlag::Recursive)) {
        files << romfs.relativeFilePath(file.filePath());
    }
    QStringList others;
    for (const QFileInfo& mod : mods.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        if (mod.fileName() != ModName && std::ranges::any_of(files, [&](const QString& file) {
                return QFileInfo::exists(mod.filePath() + "/romfs/" + file);
            })) {
            others << mod.fileName();
        }
    }
    if (!others.isEmpty()) {
        Tell(this, QMessageBox::Warning,
             "These folders in the mods folder change the same files as the seed:\n\n" + others.join('\n') +
                 "\n\nThe game may load their files in place of the seed's. Move them out of the mods folder before "
                 "you play.");
    }
}

void Window::openSpoilerLog() {
    const QString log = FolderOf(seeds->currentItem()) + "/spoiler.json";
    Ask(this, "The spoiler log shows where every shuffled item is. Open it?", "Open spoiler log", [this, log] {
        if (!QDesktopServices::openUrl(QUrl::fromLocalFile(log))) {
            Tell(this, QMessageBox::Warning, "No program opened the spoiler log.");
        }
    });
}

void Window::deleteSeed() {
    const QString seed = SeedOf(seeds->currentItem());
    const QString folder = FolderOf(seeds->currentItem());
    QString question = "Delete seed " + seed + "?";
    if (seed == seedNamedBy(SeedFile)) {
        question += "\n\nIt is the seed in use, so it is also removed from the mods folder.";
    }
    Ask(this, question, "Delete", [this, seed, folder] {
        try {
            // Removes only the library folder the list found for this seed, and the mods folder's Kaepora folder
            // while the file in it names this seed: folders this program made.
            if (seedNamedBy(SeedFile) == seed) {
                std::filesystem::remove_all(Path(QDir(modsFolder->text()).filePath(ModName)));
            }
            std::filesystem::remove_all(Path(folder));
        } catch (const std::exception& e) {
            Tell(this, QMessageBox::Critical, "The seed could not be deleted.\n\n" + QString::fromUtf8(e.what()));
        }
        refresh();
    });
}

void Window::play() {
    if (!QProcess::startDetached(Program(emulator->text()), { "-g", gameFile->text() })) {
        Tell(this, QMessageBox::Critical, "The emulator did not start.");
    }
}

// Lists the library and reads which seed the mods folder holds. The selection stays, or moves to the seed given.
void Window::refresh(QString select) {
    if (select.isEmpty() && seeds->currentItem() != nullptr) {
        select = SeedOf(seeds->currentItem());
    }
    if (!seedNamedBy(CopyingFile).isEmpty()) {
        // A copy that was cut short left part of a seed where the game loads it. The file shows this program made it.
        std::error_code ignored;
        std::filesystem::remove_all(Path(QDir(modsFolder->text()).filePath(ModName)), ignored);
    }
    inUse = seedNamedBy(SeedFile);
    seeds->clear();
    for (const QFileInfo& folder : QDir(library).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time)) {
        const QString seed = folder.fileName();
        if (!IsWholeSeed(folder)) {
            continue;
        }
        auto* item = new QTreeWidgetItem(seeds, { seed == inUse ? seed + " (in use)" : seed });
        try {
            const SeedSettings made = SettingsOf(folder.filePath());
            item->setText(1, Summary(made));
            item->setToolTip(1, Details(made));
        } catch (const std::exception& e) {
            item->setText(1, "Spoiler log cannot be read");
            item->setToolTip(1, QString::fromUtf8(e.what()));
        }
        item->setData(0, Qt::UserRole, folder.absoluteFilePath());
        if (seed == select) {
            seeds->setCurrentItem(item);
        }
    }
    seeds->resizeColumnToContents(0);
    seeds->setColumnWidth(0, seeds->columnWidth(0) + seeds->fontMetrics().horizontalAdvance("MM"));
    status->setText(making != nullptr ? "Creating seed…"
                    : inUse.isEmpty() ? "No seed in use"
                                      : "Seed " + inUse + " is in use");
    updateButtons();
}

void Window::updateButtons() {
    const bool selected = seeds->currentItem() != nullptr;
    createButton->setEnabled(making == nullptr && !gameFile->text().isEmpty() && seedField->hasAcceptableInput());
    useButton->setEnabled(selected && !modsFolder->text().isEmpty());
    spoilerButton->setEnabled(selected);
    deleteButton->setEnabled(selected);
    playButton->setEnabled(!emulator->text().isEmpty() && !gameFile->text().isEmpty() && !inUse.isEmpty());
}

// The seed named by the file of this name in the mods folder's Kaepora folder. Empty when there is none. A folder with
// neither file is one this program did not make.
QString Window::seedNamedBy(const char* fileName) const {
    if (modsFolder->text().isEmpty()) {
        return {};
    }
    QFile file(QDir(modsFolder->text()).filePath(QString(ModName) + "/" + fileName));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QString seed = QString::fromUtf8(file.readAll());
    return IsSeed(seed) ? seed : QString();
}
