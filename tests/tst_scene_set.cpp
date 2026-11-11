// Several scenes in one project, item physics, migrations from older files and the budgets that apply per scene.
#include "Scene.h"
#include "SceneDocument.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace Studio;

namespace {

SceneItem rectangle(const QString& id, double x, double y, double width = 100, double height = 100) {
    SceneItem item;
    item.id = id;
    item.type = "rectangle";
    item.name = id;
    item.x = x;
    item.y = y;
    item.width = width;
    item.height = height;
    return item;
}

SceneItem body(const QString& id, double x = 100, double y = 100) {
    auto item = rectangle(id, x, y, 64, 64);
    item.physics = "body";
    item.platformer = true;
    return item;
}

SceneItem solid(const QString& id, double x, double y, double width, double height) {
    auto item = rectangle(id, x, y, width, height);
    item.physics = "solid";
    return item;
}

Scene sceneNamed(const QString& id, const QString& name) {
    Scene scene;
    scene.id = id;
    scene.name = name;
    return scene;
}

SceneSet twoScenes() {
    SceneSet set;
    auto one = sceneNamed("level-a", "Level A");
    one.items = {body("hero"), solid("floor", 0, 900, 1920, 100)};
    auto two = sceneNamed("level-b", "Level B");
    two.background = "#225522";
    two.worldWidth = 3840;
    two.items = {rectangle("door", 300, 400)};
    set.scenes = {one, two};
    set.startScene = "level-b";
    return set;
}

QByteArray readBytes(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

void writeBytes(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(bytes);
}

constexpr auto SingleSceneFileV5 = "{\n  \"schemaVersion\": 5,\n  \"background\": \"#101010\",\n  \"world\": {\"width\": 3840, \"height\": 1080},\n  \"items\": [\n    {\"id\": \"item-1\", \"type\": \"rectangle\", \"name\": \"Box\", \"x\": 10, \"y\": 20, \"width\": 30, \"height\": 40, \"color\": \"#FFD23F\", \"controllable\": true}\n  ]\n}";

constexpr auto SchemaFivePhysics = "{\"schemaVersion\":5,\"background\":\"#101010\",\"items\":[\n  {\"id\":\"a\",\"type\":\"rectangle\",\"name\":\"A\",\"x\":0,\"y\":0,\"width\":10,\"height\":10,\"color\":\"#FFD23F\",\"controllable\":false,\"physics\":\"solid\"}]}";

}

class TstSceneSet : public QObject {
    Q_OBJECT

private slots:
    void anOlderSingleSceneFileLoadsAsOneScene() {
        QTemporaryDir directory;
        const auto path = directory.path() + "/scene.json";
        writeBytes(path, SingleSceneFileV5);
        const auto loaded = loadSceneSet(path);
        QVERIFY2(loaded.set.has_value(), qPrintable(loaded.error));
        QCOMPARE(loaded.set->scenes.size(), 1);
        const auto& only = loaded.set->scenes.first();
        QVERIFY(!only.id.isEmpty());
        QCOMPARE(loaded.set->startScene, only.id);
        QCOMPARE(only.background, QString("#101010"));
        QCOMPARE(only.worldWidth, 3840);
        QCOMPARE(only.items.size(), 1);
        QVERIFY(only.items.first().controllable);
    }

    void loadSceneStillReturnsTheScene() {
        QTemporaryDir directory;
        const auto path = directory.path() + "/scene.json";
        writeBytes(path, SingleSceneFileV5);
        const auto loaded = loadScene(path);
        QVERIFY2(loaded.scene.has_value(), qPrintable(loaded.error));
        QCOMPARE(loaded.scene->items.size(), 1);
    }

