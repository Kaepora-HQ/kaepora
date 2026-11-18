#include "settings_dialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QStringList>
#include <QVBoxLayout>

#include <algorithm>
#include <initializer_list>
#include <iterator>
#include <string_view>
#include <utility>
#include <vector>

constexpr qsizetype NamedDungeons = 3;

static QString Listed(QStringList names) {
    const QString last = names.takeLast();
    return names.isEmpty() ? last : names.join(", ") + " and " + last;
}

static std::pair<QStringList, QStringList> ShuffledAndLeftOut(const SeedSettings& settings) {
    std::pair<QStringList, QStringList> names;
    for (const Dungeon& dungeon : Dungeons) {
        (settings.shuffledDungeons.contains(dungeon) ? names.first : names.second) << QString::fromUtf8(dungeon.name);
    }
    return names;
}

QString Summary(const SeedSettings& settings) {
    if (settings == SeedSettings{}) {
        return "Default settings";
    }
    QStringList changed;
    if (const auto [shuffled, leftOut] = ShuffledAndLeftOut(settings); leftOut.size() > NamedDungeons) {
        changed << QString("%1 of %2 dungeons shuffled").arg(shuffled.size()).arg(std::size(Dungeons));
    } else if (!leftOut.isEmpty()) {
        changed << Listed(leftOut) + " not shuffled";
    }
    if (settings.keys == DungeonItems::InTheirOwnChests) {
        changed << "keys in their own chests";
    } else if (settings.keys == DungeonItems::GivenAtTheStart) {
        changed << "keys not needed";
    }
    if (settings.mapsAndCompasses == DungeonItems::InTheirOwnChests) {
        changed << "maps and compasses in their own chests";
    } else if (settings.mapsAndCompasses == DungeonItems::GivenAtTheStart) {
        changed << "maps and compasses given at the start";
    }
    if (settings.forest == Forest::Open) {
        changed << "forest open";
    }
    if (settings.bridge == Bridge::AlwaysOpen) {
        changed << "bridge always open";
    }
    if (settings.trials == Trials::None) {
        changed << "no trials";
    }
    if (settings.lens == Lens::NeverNeeded) {
        changed << "Lens never needed";
    }
    QString summary = changed.join(", ");
    summary[0] = summary[0].toUpper();
    return summary;
}

static QString Line(std::string_view setting, const QString& value) {
    return QString::fromUtf8(setting) + ": " + value;
}

static QString ShuffledDungeons(const SeedSettings& settings) {
    const auto [shuffled, leftOut] = ShuffledAndLeftOut(settings);
    if (leftOut.isEmpty()) {
        return "All";
    }
    if (shuffled.isEmpty()) {
        return "None";
    }
    return shuffled.size() < leftOut.size() ? Listed(shuffled) : "All but " + Listed(leftOut);
}

QString Details(const SeedSettings& settings) {
    return QStringList{ Line(ShuffledDungeonsSetting, ShuffledDungeons(settings)),
                        Line(KeysSetting, QString::fromUtf8(Words(KeysChoices, settings.keys))),
                        Line(MapsAndCompassesSetting,
                             QString::fromUtf8(Words(MapsAndCompassesChoices, settings.mapsAndCompasses))),
                        Line(ForestSetting, QString::fromUtf8(Words(ForestChoices, settings.forest))),
                        Line(BridgeSetting, QString::fromUtf8(Words(BridgeChoices, settings.bridge))),
                        Line(TrialsSetting, QString::fromUtf8(Words(TrialsChoices, settings.trials))),
                        Line(LensSetting, QString::fromUtf8(Words(LensChoices, settings.lens))) }
        .join('\n');
}

// Each group lays out its own labels. They all take the widest one's width, so the choices start in one column.
static void AlignLabels(std::initializer_list<QFormLayout*> forms) {
    std::vector<QWidget*> labels;
    int width = 0;
    for (QFormLayout* form : forms) {
        for (int row = 0; row < form->rowCount(); ++row) {
            QWidget* label = form->itemAt(row, QFormLayout::LabelRole)->widget();
            labels.push_back(label);
            width = std::max(width, label->sizeHint().width());
        }
    }
    for (QWidget* label : labels) {
        label->setMinimumWidth(width);
    }
}

template <typename Value, size_t Size>
static QComboBox* Choice(const std::pair<Value, std::string_view> (&choices)[Size]) {
    auto* box = new QComboBox;
    for (const auto& [value, words] : choices) {
        box->addItem(QString::fromUtf8(words), static_cast<int>(value));
    }
    return box;
}

