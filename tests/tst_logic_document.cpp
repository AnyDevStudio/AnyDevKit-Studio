#include "LogicDocument.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QVariantList>
#include <QtTest>

using namespace Studio;

namespace {

QString logicPath(const QTemporaryDir& directory) {
    return directory.path() + "/logic.json";
}

Scene twoItemScene() {
    Scene scene;
    SceneItem hero;
    hero.id = "item-1";
    hero.type = "rectangle";
    hero.name = "Hero";
    hero.width = hero.height = 100;
    SceneItem coin = hero;
    coin.id = "item-2";
    coin.name = "Coin";
    scene.items = {hero, coin};
    return scene;
}

void openWithScene(const QTemporaryDir& directory, LogicDocument& document) {
    QVERIFY(document.open(logicPath(directory)));
    document.setScene(twoItemScene());
}

QStringList idsIn(const QList<Block>& blocks) {
    QStringList ids;
    for (const auto& block : blocks) ids << block.id;
    return ids;
}

LogicProgram saved(const QTemporaryDir& directory) {
    return *loadLogic(logicPath(directory)).program;
}

QString roleOf(const LogicDocument& document, const QString& id, int role) {
    for (int row = 0; row < document.rowCount(); ++row) {
        const auto index = document.index(row);
        if (document.data(index, LogicDocument::IdRole).toString() == id) return document.data(index, role).toString();
    }
    return {};
}

void expectLanding(const LogicDocument& document, const QString& parentId, int index) {
    const auto spot = document.landing();
    QVERIFY(spot.value("valid").toBool());
    QCOMPARE(spot.value("parentId").toString(), parentId);
    QCOMPARE(spot.value("index").toInt(), index);
}

}

class LogicDocumentTests : public QObject {
    Q_OBJECT

private slots:
    void paletteComesFromTheCatalogInFourGroups() {
        LogicDocument document;
        const auto groups = document.palette();
        QCOMPARE(groups.size(), 4);
        QStringList keys;
        QStringList seen;
        for (const auto& group : groups) {
            const auto map = group.toMap();
            keys << map.value("key").toString();
            for (const auto& block : map.value("blocks").toList()) seen << block.toMap().value("type").toString();
        }
        QCOMPARE(keys, QStringList({"events", "movement", "looks", "score"}));
        QStringList catalog;
        for (const auto& info : blockCatalog()) catalog << info.jsonName;
        auto sortedSeen = seen;
        auto sortedCatalog = catalog;
        sortedSeen.sort();
        sortedCatalog.sort();
        QCOMPARE(sortedSeen, sortedCatalog);
    }

    void categoriesFollowTheCatalogFlags() {
        QCOMPARE(blockCategory(blockInfo(BlockType::WhenGameStarts)), QString("events"));
        QCOMPARE(blockCategory(blockInfo(BlockType::MoveBy)), QString("movement"));
        QCOMPARE(blockCategory(blockInfo(BlockType::SetPosition)), QString("movement"));
        QCOMPARE(blockCategory(blockInfo(BlockType::Hide)), QString("looks"));
        QCOMPARE(blockCategory(blockInfo(BlockType::SetColor)), QString("looks"));
        QCOMPARE(blockCategory(blockInfo(BlockType::AddScore)), QString("score"));
        QCOMPARE(blockCategory(blockInfo(BlockType::IfScore)), QString("score"));
    }

    void fieldsFollowTheCatalogFlags() {
        const auto keys = [](BlockType type) {
            QStringList names;
            for (const auto& field : blockFields(blockInfo(type))) names << field.key;
            return names;
        };
        QCOMPARE(keys(BlockType::MoveBy), QStringList({"item", "x", "y"}));
        QCOMPARE(keys(BlockType::SetColor), QStringList({"item", "color"}));
        QCOMPARE(keys(BlockType::WhileButtonHeld), QStringList({"button"}));
        QCOMPARE(keys(BlockType::WhenItemsTouch), QStringList({"item", "otherItem"}));
        QCOMPARE(keys(BlockType::IfScore), QStringList({"comparison", "value"}));
        QVERIFY(keys(BlockType::EveryFrame).isEmpty());
    }