    void aSetRoundTripsWithItsStartScene() {
        QTemporaryDir directory;
        const auto path = directory.path() + "/scene.json";
        QString error;
        QVERIFY2(saveSceneSet(path, twoScenes(), error), qPrintable(error));
        const auto root = QJsonDocument::fromJson(readBytes(path)).object();
        QCOMPARE(root.value("schemaVersion").toInt(), 6);
        QCOMPARE(root.value("scenes").toArray().size(), 2);
        QCOMPARE(root.value("startScene").toString(), QString("level-b"));
        QVERIFY(!root.contains("items"));

        const auto loaded = loadSceneSet(path);
        QVERIFY2(loaded.set.has_value(), qPrintable(loaded.error));
        QCOMPARE(loaded.set->startScene, QString("level-b"));
        QCOMPARE(loaded.set->scenes.size(), 2);
        QCOMPARE(loaded.set->scenes[0].name, QString("Level A"));
        QCOMPARE(loaded.set->scenes[1].id, QString("level-b"));
        QCOMPARE(loaded.set->scenes[1].background, QString("#225522"));
        QCOMPARE(loaded.set->scenes[1].worldWidth, 3840);
        QCOMPARE(loaded.set->scenes[0].items[0].physics, QString("body"));
        QVERIFY(loaded.set->scenes[0].items[0].platformer);
        QCOMPARE(loaded.set->scenes[0].items[1].physics, QString("solid"));
    }

    void loadSceneReturnsTheStartScene() {
        QTemporaryDir directory;
        const auto path = directory.path() + "/scene.json";
        QString error;
        QVERIFY(saveSceneSet(path, twoScenes(), error));
        const auto loaded = loadScene(path);
        QVERIFY(loaded.scene.has_value());
        QCOMPARE(loaded.scene->id, QString("level-b"));
    }

    void saveSceneWritesTheCurrentFormat() {
        QTemporaryDir directory;
        const auto path = directory.path() + "/scene.json";
        Scene scene;
        scene.items = {rectangle("a", 0, 0)};
        QString error;
        QVERIFY2(saveScene(path, scene, error), qPrintable(error));
        const auto root = QJsonDocument::fromJson(readBytes(path)).object();
        QCOMPARE(root.value("schemaVersion").toInt(), 6);
        QCOMPARE(root.value("scenes").toArray().size(), 1);
        const auto again = loadScene(path);
        QVERIFY2(again.scene.has_value(), qPrintable(again.error));
        QCOMPARE(again.scene->items.size(), 1);
    }

    void bodySettingsRoundTripAndDefaultsAreKept() {
        QTemporaryDir directory;
        const auto path = directory.path() + "/scene.json";
        Scene scene;
        auto hero = body("hero");
        hero.gravityScale = 80;
        hero.moveSpeed = 500;
        hero.jumpHeight = 300;
        scene.items = {hero, solid("floor", 0, 900, 1920, 100), rectangle("plain", 300, 300)};
        QString error;
        QVERIFY2(saveScene(path, scene, error), qPrintable(error));
        const auto items = QJsonDocument::fromJson(readBytes(path)).object().value("scenes").toArray().first().toObject().value("items").toArray();
        QCOMPARE(items[0].toObject().value("physics").toString(), QString("body"));
        QCOMPARE(items[0].toObject().value("gravityScale").toInt(), 80);
        QCOMPARE(items[0].toObject().value("moveSpeed").toInt(), 500);
        QCOMPARE(items[0].toObject().value("jumpHeight").toInt(), 300);
        QVERIFY(items[0].toObject().value("platformer").toBool());
        QVERIFY(!items[1].toObject().contains("gravityScale"));
        QVERIFY(!items[2].toObject().contains("physics"));

        const auto loaded = loadScene(path);
        QVERIFY2(loaded.scene.has_value(), qPrintable(loaded.error));
        QCOMPARE(loaded.scene->items[0].gravityScale, 80);
        QCOMPARE(loaded.scene->items[0].moveSpeed, 500);
        QCOMPARE(loaded.scene->items[0].jumpHeight, 300);
        QCOMPARE(loaded.scene->items[2].physics, QString());
    }

    void aBodyWithoutSettingsGetsFriendlyDefaults() {
        QTemporaryDir directory;
        const auto path = directory.path() + "/scene.json";
        writeBytes(path, "{\"schemaVersion\":6,\"startScene\":\"s\",\"scenes\":[{\"id\":\"s\",\"name\":\"S\",\"background\":\"#101010\",\"items\":[\n            {\"id\":\"a\",\"type\":\"rectangle\",\"name\":\"A\",\"x\":0,\"y\":0,\"width\":10,\"height\":10,\"color\":\"#FFD23F\",\"controllable\":false,\"physics\":\"body\"}]}]}");
        const auto loaded = loadScene(path);
        QVERIFY2(loaded.scene.has_value(), qPrintable(loaded.error));
        const auto& item = loaded.scene->items.first();
        QCOMPARE(item.gravityScale, DefaultGravityScale);
        QCOMPARE(item.moveSpeed, DefaultMoveSpeed);
        QCOMPARE(item.jumpHeight, DefaultJumpHeight);
        QVERIFY(!item.platformer);
    }

