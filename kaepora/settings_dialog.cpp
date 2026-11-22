#include "settings_dialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QStringList>
#include <QVBoxLayout>

#include <algorithm>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <ranges>
#include <set>
#include <span>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

constexpr qsizetype NamedDungeons = 3;

static QString Listed(QStringList names) {
    const QString last = names.takeLast();
    return names.isEmpty() ? last : names.join(", ") + " and " + last;
}

static QString Counted(size_t items) {
    return items == 0 ? "None" : items == 1 ? "1 item" : QString("%1 items").arg(items);
}

// The rows of the "Gifts" list, each a group whole or by one of its choices.
static size_t GiftRows() {
    std::set<std::string_view> rows;
    for (const GiftGroup& group : GiftGroups) {
        rows.insert(group.row);
    }
    return rows.size();
}

static QString CountedGroups(size_t groups) {
    if (groups == 0) {
        return "Vanilla";
    }
    return groups == GiftRows() ? "All groups" : QString("%1 of %2 groups").arg(groups).arg(GiftRows());
}

static std::pair<QStringList, QStringList> ShuffledAndLeftOut(const SeedSettings& settings) {
    std::pair<QStringList, QStringList> names;
    for (const Dungeon& dungeon : Dungeons) {
        (settings.shuffledDungeons.contains(dungeon) ? names.first : names.second) << QString::fromUtf8(dungeon.name);
    }
    return names;
}

