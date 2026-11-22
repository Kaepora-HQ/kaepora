#include "tracker.h"

#include "bytes.h"

#include <QStringList>
#include <QTest>

#include <algorithm>
#include <map>
#include <string>

static std::vector<quint32> Read(const Tracker& tracker, std::string_view menu, std::map<std::string, quint32> held) {
    held.try_emplace(Scene.name, Murmur3("KokiriForest_Child"));
    std::vector<quint32> read;
    for (const SavedValue& value : tracker.values()) {
        const auto found = held.find(value.name);
        read.push_back(value.name == MenuState.name ? Murmur3(menu) : found == held.end() ? 0 : found->second);
    }
    return read;
}

static QStringList Shown(const Tracker& tracker, const QStringList& names) {
    const std::vector<CellView> cells = tracker.cells();
    QStringList shown;
    for (const QString& name : names) {
        const auto cell = std::ranges::find(cells, name.toStdString(), &CellView::name);
        if (cell == cells.end()) {
            shown.append(name + ": no such cell");
            continue;
        }
        shown.append(
            QString("%1: %2 %3").arg(name, QString::fromStdString(cell->picture), cell->lit ? "lit" : "unlit") +
            (cell->count ? QString(" %1").arg(*cell->count) : "") +
            (cell->count && cell->total ? QString("/%1").arg(*cell->total) : ""));
    }
    return shown;
}

static bool ShowsNothing(const Tracker& tracker) {
    return std::ranges::none_of(tracker.cells(), [](const CellView& cell) { return cell.lit || cell.count; });
}

class TrackerTest : public QObject {
    Q_OBJECT

private slots:
    void aTierShowsItsHighestStageHeld() {
        Tracker tracker;
        tracker.take(Read(tracker, "None",
                          { { "PouchData.Item_Hookshot.StockNum", 1 },
                            { "PouchData.Item_Longshot.StockNum", 1 },
                            { "PouchData.Item_Scale_Gold.StockNum", 1 },
                            { "PlayerStatus.LiftGrade", Murmur3("Middle") },
                            { "PouchContentMaxNum.Rupee", 1000 } }));
        QCOMPARE(Shown(tracker, { "Hookshot", "Scale", "Strength", "Wallet" }),
                 QStringList({ "Hookshot: Item_Longshot lit", "Scale: Item_Scale_Gold lit",
                               "Strength: Armor_Adult_Gauntlet_Silver lit", "Wallet: Item_Wallet_Large lit" }));
    }

    void aChainShowsTheLastItemHeld() {
        Tracker tracker;
        tracker.take(Read(tracker, "None",
                          { { "PouchData.Item_ZeldaLetter.StockNum", 1 },
                            { "PouchData.Armor_Child_Mask_Truth.StockNum", 1 },
                            { "PouchData.Item_ClaimCheck.StockNum", 1 } }));
        QCOMPARE(
            Shown(tracker, { "Child trade item", "Adult trade item" }),
            QStringList({ "Child trade item: Armor_Child_Mask_Truth lit", "Adult trade item: Item_ClaimCheck lit" }));
    }

    void aBottleShowsItsContents() {
        Tracker tracker;
        tracker.take(Read(tracker, "None",
                          { { "PouchData.BottleA.StockNum", 1 },
                            { "PlayerStatus.AssignableItem.BottleInfo.BottleA.BottleItem", Murmur3("Fairy") },
                            { "PouchData.BottleB.StockNum", 1 },
                            { "PlayerStatus.AssignableItem.BottleInfo.BottleB.BottleItem", Murmur3("Empty") },
                            { "PouchData.BottleC.StockNum", 1 },
                            { "PlayerStatus.AssignableItem.BottleInfo.BottleC.BottleItem", Murmur3("Letter") } }));
        QCOMPARE(Shown(tracker, { "Bottle 1", "Bottle 2", "Bottle 3" }),
                 QStringList({ "Bottle 1: Item_Bottle_Fairy lit", "Bottle 2: Item_Bottle lit",
                               "Bottle 3: Item_Bottle_Letter lit" }));
    }

    void theFifthBottleShowsItsContents() {
        Tracker tracker;
        tracker.take(Read(tracker, "None",
                          { { "PouchData.BottleE.StockNum", 1 },
                            { "PlayerStatus.AssignableItem.BottleInfo.BottleE.BottleItem", Murmur3("Empty") } }));
        QCOMPARE(Shown(tracker, { "Bottle 5" }), QStringList{ "Bottle 5: Item_Bottle lit" });
        tracker.take(Read(tracker, "None",
                          { { "PouchData.BottleE.StockNum", 1 },
                            { "PlayerStatus.AssignableItem.BottleInfo.BottleE.BottleItem", Murmur3("Bug") } }));
        QCOMPARE(Shown(tracker, { "Bottle 5" }), QStringList{ "Bottle 5: Item_Bottle_Bug lit" });
    }