template <typename Value>
static void Select(QComboBox* box, Value value) {
    box->setCurrentIndex(box->findData(static_cast<int>(value)));
}

template <typename Value>
static Value Selected(const QComboBox* box) {
    return static_cast<Value>(box->currentData().toInt());
}

void ChooseSettings(QWidget* window, const SeedSettings& settings,
                    const std::function<void(const SeedSettings&)>& chosen) {
    auto* dialog = new QDialog(window);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle("Seed settings");

    std::vector<std::pair<Dungeon, QCheckBox*>> dungeons;
    auto* checklist = new QGridLayout;
    const int rows = static_cast<int>(std::size(Dungeons) + 1) / 2;
    for (const Dungeon& dungeon : Dungeons) {
        auto* box = new QCheckBox(QString::fromUtf8(dungeon.name));
        const int index = static_cast<int>(dungeons.size());
        checklist->addWidget(box, index % rows, index / rows);
        dungeons.emplace_back(dungeon, box);
    }
    QComboBox* keys = Choice(KeysChoices);
    QComboBox* mapsAndCompasses = Choice(MapsAndCompassesChoices);
    auto* dungeonForm = new QFormLayout;
    dungeonForm->addRow(QString::fromUtf8(ShuffledDungeonsSetting), checklist);
    dungeonForm->addRow(QString::fromUtf8(KeysSetting), keys);
    dungeonForm->addRow(QString::fromUtf8(MapsAndCompassesSetting), mapsAndCompasses);
    auto* dungeonGroup = new QGroupBox("Dungeons");
    dungeonGroup->setLayout(dungeonForm);

    QComboBox* forest = Choice(ForestChoices);
    QComboBox* bridge = Choice(BridgeChoices);
    QComboBox* trials = Choice(TrialsChoices);
    auto* worldForm = new QFormLayout;
    worldForm->addRow(QString::fromUtf8(ForestSetting), forest);
    worldForm->addRow(QString::fromUtf8(BridgeSetting), bridge);
    worldForm->addRow(QString::fromUtf8(TrialsSetting), trials);
    auto* worldGroup = new QGroupBox("World");
    worldGroup->setLayout(worldForm);

    QComboBox* lens = Choice(LensChoices);
    auto* logicForm = new QFormLayout;
    logicForm->addRow(QString::fromUtf8(LensSetting), lens);
    auto* logicGroup = new QGroupBox("Logic");
    logicGroup->setLayout(logicForm);
    AlignLabels({ dungeonForm, worldForm, logicForm });

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::RestoreDefaults | QDialogButtonBox::Cancel |
                                         QDialogButtonBox::Ok);
    QPushButton* defaults = buttons->button(QDialogButtonBox::RestoreDefaults);
    defaults->setText("Defaults");

    auto* layout = new QVBoxLayout(dialog);
    layout->addWidget(dungeonGroup);
    layout->addWidget(worldGroup);
    layout->addWidget(logicGroup);
    layout->addWidget(buttons);

    const auto show = [=](const SeedSettings& shown) {
        for (const auto& [dungeon, box] : dungeons) {
            box->setChecked(shown.shuffledDungeons.contains(dungeon));
        }
        Select(keys, shown.keys);
        Select(mapsAndCompasses, shown.mapsAndCompasses);
        Select(forest, shown.forest);
        Select(bridge, shown.bridge);
        Select(trials, shown.trials);
        Select(lens, shown.lens);
    };
    show(settings);

    QObject::connect(defaults, &QPushButton::clicked, dialog, [show] { show(SeedSettings{}); });
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, dialog, [=] {
        SeedSettings shown;
        shown.shuffledDungeons.clear();
        for (const auto& [dungeon, box] : dungeons) {
            if (box->isChecked()) {
                shown.shuffledDungeons.insert(dungeon);
            }
        }
        shown.keys = Selected<DungeonItems>(keys);
        shown.mapsAndCompasses = Selected<DungeonItems>(mapsAndCompasses);
        shown.forest = Selected<Forest>(forest);
        shown.bridge = Selected<Bridge>(bridge);
        shown.trials = Selected<Trials>(trials);
        shown.lens = Selected<Lens>(lens);
        chosen(shown);
        dialog->accept();
    });
    dialog->open();
}