// The names of the groups shuffled, and the rows left out.
static std::pair<QStringList, QStringList> GiftGroupsShuffledAndLeftOut(const SeedSettings& settings) {
    std::set<std::string_view> rows;
    for (const GiftGroup& group : settings.gifts) {
        rows.insert(group.row);
    }
    std::pair<QStringList, QStringList> names;
    for (const GiftGroup& group : GiftGroups) {
        if (settings.gifts.contains(group)) {
            names.first << QString::fromUtf8(group.name);
        } else if (rows.insert(group.row).second) {
            names.second << QString::fromUtf8(group.row);
        }
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
    if (settings.forest == Forest::ClosedDeku) {
        changed << "forest exit open";
    } else if (settings.forest == Forest::Open) {
        changed << "forest open";
    }
    if (settings.bridge == Bridge::AlwaysOpen) {
        changed << "bridge always open";
    }
    if (settings.trials == Trials::None) {
        changed << "no trials";
    }
    if (settings.opening == Opening::Played) {
        changed << "opening played";
    }
    if (settings.cutscenes == Cutscenes::Played) {
        changed << "cutscenes played";
    }
    if (!settings.startWith.empty()) {
        changed << "starts with " + Counted(settings.startWith.size());
    }
    if (settings.shops) {
        changed << "shop goods shuffled";
    }
    if (settings.skulltulas == Skulltulas::InDungeons) {
        changed << "dungeon Gold Skulltulas shuffled";
    } else if (settings.skulltulas == Skulltulas::InTheOverworld) {
        changed << "overworld Gold Skulltulas shuffled";
    } else if (settings.skulltulas == Skulltulas::All) {
        changed << "Gold Skulltulas shuffled";
    }
    if (settings.gifts.empty()) {
        changed << "no gifts shuffled";
    } else if (settings.gifts.size() == GiftRows()) {
        changed << "gifts shuffled";
    } else if (settings.gifts != DefaultGifts()) {
        changed << QString("%1 of %2 gift groups shuffled").arg(settings.gifts.size()).arg(GiftRows());
    }
    if (settings.songs == Songs::Vanilla) {
        changed << "songs not shuffled";
    }
    if (settings.looseItems == LooseItems::Vanilla) {
        changed << "loose items not shuffled";
    }
    if (settings.rewards == Rewards::Vanilla) {
        changed << "dungeon rewards not shuffled";
    } else if (settings.rewards == Rewards::Shuffled) {
        changed << "dungeon rewards shuffled";
    }
    if (settings.lens == Lens::NeverNeeded) {
        changed << "Lens never needed";
    }
    if (settings.gossipStones == GossipStones::Vanilla) {
        changed << "no gossip stone hints";
    }
    if (settings.hintsFromCharacters == HintsFromCharacters::Vanilla) {
        changed << "no hints from characters";
    }
    QString summary = changed.join(", ");
    summary[0] = summary[0].toUpper();
    return summary;
}

static QString Line(std::string_view setting, const QString& value) {
    return QString::fromUtf8(setting) + ": " + value;
}

static QString Chosen(const std::pair<QStringList, QStringList>& shuffledAndLeftOut) {
    const auto& [shuffled, leftOut] = shuffledAndLeftOut;
    if (leftOut.isEmpty()) {
        return "All";
    }
    if (shuffled.isEmpty()) {
        return "None";
    }
    return shuffled.size() < leftOut.size() ? Listed(shuffled) : "All but " + Listed(leftOut);
}

static QString StartWith(const SeedSettings& settings) {
    QStringList names;
    for (const StartStep& item : StartWithInOrder(settings)) {
        names << QString::fromUtf8(item.name);
    }
    return names.isEmpty() ? "None" : Listed(names);
}

static QString ShopPricesDetail(const ShopPrices& prices) {
    if (const auto* one = std::get_if<OnePrice>(&prices)) {
        return QString("One price of %1 rupees").arg(one->price);
    }
    if (const auto* between = std::get_if<PricesBetween>(&prices)) {
        return QString("Between %1 and %2 rupees").arg(between->lowest).arg(between->highest);
    }
    return QString::fromUtf8(ShopPricesChoice(prices).second);
}

QString Details(const SeedSettings& settings) {
    QStringList lines{ Line(ShuffledDungeonsSetting, Chosen(ShuffledAndLeftOut(settings))),
                        Line(KeysSetting, QString::fromUtf8(Words(KeysChoices, settings.keys))),
                        Line(MapsAndCompassesSetting,
                             QString::fromUtf8(Words(MapsAndCompassesChoices, settings.mapsAndCompasses))),
                        Line(ForestSetting, QString::fromUtf8(Words(ForestChoices, settings.forest))),
                        Line(BridgeSetting, QString::fromUtf8(Words(BridgeChoices, settings.bridge))),
                        Line(TrialsSetting, QString::fromUtf8(Words(TrialsChoices, settings.trials))),
                        Line(OpeningSetting, QString::fromUtf8(Words(OpeningChoices, settings.opening))),
                        Line(CutscenesSetting, QString::fromUtf8(Words(CutscenesChoices, settings.cutscenes))),
                        Line(StartWithSetting, StartWith(settings)),
                        Line(ShopsSetting, QString::fromUtf8(ShopsWords(settings.shops))) };
    if (ShopPricesApply(settings.shops)) {
        lines << Line(ShopPricesSetting, ShopPricesDetail(settings.shops->prices));
    }
    lines << Line(SkulltulasSetting, QString::fromUtf8(Words(SkulltulasChoices, settings.skulltulas)))
          << Line(GiftsSetting, settings.gifts.empty() ? "Vanilla" : Chosen(GiftGroupsShuffledAndLeftOut(settings)))
          << Line(SongsSetting, QString::fromUtf8(Words(SongsChoices, settings.songs)))
          << Line(LooseItemsSetting, QString::fromUtf8(Words(LooseItemsChoices, settings.looseItems)))
          << Line(RewardsSetting, QString::fromUtf8(Words(RewardsChoices, settings.rewards)))
          << Line(LensSetting, QString::fromUtf8(Words(LensChoices, settings.lens)))
          << Line(GossipStonesSetting, QString::fromUtf8(Words(GossipStonesChoices, settings.gossipStones)))
          << Line(HintsFromCharactersSetting,
                  QString::fromUtf8(Words(HintsFromCharactersChoices, settings.hintsFromCharacters)));
    return lines.join('\n');
}

// Each group lays out its own labels. They all take the widest one's width, so the groups' choices start in line.
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

namespace {
// QScrollArea caps its size hint at 36 by 24 times its font's height. It scrolls a newly focused control into view
// only when Tab moves focus between two controls inside it.
class WholeScrollArea final : public QScrollArea {
public:
    explicit WholeScrollArea(QWidget* whole) {
        setFrameShape(QFrame::NoFrame);
        setFocusPolicy(Qt::NoFocus);
        setWidgetResizable(true);
        setWidget(whole);
        connect(qApp, &QApplication::focusChanged, this, [this](QWidget*, QWidget* now) { ensureWidgetVisible(now); });
    }

    QSize sizeHint() const override {
        return widget()->sizeHint();
    }
};
} // namespace

template <typename Value, size_t Size>
static QComboBox* Choice(const std::pair<Value, std::string_view> (&choices)[Size]) {
    auto* box = new QComboBox;
    for (const auto& [value, words] : choices) {
        box->addItem(QString::fromUtf8(words), static_cast<int>(value));
    }
    return box;
}

static int RoundedPrice(int price) {
    return (price + ShopPriceStep / 2) / ShopPriceStep * ShopPriceStep;
}

// A box for a price SoH can set. A typed price goes to the nearest step. The box takes its row's height, so showing it
// leaves the groups as tall as they were.
static QSpinBox* PriceBox(const QString& name) {
    auto* box = new QSpinBox;
    box->setSizePolicy(QSizePolicy(QSizePolicy::Minimum, QSizePolicy::Ignored, QSizePolicy::SpinBox));
    box->setRange(0, MostShopPrice);
    box->setSingleStep(ShopPriceStep);
    box->setAccessibleName(name);
    QObject::connect(box, &QSpinBox::editingFinished, box, [box] { box->setValue(RoundedPrice(box->value())); });
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

static void OpenList(QWidget* settingsDialog, std::string_view setting, const QString& title, QLayout* choices,
                     const std::function<void()>& accepted) {
    auto* dialog = new QDialog(settingsDialog);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(QString::fromUtf8(setting));
    auto* group = new QGroupBox(title);
    group->setLayout(choices);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Ok);
    auto* layout = new QVBoxLayout(dialog);
    layout->addWidget(group);
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, dialog, [=] {
        accepted();
        dialog->accept();
    });
    dialog->open();
}