    void physicsNeedsTheNewSchema() {
        QTemporaryDir directory;
        const auto path = directory.path() + "/scene.json";
        writeBytes(path, SchemaFivePhysics);
        const auto loaded = loadScene(path);
        QVERIFY(!loaded.scene.has_value());
        QVERIFY2(loaded.error.contains("schemaVersion 6"), qPrintable(loaded.error));
    }

    void aSchemaSixFileNeedsItsScenesList() {
        QTemporaryDir directory;
        const auto path = directory.path() + "/scene.json";
        writeBytes(path, "{\"schemaVersion\":6,\"background\":\"#101010\",\"items\":[]}");
        const auto loaded = loadScene(path);
        QVERIFY(!loaded.scene.has_value());
        QVERIFY2(loaded.error.contains("scenes"), qPrintable(loaded.error));
    }

    void onlyShapesAndImagesCanHavePhysics() {
        SceneItem text;
        text.id = "t";
        text.type = "text";
        text.text = "Hi";
        text.physics = "solid";
        Scene scene;
        scene.items = {text};
        QVERIFY2(validateScene(scene).contains("only a shape or an image"), qPrintable(validateScene(scene)));
    }

    void physicsRulesHaveClearMessages() {
        auto item = body("a");
        item.controllable = true;
        Scene scene;
        scene.items = {item};
        QVERIFY2(validateScene(scene).contains("Moves with the controller"), qPrintable(validateScene(scene)));

        item = body("a");
        item.screenFixed = true;
        scene.items = {item};
        QVERIFY2(validateScene(scene).contains("Stays on screen"), qPrintable(validateScene(scene)));

        item = body("a");
        item.physics = "ghost";
        scene.items = {item};
        QVERIFY(validateScene(scene).contains("physics must be"));

        item = body("a");
        item.width = MaximumBodySide + 1;
        scene.items = {item};
        QVERIFY2(validateScene(scene).contains("at most 2048"), qPrintable(validateScene(scene)));

        item = body("a");
        item.gravityScale = 301;
        scene.items = {item};
        QVERIFY2(validateScene(scene).contains("gravity scale"), qPrintable(validateScene(scene)));

        item = body("a");
        item.moveSpeed = 10;
        scene.items = {item};
        QVERIFY2(validateScene(scene).contains("speed"), qPrintable(validateScene(scene)));

        item = body("a");
        item.jumpHeight = 1;
        scene.items = {item};
        QVERIFY2(validateScene(scene).contains("jump height"), qPrintable(validateScene(scene)));
    }

    void aSceneHasAtMost128MovingBodiesAnd256Solids() {
        Scene scene;
        for (int index = 0; index < 129; ++index) scene.items.append(body(QString("b%1").arg(index)));
        QVERIFY2(validateScene(scene).contains("at most 128"), qPrintable(validateScene(scene)));

        scene.items.clear();
        for (int index = 0; index < 257; ++index) scene.items.append(solid(QString("s%1").arg(index), index * 10, 0, 8, 8));
        QVERIFY2(validateScene(scene).contains("at most 256"), qPrintable(validateScene(scene)));

        scene.items.clear();
        for (int index = 0; index < 128; ++index) scene.items.append(body(QString("b%1").arg(index)));
        for (int index = 0; index < 256; ++index) scene.items.append(solid(QString("s%1").arg(index), index * 10, 0, 8, 8));
        QCOMPARE(validateScene(scene), QString());
    }

    void budgetsApplyToEachSceneOnItsOwn() {
        SceneSet set;
        for (int scene = 0; scene < 3; ++scene) {
            Scene current = sceneNamed(QString("s-%1").arg(scene), QString("Scene %1").arg(scene));
            for (int index = 0; index < 64; ++index) current.items.append(body(QString("b%1").arg(index)));
            for (int index = 0; index < 100; ++index) current.items.append(solid(QString("c%1").arg(index), index * 10, 500, 8, 8));
            set.scenes.append(current);
        }
        set.startScene = "s-0";
        QCOMPARE(validateSceneSet(set), QString());
    }

