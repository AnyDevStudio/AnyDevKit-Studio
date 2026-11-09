// Sprite sheets, tilemaps, the world and the camera: the file format, the document's editing
// operations and the generated code. The runtime half is in tst_world2d.cpp.
#include "Blocks.h"
#include "Scene.h"
#include "SceneDocument.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QVariantList>
#include <QtTest>

using namespace Studio;

namespace {

// A sprite sheet of four 16x16 frames in a 32x32 image, one tilemap of 16x16 tiles, a larger world.
constexpr auto SampleScene = R"json({
  "schemaVersion": 5,
  "background": "#151C3A",
  "world": { "width": 3840, "height": 1080 },
  "camera": { "x": 100, "y": 0 },
  "items": [
    {
      "id": "item-1", "type": "tilemap", "name": "Ground", "x": 0, "y": 0, "controllable": false,
      "source": "assets/tiles.png",
      "tileWidth": 16, "tileHeight": 16, "tileScale": 4,
      "columns": 4, "rows": 2,
      "tiles": ["-1*4", "0,1,-1,3"],
      "solidTiles": [0, 1, 3]
    },
    {
      "id": "item-2", "type": "image", "name": "Hero", "x": 96, "y": 40, "width": 64, "height": 64, "controllable": true,
      "source": "assets/hero.png", "pixelated": true, "flipX": true,
      "sheet": {
        "frameWidth": 16, "frameHeight": 16, "animation": "walk",
        "animations": [
          { "name": "idle", "frames": [0], "fps": 1 },
          { "name": "walk", "frames": [0, 1, 2, 3], "fps": 8 }
        ]
      }
    },
    {
      "id": "item-3", "type": "text", "name": "Score", "x": 16, "y": 16, "scale": 4, "text": "Score", "color": "#FFFFFF",
      "controllable": false, "screenFixed": true
    }
  ]
})json";

constexpr auto SchemaFourText = R"json({"schemaVersion":4,"background":"#151C3A","items":[
    {"id":"a","type":"text","name":"T","x":1,"y":1,"scale":2,"text":"Hi","color":"#FFFFFF"}]})json";
constexpr auto SchemaFourTilemap = R"json({"schemaVersion":4,"background":"#151C3A","items":[
    {"id":"a","type":"tilemap","name":"T","x":1,"y":1,"source":"assets/t.png","tileWidth":16,"tileHeight":16,"tileScale":1,"columns":1,"rows":1,"tiles":["0"]}]})json";
constexpr auto SchemaFourWorld = R"json({"schemaVersion":4,"background":"#151C3A","world":{"width":3840,"height":1080},"items":[]})json";
constexpr auto LogicTwoMoveBy = R"json({"schemaVersion":2,"variables":[],"events":[{"id":"e","type":"everyFrame","actions":[{"id":"b","type":"moveBy","item":"","x":1,"y":2}]}]})json";
constexpr auto LogicTwoCamera = R"json({"schemaVersion":2,"variables":[],"events":[{"id":"e","type":"everyFrame","actions":[{"id":"b","type":"cameraFollow","item":""}]}]})json";

QString sceneFile(const QTemporaryDir& directory) { return directory.filePath("scene.json"); }

void writeText(const QString& path, const QByteArray& text) {
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(text);
}

// A copy of the sample with one thing changed, in a temporary file, loaded.
SceneLoadResult loadChanged(const QByteArray& from, const QByteArray& to) {
    QTemporaryDir directory;
    auto text = QByteArray(SampleScene);
    if (!from.isEmpty()) {
        Q_ASSERT(text.contains(from));
        text.replace(from, to);
    }
    writeText(sceneFile(directory), text);
    return loadScene(sceneFile(directory));
}

SceneItem makeTilemap(const QString& id, const QString& source, int columns, int rows, int cellScale = 4) {
    SceneItem item;
    item.id = id;
    item.type = "tilemap";
    item.name = id;
    item.source = source;
    item.tileWidth = 16;
    item.tileHeight = 16;
    item.tileScale = cellScale;
    item.columns = columns;
    item.rows = rows;
    item.tiles = QList<int>(columns * rows, -1);
    updateTilemapSize(item);
    return item;
}

SceneItem makeSprite(const QString& id, const QString& source) {
    SceneItem item;
    item.id = id;
    item.type = "image";
    item.name = id;
    item.source = source;
    item.width = 64;
    item.height = 64;
    item.pixelated = true;
    item.frameWidth = 16;
    item.frameHeight = 16;
    item.animations = {{"walk", {0, 1, 2, 3}, 8}, {"jump", {2}, 4}};
    item.animation = "walk";
    return item;
}

SceneImageData imageOf(const QString& source, int width, int height) {
    SceneImageData image;
    image.source = source;
    image.width = width;
    image.height = height;
    image.pixels = QByteArray(width * height * 4, '\0');
    return image;
}

void openWithImages(QTemporaryDir& directory, SceneDocument& document, const Scene& scene) {
    QVERIFY(QDir(directory.path()).mkdir("assets"));
    for (const auto& name : {"tiles.png", "hero.png"}) writeText(directory.filePath(QString("assets/") + name), "x");
    QString error;
    QVERIFY2(saveScene(sceneFile(directory), scene, error), qPrintable(error));
    QVERIFY2(document.open(sceneFile(directory)), qPrintable(document.error()));
}

Block makeBlock(const QString& id, BlockType type) {
    Block block;
    block.id = id;
    block.type = type;
    return block;
}

}

class SpritesTilemapTests : public QObject {
    Q_OBJECT

private slots:
    // --- the file format ---