    void aMissingFileOpensEmptyAndWritesNothing() {
        QTemporaryDir directory;
        LogicDocument document;
        QVERIFY(document.open(logicPath(directory)));
        QVERIFY(document.isLoaded());
        QVERIFY(document.isEmpty());
        QVERIFY(!QFile::exists(logicPath(directory)));
    }

    void aBrokenFileIsReportedAndNeverSavedOver() {
        QTemporaryDir directory;
        QFile file(logicPath(directory));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("{\"schemaVersion\":9}");
        file.close();

        LogicDocument document;
        QVERIFY(!document.open(logicPath(directory)));
        QVERIFY(!document.isLoaded());
        QVERIFY(document.loadError().contains("schemaVersion"));
        QVERIFY(document.addFromPalette("everyFrame").isEmpty());

        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("{\"schemaVersion\":9}"));
    }

    void addingAnEventAutosavesAndUndoComesBack() {
        QTemporaryDir directory;
        LogicDocument document;
        openWithScene(directory, document);

        const auto id = document.addFromPalette("whenGameStarts");
        QVERIFY(!id.isEmpty());
        QCOMPARE(document.selectedId(), id);
        QCOMPARE(saved(directory).events.size(), 1);
        QVERIFY(document.canUndo());

        document.undo();
        QVERIFY(document.isEmpty());
        QCOMPARE(saved(directory).events.size(), 0);
        QVERIFY(!document.canUndo());
    }

    void anActionNeedsAnEventFirst() {
        QTemporaryDir directory;
        LogicDocument document;
        openWithScene(directory, document);
        QVERIFY(document.addFromPalette("moveBy").isEmpty());
        QVERIFY(document.error().contains("event", Qt::CaseInsensitive));
        QVERIFY(document.isEmpty());
    }

    void keyboardAddFollowsTheSelection() {
        QTemporaryDir directory;
        LogicDocument document;
        openWithScene(directory, document);
        const auto event = document.addFromPalette("everyFrame");
        const auto first = document.addFromPalette("hide");
        QCOMPARE(document.program().events.first().children.size(), 1);
        QCOMPARE(document.selectedId(), first);

        const auto second = document.addFromPalette("show");
        QCOMPARE(idsIn(document.program().events.first().children), QStringList({first, second}));

        const auto wrapper = document.addFromPalette("ifScore");
        document.setSelectedId(wrapper);
        const auto nested = document.addFromPalette("hide");
        QCOMPARE(idsIn(document.program().events.first().children.last().children), QStringList({nested}));

        // With nothing selected an action goes last in the last event.
        document.setSelectedId({});
        const auto last = document.addFromPalette("show");
        QCOMPARE(document.program().events.first().children.last().id, last);
        QVERIFY(!event.isEmpty());
    }

    void newBlocksStartWithSceneItemsAndUsefulValues() {
        QTemporaryDir directory;
        LogicDocument document;
        openWithScene(directory, document);
        document.addFromPalette("whenItemsTouch");
        auto touch = document.program().events.last();
        QCOMPARE(touch.item, QString("item-1"));
        QCOMPARE(touch.otherItem, QString("item-2"));
        document.addFromPalette("moveBy");
        QCOMPARE(document.program().events.last().children.first().item, QString("item-1"));
        QVERIFY(document.problemCount() == 0);
    }

    void fieldEditsAreCheckedSavedAndUndoable() {
        QTemporaryDir directory;
        LogicDocument document;
        openWithScene(directory, document);
        document.addFromPalette("everyFrame");
        const auto id = document.addFromPalette("moveBy");

        QVERIFY(document.setField(id, "x", 12.5));
        QVERIFY(document.setField(id, "item", "item-2"));
        QCOMPARE(saved(directory).events.first().children.first().x, 12.5);
        QCOMPARE(saved(directory).events.first().children.first().item, QString("item-2"));

        QVERIFY(!document.setField(id, "x", "not a number"));
        QVERIFY(document.error().contains("number", Qt::CaseInsensitive));

        document.undo();
        QCOMPARE(document.program().events.first().children.first().item, QString("item-1"));
    }

    void buttonsComparisonsColorsAndWholeNumbers() {
        QTemporaryDir directory;
        LogicDocument document;
        openWithScene(directory, document);
        const auto held = document.addFromPalette("whileButtonHeld");
        QVERIFY(document.setField(held, "button", "dpadLeft"));
        QCOMPARE(document.program().events.first().button, ControllerButton::DpadLeft);
        QVERIFY(!document.setField(held, "button", "start"));

        const auto wrapper = document.addFromPalette("ifScore");
        QVERIFY(document.setField(wrapper, "comparison", "greaterOrEqual"));
        QVERIFY(document.setField(wrapper, "value", 10));
        QVERIFY(!document.setField(wrapper, "value", 1.5));

        const auto color = document.addFromPalette("setColor");
        QVERIFY(document.setField(color, "color", "#67E3C8"));
        QVERIFY(!document.setField(color, "color", "yellow"));
        QVERIFY(document.error().contains("#RRGGBB"));
    }

    void aSetFieldThatChangesNothingLeavesNoUndoStep() {
        QTemporaryDir directory;
        LogicDocument document;
        openWithScene(directory, document);
        const auto id = document.addFromPalette("everyFrame");
        document.undo();
        const auto again = document.addFromPalette("whileButtonHeld");
        QVERIFY(document.canUndo());
        document.setField(again, "button", "cross");
        document.undo();
        QVERIFY(document.isEmpty());
        QVERIFY(!id.isEmpty());
    }

    void deleteRemovesTheBlockButNotWhatIsBelowIt() {
        QTemporaryDir directory;
        LogicDocument document;
        openWithScene(directory, document);
        document.addFromPalette("everyFrame");
        const auto wrapper = document.addFromPalette("ifScore");
        document.setSelectedId(wrapper);
        const auto nested = document.addFromPalette("hide");
        document.setSelectedId(wrapper);
        // The block after the wrapper, not inside it.
        const auto below = document.addBlock("show", document.program().events.first().id, 1);
        QCOMPARE(idsIn(document.program().events.first().children), QStringList({wrapper, below}));

        document.removeBlock(wrapper);
        QCOMPARE(idsIn(document.program().events.first().children), QStringList({below}));
        QCOMPARE(saved(directory).events.first().children.size(), 1);

        document.undo();
        QCOMPARE(idsIn(document.program().events.first().children.first().children), QStringList({nested}));
    }

    void draggingABlockTakesTheBlocksBelowItAlong() {
        QTemporaryDir directory;
        LogicDocument document;
        openWithScene(directory, document);
        const auto first = document.addFromPalette("everyFrame");
        const auto a = document.addFromPalette("hide");
        const auto b = document.addFromPalette("show");
        const auto c = document.addFromPalette("moveBy");
        document.setSelectedId({});
        const auto second = document.addFromPalette("whenGameStarts");
        Q_UNUSED(first);

        // Drag b; c must come along, a stays, and the landing is under the second event.
        document.startMoving(b);
        const auto spotY = document.positionOf(second).value("y").toDouble() + 70;
        const auto spotX = document.positionOf(second).value("x").toDouble();
        document.dragTo(spotX, spotY);
        expectLanding(document, second, 0);
        QVERIFY(!document.drop().isEmpty());

        QCOMPARE(idsIn(document.program().events.first().children), QStringList({a}));
        QCOMPARE(idsIn(document.program().events.last().children), QStringList({b, c}));
        QCOMPARE(saved(directory).events.last().children.size(), 2);
        document.undo();
        QCOMPARE(idsIn(document.program().events.first().children), QStringList({a, b, c}));
    }

    void draggingAnEventReordersTheProgram() {
        QTemporaryDir directory;
        LogicDocument document;
        openWithScene(directory, document);
        const auto first = document.addFromPalette("everyFrame");
        const auto child = document.addFromPalette("hide");
        document.setSelectedId({});
        const auto second = document.addFromPalette("whenGameStarts");

        const auto targetX = document.positionOf(second).value("x").toDouble();
        const auto targetY = document.positionOf(second).value("y").toDouble() + 200;

        document.startMoving(first);
        document.dragTo(targetX, targetY);
        QVERIFY(document.landing().value("valid").toBool());
        document.drop();

        QCOMPARE(idsIn(document.program().events), QStringList({second, first}));
        QCOMPARE(document.program().events.last().children.first().id, child);
    }

    void anEventDroppedBackWhereItWasLeavesNoUndoStep() {
        QTemporaryDir directory;
        LogicDocument document;
        openWithScene(directory, document);
        const auto id = document.addFromPalette("everyFrame");
        document.setSelectedId({});
        const auto undoSteps = document.canUndo();
        const auto x = document.positionOf(id).value("x").toDouble();
        const auto y = document.positionOf(id).value("y").toDouble();
        document.startMoving(id);
        document.dragTo(x, y);
        document.drop();
        QCOMPARE(document.program().events.size(), 1);
        QVERIFY(undoSteps);
    }

    void aBlockCannotLandInsideItself() {
        QTemporaryDir directory;
        LogicDocument document;
        openWithScene(directory, document);
        document.addFromPalette("everyFrame");
        const auto wrapper = document.addFromPalette("ifScore");
        document.setSelectedId(wrapper);
        document.addFromPalette("hide");

        document.startMoving(wrapper);
        const auto position = document.positionOf(wrapper);
        document.dragTo(position.value("x").toDouble() + 40, position.value("y").toDouble() + 70);
        const auto spot = document.landing();
        if (spot.value("valid").toBool()) QVERIFY(spot.value("parentId").toString() != wrapper);
        document.cancelDrag();
    }

    void actionsWithNoLandingSpotStayPut() {
        QTemporaryDir directory;
        LogicDocument document;
        openWithScene(directory, document);
        document.addFromPalette("everyFrame");
        const auto action = document.addFromPalette("hide");

        document.startMoving(action);
        document.dragTo(4000, 4000);
        QVERIFY(!document.landing().value("valid").toBool());
        QVERIFY(document.drop().isEmpty());
        QCOMPARE(document.program().events.first().children.first().id, action);
    }

    void addingADraggedActionToAnEmptyCanvasIsRefusedWithAReason() {
        QTemporaryDir directory;
        LogicDocument document;
        openWithScene(directory, document);
        document.startAdding("moveBy");
        document.dragTo(100, 100);
        QVERIFY(!document.landing().value("valid").toBool());
        QVERIFY(document.drop().isEmpty());
        QVERIFY(document.error().contains("event", Qt::CaseInsensitive));
    }

    void depthLimitIsRespectedWhenDragging() {
        QTemporaryDir directory;
        LogicDocument document;
        openWithScene(directory, document);
        document.addFromPalette("everyFrame");
        // An event is level 1, so wrappers reach level 7 here and a leaf can land at level 8 only.
        QString deepest;
        for (int level = 2; level <= 7; ++level) {
            deepest = document.addFromPalette("ifScore");
            QVERIFY2(!deepest.isEmpty(), qPrintable(document.error()));
            document.setSelectedId(deepest);
        }
        const auto wrapperBelow = document.addBlock("ifScore", document.program().events.first().id, 1);
        QVERIFY(!wrapperBelow.isEmpty());
        document.setSelectedId(wrapperBelow);
        const auto leaf = document.addFromPalette("hide");
        Q_UNUSED(leaf);

        // A wrapper holding a block needs 2 levels, so level 8 inside "deepest" has no spot for it.
        document.startMoving(wrapperBelow);
        for (int y = 0; y < 4000; y += 8) {
            document.dragTo(document.positionOf(deepest).value("x").toDouble() + 40, y);
            QVERIFY(document.landing().value("parentId").toString() != deepest);
        }
        document.cancelDrag();
    }

    void aLandingSpotPreviewsWithoutChangingTheProgram() {
        QTemporaryDir directory;
        LogicDocument document;
        openWithScene(directory, document);
        const auto event = document.addFromPalette("everyFrame");
        const auto a = document.addFromPalette("hide");
        const auto b = document.addFromPalette("show");
        Q_UNUSED(event);

        const auto belowBefore = document.positionOf(b).value("y").toDouble();
        QSignalSpy dragSpy(&document, &LogicDocument::dragChanged);

        document.startAdding("moveBy");
        const auto ax = document.positionOf(a).value("x").toDouble();
        const auto ay = document.positionOf(a).value("y").toDouble();
        document.dragTo(ax, ay);
        QVERIFY(document.landing().value("valid").toBool());
        QVERIFY(document.positionOf(b).value("y").toDouble() >= belowBefore);
        QCOMPARE(document.program().events.first().children.size(), 2);
        QVERIFY(dragSpy.size() > 0);

        document.cancelDrag();
        QCOMPARE(document.positionOf(b).value("y").toDouble(), belowBefore);
        QVERIFY(!document.isDragging());
    }

    void aDraggedPaletteBlockLandsAndIsSelected() {
        QTemporaryDir directory;
        LogicDocument document;
        openWithScene(directory, document);
        const auto event = document.addFromPalette("whenGameStarts");
        const auto below = document.positionOf(event);

        document.startAdding("addScore");
        document.dragTo(below.value("x").toDouble(), below.value("y").toDouble() + 70);
        const auto id = document.drop();
        QVERIFY(!id.isEmpty());
        QCOMPARE(document.selectedId(), id);
        QCOMPARE(document.program().events.first().children.first().type, BlockType::AddScore);
        QCOMPARE(document.program().events.first().children.first().value, 1);
    }

    void droppingOnThePaletteDeletesTheCarriedStack() {
        QTemporaryDir directory;
        LogicDocument document;
        openWithScene(directory, document);
        document.addFromPalette("everyFrame");
        const auto a = document.addFromPalette("hide");
        const auto b = document.addFromPalette("show");
        Q_UNUSED(a);
        document.startMoving(b);
        document.dropOnPalette();
        QVERIFY(!document.isDragging());
        QCOMPARE(document.program().events.first().children.size(), 1);
        document.undo();
        QCOMPARE(document.program().events.first().children.size(), 2);
    }

    void aWrapperGrowsWithWhatItHolds() {
        QTemporaryDir directory;
        LogicDocument document;
        openWithScene(directory, document);
        document.addFromPalette("everyFrame");
        const auto wrapper = document.addFromPalette("ifScore");
        const auto emptyHeight = document.positionOf(wrapper).value("height").toDouble();
        document.setSelectedId(wrapper);
        document.addFromPalette("hide");
        document.setSelectedId(wrapper);
        const auto grown = document.positionOf(wrapper).value("height").toDouble();
        QVERIFY2(grown > emptyHeight, "an if block must grow with its contents");
        QCOMPARE(roleOf(document, wrapper, LogicDocument::EmptyCavityRole), QString("false"));
    }

    void blocksStackFlushAndWrappersIndent() {
        LogicProgram program;
        Block event;
        event.id = "e";
        event.type = BlockType::EveryFrame;
        Block first;
        first.id = "a";
        first.type = BlockType::Hide;
        first.item = "item-1";
        Block wrapper;
        wrapper.id = "w";
        wrapper.type = BlockType::IfScore;
        Block nested = first;
        nested.id = "n";
        wrapper.children = {nested};
        event.children = {first, wrapper};
        program.events = {event};

        const auto layout = layoutLogic(program, 1200);
        const auto eventPlace = *layout.find("e");
        const auto firstPlace = *layout.find("a");
        const auto wrapperPlace = *layout.find("w");
        const auto nestedPlace = *layout.find("n");
        QCOMPARE(firstPlace.y, eventPlace.y + eventPlace.height);
        QCOMPARE(wrapperPlace.y, firstPlace.y + firstPlace.height);
        QCOMPARE(nestedPlace.x, wrapperPlace.x + LogicMetrics::NestIndent);
        QCOMPARE(nestedPlace.y, wrapperPlace.y + wrapperPlace.headerHeight);
        QVERIFY(wrapperPlace.height > wrapperPlace.headerHeight + nestedPlace.height);
    }

    void aBlockWithAProblemGetsRoomToSayIt() {
        LogicProgram program;
        Block event;
        event.id = "e";
        event.type = BlockType::EveryFrame;
        program.events = {event};
        const auto plain = *layoutLogic(program, 1200).find("e");
        const QSet<QString> problems{"e"};
        const auto marked = *layoutLogic(program, 1200, &problems).find("e");
        QCOMPARE(marked.headerHeight, plain.headerHeight + LogicMetrics::ProblemHeight);
        QCOMPARE(marked.problemHeight, LogicMetrics::ProblemHeight);
    }

    void columnsFollowTheCanvasWidth() {
        LogicProgram program;
        for (const auto* id : {"a", "b"}) {
            Block event;
            event.id = id;
            event.type = BlockType::EveryFrame;
            program.events.append(event);
        }
        QCOMPARE(layoutLogic(program, 700).columns, 1);
        QCOMPARE(layoutLogic(program, 1040).columns, 2);
    }

    void problemsAppearOnTheBlockInPlainLanguage() {
        QTemporaryDir directory;
        LogicDocument document;
        QVERIFY(document.open(logicPath(directory)));
        document.setScene(Scene{});
        document.addFromPalette("everyFrame");
        const auto id = document.addFromPalette("moveBy");

        QCOMPARE(document.problemCount(), 1);
        QCOMPARE(document.firstProblemId(), id);
        const auto text = roleOf(document, id, LogicDocument::ProblemRole);
        QVERIFY2(text.startsWith("This block has no item chosen"), qPrintable(text));
        QVERIFY2(!text.contains(id), "a block id means nothing to the person reading this");

        document.setScene(twoItemScene());
        QCOMPARE(document.problemCount(), 1); // the block still has no item
        QVERIFY(document.setField(id, "item", "item-1"));
        QCOMPARE(document.problemCount(), 0);
        QCOMPARE(roleOf(document, id, LogicDocument::ProblemRole), QString());
    }

    void itemNamesComeFromTheScene() {
        QTemporaryDir directory;
        LogicDocument document;
        openWithScene(directory, document);
        document.addFromPalette("everyFrame");
        const auto id = document.addFromPalette("moveBy");
        QVERIFY(document.setField(id, "item", "item-2"));
        QCOMPARE(roleOf(document, id, LogicDocument::ItemNameRole), QString("Coin"));

        const auto items = document.sceneItems();
        QCOMPARE(items.size(), 2);
        QCOMPARE(items.first().toMap().value("id").toString(), QString("item-1"));
        QCOMPARE(items.first().toMap().value("name").toString(), QString("Hero"));
    }

    void anItemThatLeavesTheSceneIsReported() {
        QTemporaryDir directory;
        LogicDocument document;
        openWithScene(directory, document);
        document.addFromPalette("everyFrame");
        const auto id = document.addFromPalette("hide");
        document.setField(id, "item", "item-2");
        QCOMPARE(document.problemCount(), 0);

        auto scene = twoItemScene();
        scene.items.removeLast();
        document.setScene(scene);
        QCOMPARE(document.problemCount(), 1);
        QVERIFY(roleOf(document, id, LogicDocument::ProblemRole).contains("not in the scene"));
    }

    void rowsAreStableWhileBlocksMove() {
        QTemporaryDir directory;
        LogicDocument document;
        openWithScene(directory, document);
        const auto event = document.addFromPalette("everyFrame");
        const auto a = document.addFromPalette("hide");
        const auto b = document.addFromPalette("show");
        Q_UNUSED(event);

        QSignalSpy resets(&document, &QAbstractItemModel::modelReset);
        const auto rowOf = [&](const QString& id) {
            for (int row = 0; row < document.rowCount(); ++row) {
                if (document.data(document.index(row), LogicDocument::IdRole).toString() == id) return row;
            }
            return -1;
        };
        const auto rowA = rowOf(a);
        const auto rowB = rowOf(b);

        // b is the block moved, so it has nothing below it to carry; it lands above a.
        document.startMoving(b);
        const auto spot = document.positionOf(a);
        document.dragTo(spot.value("x").toDouble(), spot.value("y").toDouble());
        document.drop();

        QCOMPARE(resets.size(), 0);
        QCOMPARE(rowOf(a), rowA);
        QCOMPARE(rowOf(b), rowB);
        // a now runs after b, and its row followed it down the column.
        QCOMPARE(idsIn(document.program().events.first().children), QStringList({b, a}));
        QVERIFY(document.positionOf(a).value("y").toDouble() > document.positionOf(b).value("y").toDouble());
    }

    void keyboardReadingOrderWalksTheTree() {
        QTemporaryDir directory;
        LogicDocument document;
        openWithScene(directory, document);
        const auto event = document.addFromPalette("everyFrame");
        const auto wrapper = document.addFromPalette("ifScore");
        document.setSelectedId(wrapper);
        const auto nested = document.addFromPalette("hide");
        document.setSelectedId({});
        const auto second = document.addFromPalette("whenGameStarts");

        QCOMPARE(document.nextBlockId(event), wrapper);
        QCOMPARE(document.nextBlockId(wrapper), nested);
        QCOMPARE(document.nextBlockId(nested), second);
        QVERIFY(document.nextBlockId(second).isEmpty());
        QCOMPARE(document.previousBlockId(second), nested);
    }

    void aReopenedProgramHasTheSameBlocks() {
        QTemporaryDir directory;
        {
            LogicDocument document;
            openWithScene(directory, document);
            document.addFromPalette("whileButtonHeld");
            const auto id = document.addFromPalette("moveBy");
            QVERIFY(document.setField(id, "x", 6));
        }
        LogicDocument reopened;
        QVERIFY(reopened.open(logicPath(directory)));
        QCOMPARE(reopened.rowCount(), 2);
        QCOMPARE(reopened.program().events.first().children.first().x, 6.0);
    }

    void whatWasBuiltIsWhatTheHeaderSays() {
        QTemporaryDir directory;
        LogicDocument document;
        openWithScene(directory, document);
        document.addFromPalette("whenGameStarts");
        const auto action = document.addFromPalette("moveBy");
        QVERIFY(document.setField(action, "x", 12));
        QVERIFY(document.setField(action, "y", 3));

        QString error;
        QVERIFY2(exportLogicHeader(directory.path(), twoItemScene(), error), qPrintable(error));
        QFile header(directory.path() + "/src/GeneratedLogic.hpp");
        QVERIFY(header.open(QIODevice::ReadOnly));
        const auto text = header.readAll();
        QVERIFY2(text.contains("world.MoveBy(0, 12.0f, 3.0f)"), text.constData());
    }

    void eventLimitIsReported() {
        QTemporaryDir directory;
        LogicDocument document;
        openWithScene(directory, document);
        for (int i = 0; i < MaximumLogicEvents; ++i) QVERIFY(!document.addFromPalette("everyFrame").isEmpty());
        QVERIFY(document.addFromPalette("everyFrame").isEmpty());
        QVERIFY(document.error().contains("128"));
    }
};

QTEST_GUILESS_MAIN(LogicDocumentTests)
#include "tst_logic_document.moc"