// A list in a dialog: a box for each entry of a row of one choice, and for a row of several choices, a choice among
// them that starts at the words for none.
template <typename Entry>
static void ChooseInList(QWidget* settingsDialog, std::string_view setting, const QString& title,
                         std::span<const Entry> entries, std::string_view Entry::* row, const QString& none,
                         const std::set<Entry>& chosen, const std::function<void(const std::set<Entry>&)>& done) {
    std::vector<std::pair<Entry, QCheckBox*>> boxes;
    std::vector<QComboBox*> rows;
    std::string_view rowName;
    auto* column = new QVBoxLayout;
    auto* form = new QFormLayout;
    for (const Entry& entry : entries) {
        if (entry.choice.empty()) {
            auto* box = new QCheckBox(QString::fromUtf8(entry.name));
            box->setChecked(chosen.contains(entry));
            column->addWidget(box);
            boxes.emplace_back(entry, box);
            continue;
        }
        if (entry.*row != rowName) {
            rowName = entry.*row;
            rows.push_back(new QComboBox);
            rows.back()->addItem(none);
            form->addRow(QString::fromUtf8(rowName), rows.back());
        }
        rows.back()->addItem(QString::fromUtf8(entry.choice), int(&entry - entries.data()));
        if (chosen.contains(entry)) {
            rows.back()->setCurrentIndex(rows.back()->count() - 1);
        }
    }
    auto* columns = new QHBoxLayout;
    columns->addLayout(column);
    columns->addLayout(form);
    OpenList(settingsDialog, setting, title, columns, [=] {
        std::set<Entry> picked;
        for (const auto& [entry, box] : boxes) {
            if (box->isChecked()) {
                picked.insert(entry);
            }
        }
        for (const QComboBox* box : rows) {
            if (box->currentIndex() > 0) {
                picked.insert(entries[box->currentData().toInt()]);
            }
        }
        done(picked);
    });
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

    QComboBox* shops = Choice(ShopCountChoices);
    shops->insertItem(0, QString::fromUtf8(VanillaShops));
    auto* shopPrices = new QComboBox;
    for (const std::string_view words : std::views::values(ShopPricesChoices)) {
        shopPrices->addItem(QString::fromUtf8(words));
    }
    QSpinBox* shopPrice = PriceBox("Shop price");
    QSpinBox* lowestShopPrice = PriceBox("Lowest shop price");
    QSpinBox* highestShopPrice = PriceBox("Highest shop price");
    auto* priceTo = new QLabel("to");
    auto* shopPricesRow = new QHBoxLayout;
    // The choice takes the width the numbers leave, so the row ends where every other row does.
    shopPricesRow->addWidget(shopPrices, 1);
    shopPricesRow->addWidget(shopPrice);
    shopPricesRow->addWidget(lowestShopPrice);
    shopPricesRow->addWidget(priceTo);
    shopPricesRow->addWidget(highestShopPrice);
    auto* shopPricesLabel = new QLabel(QString::fromUtf8(ShopPricesSetting));
    shopPricesLabel->setBuddy(shopPrices);
    QComboBox* skulltulas = Choice(SkulltulasChoices);
    auto gifts = std::make_shared<std::set<GiftGroup>>();
    auto* giftGroups = new QPushButton;
    const auto showGifts = [=](const std::set<GiftGroup>& shown) {
        *gifts = shown;
        giftGroups->setText(CountedGroups(shown.size()));
    };
    QObject::connect(giftGroups, &QPushButton::clicked, dialog, [=] {
        ChooseInList<GiftGroup>(dialog, GiftsSetting, "Gifts", GiftGroups, &GiftGroup::row, "Vanilla", *gifts,
                                showGifts);
    });
    QComboBox* songs = Choice(SongsChoices);
    QComboBox* looseItems = Choice(LooseItemsChoices);
    QComboBox* rewards = Choice(RewardsChoices);
    auto* shuffleForm = new QFormLayout;
    shuffleForm->addRow(QString::fromUtf8(ShopsSetting), shops);
    shuffleForm->addRow(shopPricesLabel, shopPricesRow);
    shuffleForm->addRow(QString::fromUtf8(SkulltulasSetting), skulltulas);
    shuffleForm->addRow(QString::fromUtf8(GiftsSetting), giftGroups);
    shuffleForm->addRow(QString::fromUtf8(SongsSetting), songs);
    shuffleForm->addRow(QString::fromUtf8(LooseItemsSetting), looseItems);
    shuffleForm->addRow(QString::fromUtf8(RewardsSetting), rewards);
    auto* shuffleGroup = new QGroupBox("Shuffle");
    shuffleGroup->setLayout(shuffleForm);

    QComboBox* forest = Choice(ForestChoices);
    QComboBox* bridge = Choice(BridgeChoices);
    QComboBox* trials = Choice(TrialsChoices);
    auto* worldForm = new QFormLayout;
    worldForm->addRow(QString::fromUtf8(ForestSetting), forest);
    worldForm->addRow(QString::fromUtf8(BridgeSetting), bridge);
    worldForm->addRow(QString::fromUtf8(TrialsSetting), trials);
    auto* worldGroup = new QGroupBox("World");
    worldGroup->setLayout(worldForm);

    // What the button counts and the list opens with.
    auto startWith = std::make_shared<std::set<StartStep>>();
    auto* items = new QPushButton;
    const auto showStartWith = [=](const std::set<StartStep>& shown) {
        *startWith = shown;
        items->setText(Counted(shown.size()));
    };
    QObject::connect(items, &QPushButton::clicked, dialog, [=] {
        ChooseInList<StartStep>(dialog, StartWithSetting, "Items", StartSteps, &StartStep::item, "None", *startWith,
                                showStartWith);
    });
    QComboBox* opening = Choice(OpeningChoices);
    QComboBox* cutscenes = Choice(CutscenesChoices);
    auto* startForm = new QFormLayout;
    startForm->addRow(QString::fromUtf8(OpeningSetting), opening);
    startForm->addRow(QString::fromUtf8(CutscenesSetting), cutscenes);
    startForm->addRow("Items", items);
    auto* startGroup = new QGroupBox("Start");
    startGroup->setLayout(startForm);

    QComboBox* lens = Choice(LensChoices);
    auto* logicForm = new QFormLayout;
    logicForm->addRow(QString::fromUtf8(LensSetting), lens);
    auto* logicGroup = new QGroupBox("Logic");
    logicGroup->setLayout(logicForm);

    QComboBox* gossipStones = Choice(GossipStonesChoices);
    QComboBox* hintsFromCharacters = Choice(HintsFromCharactersChoices);
    auto* hintsForm = new QFormLayout;
    hintsForm->addRow(QString::fromUtf8(GossipStonesSetting), gossipStones);
    hintsForm->addRow(QString::fromUtf8(HintsFromCharactersSetting), hintsFromCharacters);
    auto* hintsGroup = new QGroupBox("Hints");
    hintsGroup->setLayout(hintsForm);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::RestoreDefaults | QDialogButtonBox::Cancel |
                                         QDialogButtonBox::Ok);
    QPushButton* defaults = buttons->button(QDialogButtonBox::RestoreDefaults);
    defaults->setText("Defaults");

    // Prices wait for shop slots to be shuffled, and a number shows only while its choice is picked.
    const auto showPrices = [=] {
        const bool shuffled = shops->currentIndex() != 0 && Selected<ShopCount>(shops) != ShopCount::AmongTheShops;
        const ShopPrices& prices = ShopPricesChoices[shopPrices->currentIndex()].first;
        shopPrices->setEnabled(shuffled);
        shopPrice->setVisible(shuffled && std::holds_alternative<OnePrice>(prices));
        for (QWidget* range : std::initializer_list<QWidget*>{ lowestShopPrice, priceTo, highestShopPrice }) {
            range->setVisible(shuffled && std::holds_alternative<PricesBetween>(prices));
        }
    };
    QObject::connect(shops, &QComboBox::currentIndexChanged, dialog, showPrices);
    QObject::connect(shopPrices, &QComboBox::currentIndexChanged, dialog, showPrices);

    const auto show = [=](const SeedSettings& shown) {
        for (const auto& [dungeon, box] : dungeons) {
            box->setChecked(shown.shuffledDungeons.contains(dungeon));
        }
        Select(keys, shown.keys);
        Select(mapsAndCompasses, shown.mapsAndCompasses);
        Select(forest, shown.forest);
        Select(bridge, shown.bridge);
        Select(trials, shown.trials);
        Select(opening, shown.opening);
        Select(cutscenes, shown.cutscenes);
        showStartWith(shown.startWith);
        shops->setCurrentIndex(shown.shops ? shops->findData(static_cast<int>(shown.shops->count)) : 0);
        const ShopPrices prices = shown.shops ? shown.shops->prices : DefaultShopPrices;
        shopPrices->setCurrentIndex(static_cast<int>(&ShopPricesChoice(prices) - std::begin(ShopPricesChoices)));
        const auto* one = std::get_if<OnePrice>(&prices);
        shopPrice->setValue(one ? one->price : OnePrice{}.price);
        const auto* between = std::get_if<PricesBetween>(&prices);
        const PricesBetween range = between ? *between : PricesBetween{};
        lowestShopPrice->setValue(range.lowest);
        highestShopPrice->setValue(range.highest);
        showPrices();
        Select(skulltulas, shown.skulltulas);
        showGifts(shown.gifts);
        Select(songs, shown.songs);
        Select(looseItems, shown.looseItems);
        Select(rewards, shown.rewards);
        Select(lens, shown.lens);
        Select(gossipStones, shown.gossipStones);
        Select(hintsFromCharacters, shown.hintsFromCharacters);
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
        shown.opening = Selected<Opening>(opening);
        shown.cutscenes = Selected<Cutscenes>(cutscenes);
        shown.startWith = *startWith;
        if (shops->currentIndex() != 0) {
            shown.shops = ShuffledShops{ Selected<ShopCount>(shops) };
        }
        if (ShopPricesApply(shown.shops)) {
            ShopPrices& prices = shown.shops->prices;
            prices = ShopPricesChoices[shopPrices->currentIndex()].first;
            if (auto* one = std::get_if<OnePrice>(&prices)) {
                one->price = RoundedPrice(shopPrice->value());
            }
            if (auto* between = std::get_if<PricesBetween>(&prices)) {
                // Two prices are a range in either order.
                const auto [lowest, highest] =
                    std::minmax({ RoundedPrice(lowestShopPrice->value()), RoundedPrice(highestShopPrice->value()) });
                *between = { lowest, highest };
            }
        }
        shown.skulltulas = Selected<Skulltulas>(skulltulas);
        shown.gifts = *gifts;
        shown.songs = Selected<Songs>(songs);
        shown.looseItems = Selected<LooseItems>(looseItems);
        shown.rewards = Selected<Rewards>(rewards);
        shown.lens = Selected<Lens>(lens);
        shown.gossipStones = Selected<GossipStones>(gossipStones);
        shown.hintsFromCharacters = Selected<HintsFromCharacters>(hintsFromCharacters);
        chosen(shown);
        dialog->accept();
    });

    // Qt keeps a row's cached size hint when a price box in it hides before the window first shows, so the groups go
    // into the scroll area only after show(settings).
    auto* left = new QVBoxLayout;
    left->addWidget(dungeonGroup);
    left->addWidget(shuffleGroup);
    left->addStretch();
    AlignLabels({ dungeonForm, shuffleForm });
    auto* right = new QVBoxLayout;
    right->addWidget(worldGroup);
    right->addWidget(startGroup);
    right->addWidget(logicGroup);
    right->addWidget(hintsGroup);
    right->addStretch();
    AlignLabels({ worldForm, startForm, logicForm, hintsForm });
    auto* groups = new QWidget;
    auto* columns = new QHBoxLayout(groups);
    columns->setContentsMargins({});
    columns->addLayout(left);
    columns->addLayout(right);
    auto* scroll = new WholeScrollArea(groups);

    auto* layout = new QVBoxLayout(dialog);
    layout->addWidget(scroll);
    layout->addWidget(buttons);

    dialog->ensurePolished();
    const QSize room = window->screen()->availableSize() - (window->frameGeometry().size() - window->size());
    QSize size = dialog->sizeHint();
    if (size.height() > room.height()) {
        size.rwidth() += scroll->verticalScrollBar()->sizeHint().width();
    }
    dialog->resize(size.boundedTo(room));
    dialog->open();
}