    void loadsTheSample() {
        const auto result = loadChanged({}, {});
        QVERIFY2(result.scene.has_value(), qPrintable(result.error));
        const auto& scene = *result.scene;
        QCOMPARE(scene.worldWidth, 3840);
        QCOMPARE(scene.worldHeight, 1080);
        QCOMPARE(scene.cameraX, 100.0);
        QCOMPARE(scene.cameraY, 0.0);

        const auto& map = scene.items[0];
        QCOMPARE(map.type, QString("tilemap"));
        QCOMPARE(map.tileWidth, 16);
        QCOMPARE(map.tileHeight, 16);
        QCOMPARE(map.tileScale, 4);
        QCOMPARE(map.columns, 4);
        QCOMPARE(map.rows, 2);
        QCOMPARE(map.tiles, QList<int>({-1, -1, -1, -1, 0, 1, -1, 3}));
        QCOMPARE(map.solidTiles, QList<int>({0, 1, 3}));
        QCOMPARE(map.width, 256.0);  // the drawn size comes from the map
        QCOMPARE(map.height, 128.0);

        const auto& hero = scene.items[1];
        QVERIFY(hero.pixelated);
        QVERIFY(hero.flipX);
        QCOMPARE(hero.frameWidth, 16);
        QCOMPARE(hero.animation, QString("walk"));
        QCOMPARE(hero.animations.size(), 2);
        QCOMPARE(hero.animations[1].frames, QList<int>({0, 1, 2, 3}));
        QCOMPARE(hero.animations[1].fps, 8);
        QVERIFY(scene.items[2].screenFixed);
    }

    void savesAndReloadsTheSameScene() {
        const auto scene = *loadChanged({}, {}).scene;
        QTemporaryDir directory;
        QString error;
        QVERIFY2(saveScene(sceneFile(directory), scene, error), qPrintable(error));

        QFile file(sceneFile(directory));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto root = QJsonDocument::fromJson(file.readAll()).object();
        QCOMPARE(root.value("schemaVersion").toInt(), 6);
        // One row per string, so a tilemap reads and diffs by row.
        const auto savedItems = root.value("scenes").toArray()[0].toObject().value("items").toArray();
        QCOMPARE(savedItems[0].toObject().value("tiles").toArray().size(), 2);
        QCOMPARE(savedItems[0].toObject().value("tiles").toArray()[0].toString(), QString("-1*4"));

        const auto reloaded = loadScene(sceneFile(directory));
        QVERIFY2(reloaded.scene.has_value(), qPrintable(reloaded.error));
        QCOMPARE(reloaded.scene->items[0].tiles, scene.items[0].tiles);
        QCOMPARE(reloaded.scene->items[1].animations, scene.items[1].animations);
        QCOMPARE(reloaded.scene->worldWidth, 3840);
        QCOMPARE(reloaded.scene->cameraX, 100.0);
        QVERIFY(reloaded.scene->items[2].screenFixed);
    }

    void worldAndCameraAreOptionalAndDefaultToTheStage() {
        Scene scene;
        QCOMPARE(scene.worldWidth, 1920);
        QCOMPARE(scene.worldHeight, 1080);
        QVERIFY(validateScene(scene).isEmpty());
        QTemporaryDir directory;
        QString error;
        QVERIFY2(saveScene(sceneFile(directory), scene, error), qPrintable(error));
        QFile file(sceneFile(directory));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto root = QJsonDocument::fromJson(file.readAll()).object();
        QVERIFY(!root.contains("world")); // a scene with the stage-sized world saves as before
        QVERIFY(!root.contains("camera"));
    }

    void olderSchemasLoadAndCannotHoldTheNewFeatures() {
        QTemporaryDir directory;
        writeText(sceneFile(directory), SchemaFourText);
        const auto loaded = loadScene(sceneFile(directory));
        QVERIFY2(loaded.scene.has_value(), qPrintable(loaded.error));
        QCOMPARE(loaded.scene->worldWidth, 1920);
        QVERIFY(!loaded.scene->items[0].screenFixed);

        writeText(sceneFile(directory), SchemaFourTilemap);
        const auto refused = loadScene(sceneFile(directory));
        QVERIFY(!refused.scene.has_value());
        QVERIFY2(refused.error.contains("schemaVersion 5"), qPrintable(refused.error));

        writeText(sceneFile(directory), SchemaFourWorld);
        const auto refusedWorld = loadScene(sceneFile(directory));
        QVERIFY(!refusedWorld.scene.has_value());
        QVERIFY2(refusedWorld.error.contains("schemaVersion 5"), qPrintable(refusedWorld.error));
    }