    void setValidationHasClearMessages() {
        auto set = twoScenes();
        set.scenes[1].id = set.scenes[0].id;
        QVERIFY2(validateSceneSet(set).contains("level-a"), qPrintable(validateSceneSet(set)));

        set = twoScenes();
        set.scenes[1].name = "LEVEL A";
        QVERIFY2(validateSceneSet(set).contains("name"), qPrintable(validateSceneSet(set)));

        set = twoScenes();
        set.scenes[1].name.clear();
        QVERIFY(!validateSceneSet(set).isEmpty());

        set = twoScenes();
        set.startScene = "nope";
        QVERIFY2(validateSceneSet(set).contains("start"), qPrintable(validateSceneSet(set)));

        set = twoScenes();
        set.scenes[0].id = "bad id!";
        QVERIFY(!validateSceneSet(set).isEmpty());

        set = {};
        QVERIFY(!validateSceneSet(set).isEmpty());

        set = {};
        for (int index = 0; index < MaximumScenes + 1; ++index) set.scenes.append(sceneNamed(QString("s%1").arg(index), QString("Scene %1").arg(index)));
        set.startScene = "s0";
        QVERIFY2(validateSceneSet(set).contains("32"), qPrintable(validateSceneSet(set)));

        set = twoScenes();
        set.scenes[0].items.append(body("hero"));
        QVERIFY(validateSceneSet(set).contains("hero"));
    }

    void imageSourcesAcrossScenesComeOnce() {
        SceneSet set;
        auto one = sceneNamed("a", "A");
        auto two = sceneNamed("b", "B");
        SceneItem image;
        image.id = "i";
        image.type = "image";
        image.source = "assets/a.png";
        image.width = image.height = 10;
        one.items = {image};
        image.id = "j";
        two.items = {image};
        image.id = "k";
        image.source = "assets/b.png";
        two.items.append(image);
        set.scenes = {one, two};
        set.startScene = "a";
        QCOMPARE(sceneImageSources(set), QStringList({"assets/a.png", "assets/b.png"}));
    }

    void theHeaderListsScenesAsSlicesOfTheItems() {
        const auto header = generateSceneHeader(twoScenes());
        QVERIFY2(!header.isEmpty(), "no header");
        QVERIFY2(header.contains("inline constexpr std::size_t SceneCount = 2;"), header.constData());
        QVERIFY2(header.contains("inline constexpr std::size_t StartScene = 1;"), header.constData());
        QVERIFY2(header.contains("Items.size() == 3") || header.contains("std::array<Item, 3> Items"), header.constData());
        // Scene 0 owns items 0..1, scene 1 owns the door at index 2 with a 3840 wide world.
        QVERIFY2(header.contains("{0, 2, 1920.0f, 1080.0f"), header.constData());
        QVERIFY2(header.contains("{2, 1, 3840.0f, 1080.0f"), header.constData());
    }

    void theHeaderHasABodyAndASolidTable() {
        const auto header = generateSceneHeader(twoScenes());
        QVERIFY2(header.contains("BodyCount = 1;"), header.constData());
        QVERIFY2(header.contains("SolidItemCount = 1;"), header.constData());
        QVERIFY2(header.contains("{100, 600, 260, true}"), header.constData());
    }

    void aGeneratedSceneHasNoBodiesOrSolidsWhenNotUsed() {
        const auto header = generateSceneHeader(Scene{});
        QVERIFY2(header.contains("inline constexpr std::size_t SceneCount = 1;"), header.constData());
        QVERIFY2(header.contains("inline constexpr std::size_t BodyCount = 0;"), header.constData());
        QVERIFY2(header.contains("inline constexpr std::array<Body, 0> Bodies{};"), header.constData());
    }