    void magicShowsTheMeterThenTheGiftOfDoubledMagic() {
        Tracker tracker;
        tracker.take(Read(tracker, "None", { { "PlayerStatus.MPMax", 240 } }));
        QCOMPARE(Shown(tracker, { "Magic" }), QStringList{ "Magic: Item_FairyBless_MP lit" });
        tracker.take(Read(tracker, "None", { { "PlayerStatus.MPMax", 480 } }));
        QCOMPARE(Shown(tracker, { "Magic" }), QStringList{ "Magic: Item_FairyBless_MPDouble lit" });
    }

    void aCountShowsItsNumber() {
        Tracker tracker;
        tracker.take(Read(tracker, "None",
                          { { "PouchData.Item_Token_Gold.StockNum", 23 },
                            { "PlayerStatus.MaxLife", 28 },
                            { "IsGet.Item_DekuStick", 1 },
                            { "PouchContentMaxNum.DekuStick", 20 },
                            { "PouchData.Obj_Heart_Piece.StockNum", 2 } }));
        QCOMPARE(Shown(tracker, { "Gold Skulltula Tokens", "Hearts", "Deku Sticks", "Pieces of Heart" }),
                 QStringList({ "Gold Skulltula Tokens: Item_Token_Gold lit 23", "Hearts: HeartW_00_ASTC4x4 lit 7",
                               "Deku Sticks: Item_DekuStick lit 20", "Pieces of Heart: CollectHeartPiece_01 lit 2" }));
    }

    void aSmallKeyLightsFromTheFirstFoundAndCountsTheFoundOfItsDungeonsTotal() {
        Tracker tracker;
        tracker.take(Read(
            tracker, "None",
            { { "DungeonInfo.ForestTemple.SmallKeyTotalNum", 1 }, { "DungeonInfo.FireTemple.SmallKeyTotalNum", 8 } }));
        QCOMPARE(Shown(tracker, { "Forest Temple Small Keys", "Fire Temple Small Keys", "Water Temple Small Keys" }),
                 QStringList({ "Forest Temple Small Keys: Item_Dungeon_SmallKey lit 1/5",
                               "Fire Temple Small Keys: Item_Dungeon_SmallKey lit 8/8",
                               "Water Temple Small Keys: Item_Dungeon_SmallKey unlit 0/4" }));
    }

    void aCellWithNothingHeldShowsItsFirstPictureUnlit() {
        Tracker tracker;
        tracker.take(Read(tracker, "None", {}));
        QCOMPARE(
            Shown(tracker, { "Megaton Hammer", "Hookshot", "Bottle 1", "Child trade item", "Forest Temple Boss Key" }),
            QStringList({ "Megaton Hammer: Item_Hammer unlit", "Hookshot: Item_Hookshot unlit",
                          "Bottle 1: Item_Bottle unlit", "Child trade item: Item_WeirdEgg unlit",
                          "Forest Temple Boss Key: Item_Dungeon_BossKey unlit" }));
    }

    void nothingShowsFromTheTitleUntilPlay() {
        const std::map<std::string, quint32> finished = { { "PouchData.Item_Hammer.StockNum", 1 },
                                                          { "PouchData.Item_Token_Gold.StockNum", 100 },
                                                          { "PlayerStatus.MaxLife", 80 } };
        Tracker tracker;
        QVERIFY(!tracker.take(Read(tracker, "Title", finished)));
        QVERIFY(ShowsNothing(tracker));
        QVERIFY(!tracker.take(Read(tracker, "Loading", finished)));
        QVERIFY(ShowsNothing(tracker));
        QVERIFY(tracker.take(Read(tracker, "None", finished)));
        const QStringList shown = { "Megaton Hammer: Item_Hammer lit", "Hearts: HeartW_00_ASTC4x4 lit 20" };
        QCOMPARE(Shown(tracker, { "Megaton Hammer", "Hearts" }), shown);
        QVERIFY(!tracker.take(Read(tracker, "Loading", {})));
        QVERIFY(!tracker.take(Read(tracker, "ItemMenu", {})));
        QCOMPARE(Shown(tracker, { "Megaton Hammer", "Hearts" }), shown);
        QVERIFY(tracker.take(Read(tracker, "Title", finished)));
        QVERIFY(ShowsNothing(tracker));
    }

    void aNewGameShowsWhatLinkHolds() {
        Tracker tracker;
        QVERIFY(tracker.take(
            Read(tracker, "None", { { Scene.name, Murmur3("KokiriForest_Child") }, { "PlayerStatus.MaxLife", 12 } })));
        QCOMPARE(Shown(tracker, { "Hearts" }), QStringList{ "Hearts: HeartW_00_ASTC4x4 lit 3" });
    }

    void nothingShowsWhileTheGameBootsBeforeTheTitle() {
        Tracker tracker;
        QVERIFY(!tracker.take(Read(tracker, "None", { { Scene.name, Murmur3("") }, { "PlayerStatus.MaxLife", 12 } })));
        QVERIFY(ShowsNothing(tracker));
    }
};

QTEST_GUILESS_MAIN(TrackerTest)

#include "tracker_test.moc"