    void refusesEveryBrokenRule_data() {
        QTest::addColumn<QByteArray>("from");
        QTest::addColumn<QByteArray>("to");
        QTest::addColumn<QString>("expected");

        // World and camera
        QTest::newRow("world too narrow") << QByteArray("\"width\": 3840") << QByteArray("\"width\": 1000") << "world";
        QTest::newRow("world too wide") << QByteArray("\"width\": 3840") << QByteArray("\"width\": 99999") << "world";
        QTest::newRow("world fraction") << QByteArray("\"width\": 3840") << QByteArray("\"width\": 3840.5") << "world";
        QTest::newRow("camera out of world") << QByteArray("\"x\": 100, \"y\": 0") << QByteArray("\"x\": 3000, \"y\": 0") << "camera";
        QTest::newRow("camera not numbers") << QByteArray("\"x\": 100, \"y\": 0") << QByteArray("\"x\": \"a\", \"y\": 0") << "camera";

        // Tilemaps
        QTest::newRow("tile too small") << QByteArray("\"tileWidth\": 16") << QByteArray("\"tileWidth\": 2") << "tileWidth";
        QTest::newRow("tile too large") << QByteArray("\"tileHeight\": 16") << QByteArray("\"tileHeight\": 512") << "tileHeight";
        QTest::newRow("scale zero") << QByteArray("\"tileScale\": 4") << QByteArray("\"tileScale\": 0") << "tileScale";
        QTest::newRow("no columns") << QByteArray("\"columns\": 4") << QByteArray("\"columns\": 0") << "columns";
        QTest::newRow("too many rows") << QByteArray("\"rows\": 2") << QByteArray("\"rows\": 513") << "rows";
        QTest::newRow("too few rows given") << QByteArray("\"tiles\": [\"-1*4\", \"0,1,-1,3\"]") << QByteArray("\"tiles\": [\"-1*4\"]") << "tiles";
        QTest::newRow("short row") << QByteArray("\"0,1,-1,3\"") << QByteArray("\"0,1\"") << "tiles";
        QTest::newRow("junk row") << QByteArray("\"0,1,-1,3\"") << QByteArray("\"0,x,-1,3\"") << "tiles";
        QTest::newRow("tile below empty") << QByteArray("\"0,1,-1,3\"") << QByteArray("\"0,1,-2,3\"") << "tile";
        QTest::newRow("tile index too large") << QByteArray("\"0,1,-1,3\"") << QByteArray("\"0,1,70000,3\"") << "tile";
        QTest::newRow("solid out of order") << QByteArray("[0, 1, 3]") << QByteArray("[3, 1]") << "solidTiles";
        QTest::newRow("solid duplicate") << QByteArray("[0, 1, 3]") << QByteArray("[1, 1]") << "solidTiles";
        QTest::newRow("solid negative") << QByteArray("[0, 1, 3]") << QByteArray("[-1]") << "solidTiles";
        QTest::newRow("tilemap source") << QByteArray("assets/tiles.png") << QByteArray("tiles.png") << "source";

        // Sprite sheets
        QTest::newRow("no frame width") << QByteArray("\"frameWidth\": 16") << QByteArray("\"frameWidth\": 0") << "frameWidth";
        QTest::newRow("frame too large") << QByteArray("\"frameHeight\": 16") << QByteArray("\"frameHeight\": 5000") << "frameHeight";
        QTest::newRow("no frames") << QByteArray("\"frames\": [0], \"fps\": 1") << QByteArray("\"frames\": [], \"fps\": 1") << "frames";
        QTest::newRow("negative frame") << QByteArray("\"frames\": [0], \"fps\": 1") << QByteArray("\"frames\": [-1], \"fps\": 1") << "frames";
        QTest::newRow("fps zero") << QByteArray("\"fps\": 1") << QByteArray("\"fps\": 0") << "fps";
        QTest::newRow("fps too fast") << QByteArray("\"fps\": 8") << QByteArray("\"fps\": 61") << "fps";
        QTest::newRow("bad name") << QByteArray("\"name\": \"walk\"") << QByteArray("\"name\": \"walk left\"") << "name";
        QTest::newRow("same name twice") << QByteArray("\"name\": \"walk\"") << QByteArray("\"name\": \"idle\"") << "name";
        QTest::newRow("start not defined") << QByteArray("\"animation\": \"walk\"") << QByteArray("\"animation\": \"run\"") << "animation";
    }

    void refusesEveryBrokenRule() {
        QFETCH(QByteArray, from);
        QFETCH(QByteArray, to);
        QFETCH(QString, expected);
        const auto result = loadChanged(from, to);
        QVERIFY2(!result.scene.has_value(), "a broken scene was accepted");
        QVERIFY2(result.error.contains(expected), qPrintable(result.error));
    }

    void aSpriteSheetIsAnImageWithFrames() {
        auto item = makeSprite("hero", "assets/hero.png");
        QVERIFY(isSpriteSheet(item));
        item.frameWidth = 0;
        item.frameHeight = 0;
        item.animations.clear();
        item.animation.clear();
        QVERIFY(!isSpriteSheet(item));

        Scene scene;
        scene.items = {item};
        QVERIFY2(validateScene(scene).isEmpty(), qPrintable(validateScene(scene)));
        scene.items[0].animations = {{"walk", {0}, 8}}; // an image without frames cannot have animations
        QVERIFY(validateScene(scene).contains("frameWidth"));
    }

    void onlyImagesMayBeFlippedOrPixelated() {
        Scene scene;
        SceneItem rectangle;
        rectangle.id = "a";
        rectangle.type = "rectangle";
        rectangle.width = 10;
        rectangle.height = 10;
        rectangle.flipX = true;
        scene.items = {rectangle};
        QVERIFY(validateScene(scene).contains("flip"));
    }

    void tileRowsAreRunLengthEncoded() {
        const QList<int> tiles{-1, -1, -1, 5, 5, 2, 7, 7, 7, 7};
        QCOMPARE(encodeTileRow(tiles, 0, tiles.size()), QString("-1*3,5*2,2,7*4"));
        QCOMPARE(encodeTileRow(tiles, 3, 2), QString("5*2"));

        QList<int> decoded;
        QVERIFY(decodeTileRow(QString("-1*3,5*2,2,7*4"), tiles.size(), decoded));
        QCOMPARE(decoded, tiles);

        for (const auto* bad : {"", "a", "1,", ",1", "1*", "1*0", "1*2,3", "1*999999", "1**2", "1,2,3 "}) {
            QList<int> out;
            QVERIFY2(!decodeTileRow(QString(bad), 2, out), bad);
        }
    }

    void tilemapDrawnSizeFollowsTheMap() {
        auto item = makeTilemap("map", "assets/tiles.png", 10, 5, 2);
        QCOMPARE(item.width, 320.0);
        QCOMPARE(item.height, 160.0);
        item.tileScale = 3;
        updateTilemapSize(item);
        QCOMPARE(item.width, 480.0);
    }

    void resizingATilemapKeepsItsTiles() {
        auto item = makeTilemap("map", "assets/tiles.png", 3, 2);
        item.tiles = {0, 1, 2, 3, 4, 5};
        resizeTilemap(item, 2, 3);
        QCOMPARE(item.columns, 2);
        QCOMPARE(item.rows, 3);
        QCOMPARE(item.tiles, QList<int>({0, 1, 3, 4, -1, -1}));
        QCOMPARE(item.width, 2 * 64.0);
        QCOMPARE(item.height, 3 * 64.0);
    }

    // --- budgets ---

    void reservesQuadsOnlyForTheVisibleScreen() {
        // A screen of 1920x1080 over cells of 64: 30 + 1 columns, 17 + 1 rows, but no more than the map has.
        QCOMPARE(tilemapReservedQuads(makeTilemap("a", "assets/tiles.png", 500, 500)), 31 * 18);
        QCOMPARE(tilemapReservedQuads(makeTilemap("a", "assets/tiles.png", 10, 4)), 10 * 4);

        Scene scene;
        scene.items = {makeTilemap("a", "assets/tiles.png", 500, 500)};
        QCOMPARE(sceneQuadCount(scene), 1 + 31 * 18);
    }