    void aSetWithPhysicsNeedsARuntimeThatKnowsIt() {
        QVERIFY(sceneSetUsesPhysics(twoScenes()));
        SceneSet set;
        set.scenes = {Scene{}};
        set.scenes.first().items = {rectangle("a", 0, 0)};
        set.startScene = set.scenes.first().id;
        QVERIFY(!sceneSetUsesPhysics(set));
        set.scenes.append(sceneNamed("two", "Two"));
        QVERIFY2(sceneSetUsesPhysics(set), "a second scene needs the new runtime");

        QTemporaryDir directory;
        QVERIFY(QDir(directory.path()).mkpath("include/Scene"));
        writeBytes(directory.path() + "/include/Scene/SceneState.hpp", "#include \"Scene/World2D.hpp\"");
        QVERIFY(sceneRuntimeLacksPhysics(directory.path()));
        writeBytes(directory.path() + "/include/Scene/SceneState.hpp", "#include \"Scene/Physics2D.hpp\"");
        QVERIFY2(sceneRuntimeLacksPhysics(directory.path()), "a runtime from before the top-down blocks needs replacing");
        writeBytes(directory.path() + "/include/Scene/SceneState.hpp", "#include \"Scene/Physics2D.hpp\"\nbool Pressed(unsigned button) const;");
        QVERIFY2(sceneRuntimeLacksPhysics(directory.path()), "a runtime from before the tilted camera needs replacing");
        writeBytes(directory.path() + "/include/Scene/World2D.hpp", "constexpr Point TiltProject();");
        QVERIFY2(sceneRuntimeLacksPhysics(directory.path()), "a runtime from before pausing and the second player needs replacing");
        writeBytes(directory.path() + "/include/Scene/SceneState.hpp", "#include \"Scene/Physics2D.hpp\"\nbool Pressed(unsigned button) const;\nvoid PauseGame();");
        QVERIFY2(sceneRuntimeLacksPhysics(directory.path()), "a runtime from before players and copies needs replacing");
        writeBytes(directory.path() + "/include/Scene/SceneState.hpp", "#include \"Scene/Physics2D.hpp\"\nbool Pressed(unsigned button) const;\nvoid PauseGame();\nstd::size_t SpawnCopy();");
        QVERIFY2(sceneRuntimeLacksPhysics(directory.path()), "a runtime from before playing online needs replacing");
        writeBytes(directory.path() + "/include/Scene/SceneState.hpp", "#include \"Scene/Physics2D.hpp\"\nbool Pressed(unsigned button) const;\nvoid PauseGame();\nstd::size_t SpawnCopy();\nstd::size_t OpenScreen();");
        QVERIFY(!sceneRuntimeLacksPhysics(directory.path()));
    }

    // The document in the Scene tab: a scene list to add, rename, duplicate, delete and pick the start scene.
    void addingASceneNamesItAndSwitchesToIt() {
        QTemporaryDir directory;
        const auto path = directory.path() + "/scene.json";
        QString error;
        QVERIFY(saveSceneSet(path, twoScenes(), error));
        SceneDocument document;
        QVERIFY(document.open(path));
        QCOMPARE(document.sceneList().size(), 2);
        QCOMPARE(document.startSceneIndex(), 1);
        QCOMPARE(document.currentSceneIndex(), 1); // opens the start scene first

        QSignalSpy resets(&document, &QAbstractItemModel::modelReset);
        const auto newIndex = document.addScene();
        QCOMPARE(newIndex, 2);
        QCOMPARE(document.currentSceneIndex(), 2);
        QCOMPARE(document.count(), 0);
        QVERIFY(resets.count() >= 1);
        const auto list = document.sceneList();
        QCOMPARE(list.size(), 3);
        QVERIFY(list[2].toMap().value("name").toString().startsWith("Scene"));

        const auto root = QJsonDocument::fromJson(readBytes(path)).object();
        QCOMPARE(root.value("scenes").toArray().size(), 3);
    }

    void renamingASceneKeepsItsIdAndRefusesDuplicates() {
        QTemporaryDir directory;
        const auto path = directory.path() + "/scene.json";
        QString error;
        QVERIFY(saveSceneSet(path, twoScenes(), error));
        SceneDocument document;
        QVERIFY(document.open(path));
        QVERIFY(document.renameScene(0, "Forest"));
        QCOMPARE(document.sceneList().first().toMap().value("name").toString(), QString("Forest"));
        QCOMPARE(document.sceneList().first().toMap().value("id").toString(), QString("level-a"));
        QVERIFY(!document.renameScene(1, "Forest"));
        QVERIFY(!document.error().isEmpty());
    }