    void refusesASceneOverTheQuadBudget() {
        Scene scene;
        scene.worldWidth = 8192;
        for (int layer = 0; layer < 3; ++layer) scene.items.append(makeTilemap(QString("a%1").arg(layer), "assets/tiles.png", 512, 80, 1));
        const auto problem = validateScene(scene);
        QVERIFY2(problem.contains("tiles") && problem.contains(QString::number(MaximumSceneBlocks)), qPrintable(problem));
        // Bigger tiles fit in fewer quads.
        for (auto& item : scene.items) {
            item.tileScale = 4;
            updateTilemapSize(item);
        }
        QVERIFY2(validateScene(scene).isEmpty(), qPrintable(validateScene(scene)));
    }

    void refusesTooManyTileCells() {
        Scene scene;
        scene.worldWidth = 32768;
        scene.worldHeight = 32768;
        for (int layer = 0; layer < 5; ++layer) scene.items.append(makeTilemap(QString("a%1").arg(layer), "assets/tiles.png", 512, 512));
        const auto problem = validateScene(scene);
        QVERIFY2(problem.contains(QString::number(MaximumSceneTileCells)), qPrintable(problem));
    }

    void layersThatShareATilesetAreOneDraw() {
        Scene scene;
        scene.items = {makeTilemap("a", "assets/tiles.png", 10, 10), makeTilemap("b", "assets/tiles.png", 10, 10), makeTilemap("c", "assets/other.png", 10, 10)};
        QCOMPARE(sceneDrawCalls(scene), 3); // the background, the shared tileset and the other tileset

        SceneItem rectangle;
        rectangle.id = "r";
        rectangle.type = "rectangle";
        rectangle.width = 10;
        rectangle.height = 10;
        scene.items.insert(1, rectangle);
        QCOMPARE(sceneDrawCalls(scene), 5); // the rectangle paints over the first tilemap, so it splits the shared tileset

        // A sharp sprite can share the tileset's draw when it is the same image.
        auto sprite = makeSprite("s", "assets/tiles.png");
        scene.items = {makeTilemap("a", "assets/tiles.png", 10, 10), sprite};
        QCOMPARE(sceneDrawCalls(scene), 2);
    }

    void refusesAWorldSceneWithTooManyDraws() {
        Scene scene;
        for (int index = 0; index < 40; ++index) {
            auto map = makeTilemap(QString("m%1").arg(index), QString("assets/t%1.png").arg(index), 2, 2);
            scene.items.append(map);
            SceneItem rectangle;
            rectangle.id = QString("r%1").arg(index);
            rectangle.type = "rectangle";
            rectangle.width = 10;
            rectangle.height = 10;
            scene.items.append(rectangle);
        }
        const auto problem = validateScene(scene);
        QVERIFY2(problem.contains("draw") && problem.contains(QString::number(MaximumSceneDrawCalls)), qPrintable(problem));
    }

    void imagesAreCheckedAgainstTheirSheetsAndTilesets() {
        Scene scene;
        scene.items = {makeTilemap("map", "assets/tiles.png", 2, 1), makeSprite("hero", "assets/hero.png")};
        scene.items[0].tiles = {0, 3};
        scene.items[0].solidTiles = {3};

        const QList<SceneImageData> images{imageOf("assets/tiles.png", 32, 32), imageOf("assets/hero.png", 32, 32)};
        QVERIFY2(sceneImagesProblem(scene, images).isEmpty(), qPrintable(sceneImagesProblem(scene, images)));
        QVERIFY(!generateSceneHeader(scene, images).isEmpty());

        auto bigTile = scene;
        bigTile.items[0].tiles = {0, 4}; // the tileset has only 4 tiles
        QVERIFY(sceneImagesProblem(bigTile, images).contains("tile 4"));
        QVERIFY(generateSceneHeader(bigTile, images).isEmpty());

        auto bigFrame = scene;
        bigFrame.items[1].animations[0].frames = {4}; // the sheet has only 4 frames
        QVERIFY(sceneImagesProblem(bigFrame, images).contains("frame 4"));
        QVERIFY(generateSceneHeader(bigFrame, images).isEmpty());

        auto tooBig = scene;
        tooBig.items[1].frameWidth = 64; // the sheet is only 32 wide
        QVERIFY(sceneImagesProblem(tooBig, images).contains("64"));
    }

    void nearTheMaximumTileIndexIsFine() {
        Scene scene;
        auto map = makeTilemap("map", "assets/tiles.png", 2, 1);
        map.tiles = {MaximumTileIndex, -1};
        map.solidTiles = {MaximumTileIndex};
        scene.items = {map};
        QVERIFY2(validateScene(scene).isEmpty(), qPrintable(validateScene(scene)));
    }

    // --- generated code ---

    void generatedHeaderDescribesWorldSpritesAndTilemaps() {
        Scene scene;
        scene.worldWidth = 3840;
        scene.worldHeight = 2160;
        scene.cameraX = 100;
        auto map = makeTilemap("map", "assets/tiles.png", 4, 2);
        map.tiles = {-1, -1, -1, -1, 0, 1, -1, 3};
        map.solidTiles = {0, 3};
        auto hero = makeSprite("hero", "assets/hero.png");
        hero.flipX = true;
        SceneItem score;
        score.id = "score";
        score.type = "text";
        score.text = "Hi";
        score.scale = 2;
        score.screenFixed = true;
        scene.items = {map, hero, score};
        const QList<SceneImageData> images{imageOf("assets/tiles.png", 64, 32), imageOf("assets/hero.png", 32, 32)};

        const auto header = generateSceneHeader(scene, images);
        QVERIFY(!header.isEmpty());
        QVERIFY(header.contains("inline constexpr float WorldWidth = 3840.0f;"));
        QVERIFY(header.contains("inline constexpr float WorldHeight = 2160.0f;"));
        QVERIFY(header.contains("inline constexpr float CameraStartX = 100.0f;"));
        QVERIFY(header.contains("inline constexpr std::size_t TilemapCount = 1;"));
        QVERIFY(header.contains("inline constexpr std::size_t SpriteCount = 1;"));
        // columns, rows, tileWidth, tileHeight, cellWidth, cellHeight, tilesetColumns, image, reservedQuads
        QVERIFY2(header.contains("{4, 2, 16, 16, 64, 64, 4, 0, 8, 0, 2},"), header.constData());
        QVERIFY2(header.contains("inline constexpr std::array<std::uint16_t, 2> SolidTiles{{0, 3}};"), header.constData());
        // frameWidth, frameHeight, columns, rows, firstAnimation, animationCount, startAnimation
        QVERIFY2(header.contains("{16, 16, 2, 2, 0, 2, 0},"), header.constData());
        // first frame, frames, fps
        QVERIFY2(header.contains("{0, 4, 8},"), header.constData());
        QVERIFY2(header.contains("{4, 1, 4},"), header.constData());
        QVERIFY2(header.contains("> AnimationFrames{{0, 1, 2, 3, 2}};"), header.constData());

        // tilemap and sprite item lines
        QVERIFY2(header.contains("{Kind::Tilemap, 0.0f, 0.0f, 256.0f, 128.0f, 0.0f, 0.0f, 0.0f, 0.0f, \"\", false, 0, -1, 0, -1, false, true, false},"), header.constData());
        QVERIFY2(header.contains("{Kind::Image, 0.0f, 0.0f, 64.0f, 64.0f, 0.0f, 0.0f, 0.0f, 0.0f, \"\", false, 1, -1, -1, 0, false, true, true},"), header.constData());
        // text stays screen-fixed
        QVERIFY2(header.contains("\"Hi\", false, -1, -1, -1, -1, true, false, false},"), header.constData());
    }