    void duplicatingASceneCopiesItsItemsWithANewId() {
        QTemporaryDir directory;
        const auto path = directory.path() + "/scene.json";
        QString error;
        QVERIFY(saveSceneSet(path, twoScenes(), error));
        SceneDocument document;
        QVERIFY(document.open(path));
        QSignalSpy duplicated(&document, &SceneDocument::sceneDuplicated);
        const auto index = document.duplicateScene(0);
        QCOMPARE(index, 1);
        QCOMPARE(document.currentSceneIndex(), 1);
        QCOMPARE(document.sceneList().size(), 3);
        const auto copyId = document.sceneList()[1].toMap().value("id").toString();
        QVERIFY(copyId != "level-a");
        QCOMPARE(document.sceneSet().scenes[1].items.size(), 2);
        QCOMPARE(document.sceneSet().scenes[1].items[0].physics, QString("body"));
        QCOMPARE(duplicated.count(), 1);
        QCOMPARE(duplicated.first().at(0).toString(), QString("level-a"));
        QCOMPARE(duplicated.first().at(1).toString(), copyId);
    }

    void aSceneCannotBeDeletedWhenItIsTheLastOne() {
        QTemporaryDir directory;
        const auto path = directory.path() + "/scene.json";
        Scene scene;
        QString error;
        QVERIFY(saveScene(path, scene, error));
        SceneDocument document;
        QVERIFY(document.open(path));
        QVERIFY(!document.deleteScene(0));
        QVERIFY(document.error().contains("last"));
    }

    void deletingTheStartSceneMovesTheStartToAnother() {
        QTemporaryDir directory;
        const auto path = directory.path() + "/scene.json";
        QString error;
        QVERIFY(saveSceneSet(path, twoScenes(), error));
        SceneDocument document;
        QVERIFY(document.open(path));
        QVERIFY(document.deleteScene(1));
        QCOMPARE(document.sceneList().size(), 1);
        QCOMPARE(document.startSceneIndex(), 0);
        QCOMPARE(document.currentSceneIndex(), 0);
        const auto root = QJsonDocument::fromJson(readBytes(path)).object();
        QCOMPARE(root.value("startScene").toString(), QString("level-a"));
    }

    void choosingTheStartSceneAndSwitchingScenes() {
        QTemporaryDir directory;
        const auto path = directory.path() + "/scene.json";
        QString error;
        QVERIFY(saveSceneSet(path, twoScenes(), error));
        SceneDocument document;
        QVERIFY(document.open(path));
        QVERIFY(document.setStartScene(0));
        QCOMPARE(document.startSceneIndex(), 0);
        QVERIFY(document.selectScene(0));
        QCOMPARE(document.count(), 2);
        QVERIFY(document.selectScene(1));
        QCOMPARE(document.count(), 1);
        QCOMPARE(document.selectedIndex(), -1);
        QVERIFY(!document.selectScene(9));
    }

    void aDuplicateCopiesAWholeSceneButNotPastTheLimit() {
        QTemporaryDir directory;
        const auto path = directory.path() + "/scene.json";
        QString error;
        SceneSet set = twoScenes();
        for (int index = set.scenes.size(); index < MaximumScenes; ++index) set.scenes.append(sceneNamed(QString("s%1").arg(index), QString("Scene %1").arg(index)));
        QVERIFY(saveSceneSet(path, set, error));
        SceneDocument document;
        QVERIFY(document.open(path));
        QCOMPARE(document.duplicateScene(0), -1);
        QCOMPARE(document.addScene(), -1);
        QVERIFY(document.error().contains("32"));
    }

    void undoRestoresScenesAndTheOneBeingEdited() {
        QTemporaryDir directory;
        const auto path = directory.path() + "/scene.json";
        QString error;
        QVERIFY(saveSceneSet(path, twoScenes(), error));
        SceneDocument document;
        QVERIFY(document.open(path));
        QVERIFY(document.selectScene(0));
        QVERIFY(document.deleteScene(1));
        QCOMPARE(document.sceneList().size(), 1);
        document.undo();
        QCOMPARE(document.sceneList().size(), 2);
        QCOMPARE(document.sceneSet().scenes[1].id, QString("level-b"));
    }