    void imagesAndTextsStayShortInHeadersThatDoNotUseThem() {
        Scene scene;
        SceneItem rect;
        rect.id = "r";
        rect.type = "rectangle";
        rect.width = 10;
        rect.height = 20;
        scene.items = {rect};
        const auto header = generateSceneHeader(scene);
        QVERIFY2(header.contains("{Kind::Rectangle, 0.0f, 0.0f, 10.0f, 20.0f, 1.0f, 0.823529f, 0.247059f, 0.0f, \"\", false},"), header.constData());
        QVERIFY(header.contains("inline constexpr float WorldWidth = 1920.0f;"));
        QVERIFY(header.contains("inline constexpr std::size_t TilemapCount = 0;"));
        QVERIFY(header.contains("inline constexpr std::array<Tilemap, 0> Tilemaps{};"));
        QVERIFY(header.contains("inline constexpr std::array<Sprite, 0> Sprites{};"));
        QVERIFY(header.contains("inline constexpr std::array<Animation, 0> Animations{};"));
    }

    void tilesAreWrittenAsRawLittleEndianCells() {
        auto map = makeTilemap("map", "assets/tiles.png", 2, 1);
        map.tiles = {258, -1};
        const auto bytes = tilemapCellBytes(map);
        QCOMPARE(bytes.size(), 8);
        QCOMPARE(static_cast<unsigned char>(bytes[0]), 2);
        QCOMPARE(static_cast<unsigned char>(bytes[1]), 1);
        QCOMPARE(static_cast<unsigned char>(bytes[4]), 255);
        QCOMPARE(static_cast<unsigned char>(bytes[7]), 255);
    }

    void imageSourceLinksTheTileFiles() {
        const QList<SceneImageData> images{imageOf("assets/tiles.png", 2, 2)};
        const auto withTiles = generateImageSource(images, 2);
        QVERIFY2(withTiles.contains("/tilemap-1.tiles"), withTiles.constData());
        QVERIFY2(withTiles.contains("const std::array<const std::int32_t*, TilemapCount> TilemapCells{{"), withTiles.constData());
        // Without tilemaps the file reads exactly as it did.
        const auto without = generateImageSource(images);
        QVERIFY(!without.contains("tilemap"));
        QVERIFY(without.contains("const std::array<const std::int32_t*, 0> TilemapCells{};"));
    }