    void editsToAnItemInOneSceneDoNotTouchAnother() {
        QTemporaryDir directory;
        const auto path = directory.path() + "/scene.json";
        QString error;
        QVERIFY(saveSceneSet(path, twoScenes(), error));
        SceneDocument document;
        QVERIFY(document.open(path));
        QVERIFY(document.selectScene(0));
        document.setSelectedIndex(0);
        QVERIFY(document.setItemValue(0, "name", "Champion"));
        QVERIFY(document.selectScene(1));
        QCOMPARE(document.data(document.index(0), SceneDocument::NameRole).toString(), QString("door"));
        QVERIFY(document.selectScene(0));
        QCOMPARE(document.data(document.index(0), SceneDocument::NameRole).toString(), QString("Champion"));
        const auto saved = loadSceneSet(path);
        QVERIFY(saved.set.has_value());
        QCOMPARE(saved.set->scenes[0].items[0].name, QString("Champion"));
    }

    void physicsEditsGoThroughTheInspectorAPI() {
        QTemporaryDir directory;
        const auto path = directory.path() + "/scene.json";
        Scene scene;
        scene.items = {rectangle("a", 0, 0)};
        QString error;
        QVERIFY(saveScene(path, scene, error));
        SceneDocument document;
        QVERIFY(document.open(path));
        document.setSelectedIndex(0);
        QCOMPARE(document.selected().value("physics").toString(), QString());
        QVERIFY(document.setItemValue(0, "physics", "body"));
        QCOMPARE(document.selected().value("physics").toString(), QString("body"));
        QCOMPARE(document.selected().value("gravityScale").toInt(), DefaultGravityScale);
        QVERIFY(document.setItemValue(0, "gravityScale", 150));
        QCOMPARE(document.selected().value("gravityScale").toInt(), 150);
        QVERIFY(document.setItemValue(0, "moveSpeed", 700));
        QCOMPARE(document.selected().value("moveSpeed").toInt(), 700);
        QVERIFY(document.setItemValue(0, "jumpHeight", 320));
        QCOMPARE(document.selected().value("jumpHeight").toInt(), 320);
        QVERIFY(document.setItemValue(0, "platformer", true));
        QVERIFY(document.selected().value("platformer").toBool());
    }

    void turningOnPhysicsTurnsOffTheOldControllerMovement() {
        QTemporaryDir directory;
        const auto path = directory.path() + "/scene.json";
        Scene scene;
        auto item = rectangle("a", 0, 0);
        item.controllable = true;
        scene.items = {item};
        QString error;
        QVERIFY(saveScene(path, scene, error));
        SceneDocument document;
        QVERIFY(document.open(path));
        QVERIFY(document.setItemValue(0, "physics", "body"));
        QVERIFY(!document.scene().items[0].controllable);
        QVERIFY(document.setItemValue(0, "controllable", true));
        QVERIFY(document.scene().items[0].physics.isEmpty());
    }

    void physicsCannotBeTurnedOnForText() {
        QTemporaryDir directory;
        const auto path = directory.path() + "/scene.json";
        SceneItem text;
        text.id = "t";
        text.type = "text";
        text.text = "Hi";
        Scene scene;
        scene.items = {text};
        QString error;
        QVERIFY(saveScene(path, scene, error));
        SceneDocument document;
        QVERIFY(document.open(path));
        QVERIFY(!document.setItemValue(0, "physics", "solid"));
    }

    void theSceneListReadsWellInTheInterface() {
        QTemporaryDir directory;
        const auto path = directory.path() + "/scene.json";
        QString error;
        QVERIFY(saveSceneSet(path, twoScenes(), error));
        SceneDocument document;
        QVERIFY(document.open(path));
        const auto list = document.sceneList();
        QCOMPARE(list.size(), 2);
        QCOMPARE(list[0].toMap().value("name").toString(), QString("Level A"));
        QCOMPARE(list[1].toMap().value("isStart").toBool(), true);
        QCOMPARE(list[0].toMap().value("isStart").toBool(), false);
    }
};

QTEST_MAIN(TstSceneSet)
#include "tst_scene_set.moc"