    void writesTileDataAndClearsStaleFiles() {
        QTemporaryDir directory;
        Scene scene;
        auto map = makeTilemap("map", "assets/tiles.png", 2, 1);
        map.tiles = {5, -1};
        scene.items = {map};
        const QList<SceneImageData> images{imageOf("assets/tiles.png", 96, 32)};
        QDir(directory.path()).mkpath("src/GeneratedImages");
        writeText(directory.filePath("src/GeneratedImages/tilemap-9.tiles"), "stale");
        QString error;
        QVERIFY2(writeGeneratedScene(directory.path(), scene, images, error), qPrintable(error));
        QFile file(directory.filePath("src/GeneratedImages/tilemap-0.tiles"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.size(), 8);
        QVERIFY(!QFile::exists(directory.filePath("src/GeneratedImages/tilemap-9.tiles")));

        auto bad = scene;
        bad.items[0].tiles = {99, -1};
        QVERIFY(!writeGeneratedScene(directory.path(), bad, images, error));
        QVERIFY2(error.contains("tile 99"), qPrintable(error));
    }

    void projectsWithOldRuntimesAreUpdatedOnlyForSpritesAndTilemaps() {
        QTemporaryDir project;
        QDir(project.path()).mkpath("include/Scene");
        writeText(project.filePath("include/Scene/SceneState.hpp"), "GeneratedVariables.hpp");
        Scene plain;
        QVERIFY(!sceneRuntimeNeedsUpdate(project.path(), plain));
        Scene tiled;
        tiled.items = {makeTilemap("map", "assets/tiles.png", 2, 2)};
        QVERIFY(sceneRuntimeNeedsUpdate(project.path(), tiled));
        Scene wider;
        wider.worldWidth = 3840;
        QVERIFY(sceneRuntimeNeedsUpdate(project.path(), wider));
        writeText(project.filePath("include/Scene/SceneState.hpp"), "GeneratedVariables.hpp\nScene/World2D.hpp");
        QVERIFY(!sceneRuntimeNeedsUpdate(project.path(), tiled));
    }

    // --- the document's operations ---

    void paintsAndEraseTilesAndSavesOnEndChange() {
        QTemporaryDir directory;
        SceneDocument document;
        Scene scene;
        scene.items = {makeTilemap("map", "assets/tiles.png", 4, 2)};
        openWithImages(directory, document, scene);
        QVERIFY(!document.canUndo());

        document.beginChange();
        QVERIFY(document.paintTile(0, 1, 1, 7));
        QVERIFY(document.paintTile(0, 2, 1, 7));
        document.endChange();
        QCOMPARE(document.scene().items[0].tiles, QList<int>({-1, -1, -1, -1, -1, 7, 7, -1}));
        QCOMPARE(document.tileAt(0, 1, 1), 7);
        QCOMPARE(loadScene(sceneFile(directory)).scene->items[0].tiles, document.scene().items[0].tiles);

        // A drag is one undo step, and painting off the map changes nothing.
        QVERIFY(document.canUndo());
        QVERIFY(!document.paintTile(0, 9, 9, 3));
        QVERIFY(!document.paintTile(0, -1, 0, 3));
        QVERIFY(document.paintTile(0, 2, 1, -1)); // erase, on its own, is one undo step too
        QCOMPARE(document.tileAt(0, 2, 1), -1);
        document.undo();
        QCOMPARE(document.tileAt(0, 2, 1), 7);
        document.undo();
        QCOMPARE(document.tileAt(0, 1, 1), -1);
    }

    void refusesPaintingWhatCannotBePainted() {
        QTemporaryDir directory;
        SceneDocument document;
        Scene scene;
        SceneItem rectangle;
        rectangle.id = "r";
        rectangle.type = "rectangle";
        rectangle.width = 10;
        rectangle.height = 10;
        scene.items = {rectangle};
        openWithImages(directory, document, scene);
        QVERIFY(!document.paintTile(0, 0, 0, 1));
        QVERIFY(!document.paintTile(5, 0, 0, 1));
        QCOMPARE(document.fillTiles(0, 0, 0, 1), 0);
        QVERIFY(!document.setTileSolid(0, 1, true));
    }

    void fillsAConnectedAreaOnly() {
        QTemporaryDir directory;
        SceneDocument document;
        Scene scene;
        auto map = makeTilemap("map", "assets/tiles.png", 4, 3);
        // A wall of 1s down the middle: a fill on the left does not reach the right.
        map.tiles = {-1, 1, -1, -1,
                     -1, 1, -1, -1,
                     -1, 1, -1, 2};
        scene.items = {map};
        openWithImages(directory, document, scene);

        const auto painted = document.fillTiles(0, 0, 0, 5);
        QCOMPARE(painted, 3);
        QCOMPARE(document.scene().items[0].tiles, QList<int>({5, 1, -1, -1, 5, 1, -1, -1, 5, 1, -1, 2}));
        document.undo();
        QCOMPARE(document.scene().items[0].tiles.first(), -1);

        // Filling with the value that is already there does nothing at all.
        QCOMPARE(document.fillTiles(0, 0, 0, -1), 0);
        QVERIFY(!document.canUndo() || document.scene().items[0].tiles.first() == -1);
    }

    void solidFlagsAreSortedAndUndoable() {
        QTemporaryDir directory;
        SceneDocument document;
        Scene scene;
        scene.items = {makeTilemap("map", "assets/tiles.png", 2, 2)};
        openWithImages(directory, document, scene);

        QVERIFY(document.setTileSolid(0, 5, true));
        QVERIFY(document.setTileSolid(0, 1, true));
        QVERIFY(document.setTileSolid(0, 5, true)); // already solid: nothing to do, still fine
        QCOMPARE(document.scene().items[0].solidTiles, QList<int>({1, 5}));
        QVERIFY(document.isTileSolid(0, 5));
        QVERIFY(!document.isTileSolid(0, 2));
        QVERIFY(document.setTileSolid(0, 5, false));
        QCOMPARE(document.scene().items[0].solidTiles, QList<int>({1}));
        document.undo();
        QCOMPARE(document.scene().items[0].solidTiles, QList<int>({1, 5}));
        QVERIFY(!document.setTileSolid(0, 70000, true));
    }

    void changingTheTilemapsSettingsKeepsTheTiles() {
        QTemporaryDir directory;
        SceneDocument document;
        Scene scene;
        auto map = makeTilemap("map", "assets/tiles.png", 3, 2);
        map.tiles = {0, 1, 2, 3, 4, 5};
        scene.items = {map};
        openWithImages(directory, document, scene);

        QVERIFY(document.setItemValue(0, "columns", 2));
        QCOMPARE(document.scene().items[0].tiles, QList<int>({0, 1, 3, 4}));
        QVERIFY(document.setItemValue(0, "tileScale", 2));
        QCOMPARE(document.scene().items[0].width, 2.0 * 16 * 2);
        QVERIFY(!document.setItemValue(0, "tileWidth", 2)); // refused by the validator
        QVERIFY(document.error().contains("tileWidth"));
        QVERIFY(document.setItemValue(0, "rows", 3));
        QCOMPARE(document.scene().items[0].rows, 3);
        QCOMPARE(document.scene().items[0].tiles.size(), 6);
    }

    void movesMapsWithinTheWorld() {
        QTemporaryDir directory;
        SceneDocument document;
        Scene scene;
        scene.worldWidth = 3840;
        scene.worldHeight = 1080;
        SceneItem rectangle;
        rectangle.id = "r";
        rectangle.type = "rectangle";
        rectangle.width = 10;
        rectangle.height = 10;
        scene.items = {rectangle};
        openWithImages(directory, document, scene);

        // Past the old 1920 stage edge, because the world is wider.
        document.moveItem(0, 3000, 0);
        QCOMPARE(document.scene().items[0].x, 3000.0);
        document.moveItem(0, 9999, 0);
        QCOMPARE(document.scene().items[0].x, 3830.0);

        // A screen-fixed item stays on the screen, not the world.
        QVERIFY(document.setItemValue(0, "screenFixed", true));
        QCOMPARE(document.scene().items[0].x, 1910.0);
    }

    void stageSizeStillBoundsDefaultWorldScenes() {
        QTemporaryDir directory;
        SceneDocument document;
        openWithImages(directory, document, Scene{});
        const auto index = document.addItem("rectangle", 960, 540);
        document.moveItem(index, 9999, 9999);
        QCOMPARE(document.scene().items[0].x, 1720.0); // same as before there was a world
    }

    void worldSizeAndCameraAreEditable() {
        QTemporaryDir directory;
        SceneDocument document;
        openWithImages(directory, document, Scene{});
        QSignalSpy changed(&document, &SceneDocument::worldChanged);

        QVERIFY(document.setWorldSize(3840, 2160));
        QCOMPARE(document.worldWidth(), 3840);
        QCOMPARE(document.worldHeight(), 2160);
        QCOMPARE(changed.size(), 1);

        document.setCameraStart(1000, 9999);
        QCOMPARE(document.cameraX(), 1000.0);
        QCOMPARE(document.cameraY(), 1080.0); // clamped: 2160 - 1080

        QVERIFY(!document.setWorldSize(100, 100));
        QVERIFY(document.error().contains("world"));
        QCOMPARE(document.worldWidth(), 3840);

        // Shrinking the world keeps the camera inside it.
        QVERIFY(document.setWorldSize(1920, 2160));
        QCOMPARE(document.cameraX(), 0.0);

        document.undo();
        QCOMPARE(document.worldWidth(), 3840);
        QCOMPARE(document.cameraX(), 1000.0);
    }

    void aTilemapIsAddedOverTheWorldWithoutTiles() {
        QTemporaryDir directory;
        SceneDocument document;
        openWithImages(directory, document, Scene{});
        const auto index = document.addTilemap("assets/tiles.png", 160, 32, "Tiles", 960, 540);
        QCOMPARE(index, 0);
        const auto& item = document.scene().items[0];
        QCOMPARE(item.type, QString("tilemap"));
        QCOMPARE(item.tileWidth, 16);
        QCOMPARE(item.tileScale, 4);
        QVERIFY(std::all_of(item.tiles.begin(), item.tiles.end(), [](int tile) { return tile == -1; }));
        QVERIFY(!item.tiles.isEmpty());
        const auto expected = QList<int>(item.columns * item.rows, -1);
        QCOMPARE(item.tiles, expected);

        // A tileset that cannot hold a single tile is refused with a reason.
        QCOMPARE(document.addTilemap("assets/tiles.png", 8, 8, "Bad", 0, 0), -1);
        QVERIFY(document.error().contains("tile"));
    }

    void animationsAreAddedChangedAndRemoved() {
        QTemporaryDir directory;
        SceneDocument document;
        Scene scene;
        scene.items = {makeSprite("hero", "assets/hero.png")};
        openWithImages(directory, document, scene);

        QVERIFY(document.addAnimation(0, "run"));
        QCOMPARE(document.scene().items[0].animations.size(), 3);
        QVERIFY(!document.addAnimation(0, "run")); // the same name twice
        QVERIFY(!document.addAnimation(0, "not valid"));
        QVERIFY(document.setAnimationFrames(0, "run", QVariantList{1, 2, 3}));
        QCOMPARE(document.scene().items[0].animations[2].frames, QList<int>({1, 2, 3}));
        QVERIFY(!document.setAnimationFrames(0, "run", QVariantList{}));
        QVERIFY(!document.setAnimationFrames(0, "run", QVariantList{"a"}));
        QVERIFY(document.setAnimationFps(0, "run", 12));
        QCOMPARE(document.scene().items[0].animations[2].fps, 12);
        QVERIFY(!document.setAnimationFps(0, "run", 0));

        QVERIFY(document.setItemValue(0, "animation", "run"));
        QCOMPARE(document.scene().items[0].animation, QString("run"));
        QVERIFY(document.removeAnimation(0, "run"));
        QCOMPARE(document.scene().items[0].animation, QString("walk")); // falls back to the first one

        QVERIFY(document.setItemValue(0, "flipX", true));
        QVERIFY(document.scene().items[0].flipX);
        QVERIFY(document.setItemValue(0, "frameWidth", 8));
        QCOMPARE(document.scene().items[0].frameWidth, 8);
    }

    void animationsCannotBeAddedToAPlainImage() {
        QTemporaryDir directory;
        SceneDocument document;
        Scene scene;
        auto item = makeSprite("hero", "assets/hero.png");
        item.frameWidth = 0;
        item.frameHeight = 0;
        item.animations.clear();
        item.animation.clear();
        scene.items = {item};
        openWithImages(directory, document, scene);
        QVERIFY(!document.addAnimation(0, "run"));
        QVERIFY(document.error().contains("frame"));
    }

    void turningOnASheetTurnsOnPixelScaling() {
        QTemporaryDir directory;
        SceneDocument document;
        Scene scene;
        auto item = makeSprite("hero", "assets/hero.png");
        item.frameWidth = 0;
        item.frameHeight = 0;
        item.animations.clear();
        item.animation.clear();
        item.pixelated = false;
        scene.items = {item};
        openWithImages(directory, document, scene);
        QVERIFY(document.setItemValue(0, "frameWidth", 16));
        QCOMPARE(document.scene().items[0].frameHeight, 16);
        QVERIFY(document.scene().items[0].pixelated);
    }

    void selectedExposesSpritesAndTilemapsToTheInterface() {
        QTemporaryDir directory;
        SceneDocument document;
        Scene scene;
        scene.items = {makeSprite("hero", "assets/hero.png"), makeTilemap("map", "assets/tiles.png", 2, 2)};
        openWithImages(directory, document, scene);
        document.setSelectedIndex(0);
        auto selected = document.selected();
        QCOMPARE(selected.value("frameWidth").toInt(), 16);
        QCOMPARE(selected.value("animations").toList().size(), 2);
        QCOMPARE(selected.value("animations").toList()[0].toMap().value("frames").toString(), QString("0, 1, 2, 3"));
        document.setSelectedIndex(1);
        selected = document.selected();
        QCOMPARE(selected.value("tileScale").toInt(), 4);
        QCOMPARE(selected.value("columns").toInt(), 2);
    }

    // --- blocks ---

    void catalogHasTheNewBlocks() {
        const QHash<QString, QString> titles{{"playAnimation", "Play animation on item"}, {"flipItem", "Flip item"}, {"cameraFollow", "Camera follows item"}, {"setCameraPosition", "Set camera position"}};
        for (const auto& info : blockCatalog()) {
            if (titles.contains(info.jsonName)) QCOMPARE(info.title, titles.value(info.jsonName));
        }
        QVERIFY(std::ranges::any_of(blockCatalog(), [](const BlockInfo& info) { return info.type == BlockType::SetCameraPosition && info.usesPosition && !info.usesItem; }));
        QVERIFY(std::ranges::any_of(blockCatalog(), [](const BlockInfo& info) { return info.type == BlockType::PlayAnimation && info.usesItem && info.usesAnimation; }));
        QVERIFY(std::ranges::any_of(blockCatalog(), [](const BlockInfo& info) { return info.type == BlockType::FlipItem && info.usesItem && info.usesFlipped; }));
        QVERIFY(std::ranges::any_of(blockCatalog(), [](const BlockInfo& info) { return info.type == BlockType::CameraFollow && info.usesItem && !info.usesPosition; }));
    }

    void newBlocksRoundTripThroughLogicJson() {
        QTemporaryDir directory;
        LogicProgram program;
        auto event = makeBlock("e1", BlockType::EveryFrame);
        auto play = makeBlock("b1", BlockType::PlayAnimation);
        play.item = "hero";
        play.animation = "walk";
        auto flip = makeBlock("b2", BlockType::FlipItem);
        flip.item = "hero";
        flip.flipped = false;
        auto follow = makeBlock("b3", BlockType::CameraFollow);
        follow.item = "hero";
        auto camera = makeBlock("b4", BlockType::SetCameraPosition);
        camera.x = 500;
        camera.y = -20;
        event.children = {play, flip, follow, camera};
        program.events = {event};

        QString error;
        QVERIFY2(saveLogic(directory.filePath("logic.json"), program, error), qPrintable(error));
        const auto loaded = loadLogic(directory.filePath("logic.json"));
        QVERIFY2(loaded.program.has_value(), qPrintable(loaded.error));
        const auto& actions = loaded.program->events[0].children;
        QCOMPARE(actions[0].animation, QString("walk"));
        QCOMPARE(actions[1].flipped, false);
        QCOMPARE(actions[3].x, 500.0);
        QCOMPARE(actions[3].y, -20.0);
    }

    void aSchemaTwoLogicFileStillLoadsAndCannotUseNewBlocks() {
        QTemporaryDir directory;
        writeText(directory.filePath("logic.json"), LogicTwoMoveBy);
        QVERIFY(loadLogic(directory.filePath("logic.json")).program.has_value());
        writeText(directory.filePath("logic.json"), LogicTwoCamera);
        const auto refused = loadLogic(directory.filePath("logic.json"));
        QVERIFY(!refused.program.has_value());
        QVERIFY2(refused.error.contains("schemaVersion 3"), qPrintable(refused.error));
    }

    void validatesSpriteAndCameraBlocks() {
        Scene scene;
        auto hero = makeSprite("hero", "assets/hero.png");
        SceneItem rectangle;
        rectangle.id = "r";
        rectangle.type = "rectangle";
        rectangle.name = "Box";
        rectangle.width = 10;
        rectangle.height = 10;
        SceneItem pinned = rectangle;
        pinned.id = "pinned";
        pinned.screenFixed = true;
        scene.items = {hero, rectangle, pinned};

        const auto problemsFor = [&](const Block& action) {
            LogicProgram program;
            auto event = makeBlock("e1", BlockType::EveryFrame);
            event.children = {action};
            program.events = {event};
            QStringList texts;
            for (const auto& problem : validateLogic(program, scene)) texts << problem.message;
            return texts.join(' ');
        };

        auto play = makeBlock("b1", BlockType::PlayAnimation);
        play.item = "hero";
        play.animation = "walk";
        QVERIFY2(problemsFor(play).isEmpty(), qPrintable(problemsFor(play)));
        play.animation = "ghost";
        QVERIFY2(problemsFor(play).contains("ghost"), qPrintable(problemsFor(play)));
        play.animation = "";
        QVERIFY(problemsFor(play).contains("no animation"));
        play.item = "r";
        play.animation = "walk";
        QVERIFY2(problemsFor(play).contains("sprite"), qPrintable(problemsFor(play)));

        auto flip = makeBlock("b2", BlockType::FlipItem);
        flip.item = "hero";
        QVERIFY(problemsFor(flip).isEmpty());
        flip.item = "r";
        QVERIFY2(problemsFor(flip).contains("image"), qPrintable(problemsFor(flip)));

        auto follow = makeBlock("b3", BlockType::CameraFollow);
        follow.item = "hero";
        QVERIFY(problemsFor(follow).isEmpty());
        follow.item = "pinned";
        QVERIFY2(problemsFor(follow).contains("Stays on screen"), qPrintable(problemsFor(follow)));

        auto camera = makeBlock("b4", BlockType::SetCameraPosition);
        camera.x = 2000000;
        QVERIFY(!problemsFor(camera).isEmpty());
    }

    void generatedLogicCallsTheRuntime() {
        Scene scene;
        scene.items = {makeSprite("hero", "assets/hero.png")};
        LogicProgram program;
        auto event = makeBlock("e1", BlockType::EveryFrame);
        auto play = makeBlock("b1", BlockType::PlayAnimation);
        play.item = "hero";
        play.animation = "jump";
        auto flip = makeBlock("b2", BlockType::FlipItem);
        flip.item = "hero";
        flip.flipped = true;
        auto follow = makeBlock("b3", BlockType::CameraFollow);
        follow.item = "hero";
        auto camera = makeBlock("b4", BlockType::SetCameraPosition);
        camera.x = 12;
        camera.y = 30;
        event.children = {play, flip, follow, camera};
        program.events = {event};

        const auto header = generateLogicHeader(program, scene);
        QVERIFY2(header.contains("world.PlayAnimation(0, 1); // b1"), header.constData());
        QVERIFY2(header.contains("world.SetFlipped(0, true); // b2"), header.constData());
        QVERIFY2(header.contains("world.CameraFollow(0); // b3"), header.constData());
        QVERIFY2(header.contains("world.SetCameraPosition(12.0f, 30.0f); // b4"), header.constData());
    }
};

QTEST_GUILESS_MAIN(SpritesTilemapTests)
#include "tst_sprites_tilemap.moc"
