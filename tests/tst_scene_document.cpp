#include "SceneDocument.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <limits>

using namespace Studio;

namespace {

QString scenePath(const QTemporaryDir& directory) {
    return directory.path() + "/scene.json";
}

void openEmpty(const QTemporaryDir& directory, SceneDocument& document) {
    QString error;
    QVERIFY2(saveScene(scenePath(directory), Scene{}, error), qPrintable(error));
    QVERIFY(document.open(scenePath(directory)));
}

Scene savedScene(const QTemporaryDir& directory) {
    return *loadScene(scenePath(directory)).scene;
}

// The document only checks that an image file exists; decoding it happens in src/export.
void createFile(const QString& path) {
    QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("image bytes");
}

}

class SceneDocumentTests : public QObject {
    Q_OBJECT

private slots:
    void missingSceneIsNotLoadedAndNotAnError() {
        QTemporaryDir directory;
        SceneDocument document;
        QVERIFY(!document.open(scenePath(directory)));
        QVERIFY(!document.isLoaded());
        QVERIFY(document.error().isEmpty());
    }

    void invalidSceneReportsWhy() {
        QTemporaryDir directory;
        QFile file(scenePath(directory));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("{\"schemaVersion\":9}");
        file.close();
        SceneDocument document;
        QVERIFY(!document.open(scenePath(directory)));
        QVERIFY(!document.error().isEmpty());
    }

    void addingCentersTheItemSelectsAndSaves() {
        QTemporaryDir directory;
        SceneDocument document;
        openEmpty(directory, document);
        const auto index = document.addItem("rectangle", 960, 540);
        QCOMPARE(index, 0);
        QCOMPARE(document.rowCount(), 1);
        QCOMPARE(document.selectedIndex(), 0);
        const auto saved = savedScene(directory).items.first();
        QCOMPARE(saved.x + saved.width / 2, 960.0);
        QCOMPARE(saved.y + saved.height / 2, 540.0);
    }

    void itemsDroppedAtTheEdgeStayOnStage() {
        QTemporaryDir directory;
        SceneDocument document;
        openEmpty(directory, document);
        document.addItem("rectangle", 1919, 1079);
        const auto item = document.scene().items.first();
        QCOMPARE(item.x + item.width, 1920.0);
        QCOMPARE(item.y + item.height, 1080.0);
    }

    void refusesItemsPastTheLimit() {
        QTemporaryDir directory;
        SceneDocument document;
        openEmpty(directory, document);
        for (int i = 0; i < 256; ++i) QCOMPARE(document.addItem("rectangle", 100, 100), i);
        QCOMPARE(document.addItem("rectangle", 100, 100), -1);
        QVERIFY(document.error().contains("256"));
        QCOMPARE(document.rowCount(), 256);
    }

    void movingRoundsAndClampsToTheStage() {
        QTemporaryDir directory;
        SceneDocument document;
        openEmpty(directory, document);
        document.addItem("rectangle", 500, 500);
        document.moveItem(0, 10.6, 20.2);
        QCOMPARE(document.scene().items[0].x, 11.0);
        QCOMPARE(document.scene().items[0].y, 20.0);
        document.moveItem(0, -50, 5000);
        QCOMPARE(document.scene().items[0].x, 0.0);
        QCOMPARE(document.scene().items[0].y, 1080.0 - document.scene().items[0].height);
    }

    void aDragIsOneUndoAndSavesWhenFinished() {
        QTemporaryDir directory;
        SceneDocument document;
        openEmpty(directory, document);
        document.addItem("rectangle", 500, 500);
        const auto startX = document.scene().items[0].x;
        document.beginChange();
        document.moveItem(0, 10, 10);
        document.moveItem(0, 20, 20);
        QCOMPARE(savedScene(directory).items[0].x, startX);
        document.endChange();
        QCOMPARE(savedScene(directory).items[0].x, 20.0);
        document.undo();
        QCOMPARE(document.scene().items[0].x, startX);
        QCOMPARE(savedScene(directory).items[0].x, startX);
    }

    void aClickWithoutMovementAddsNoUndoStep() {
        QTemporaryDir directory;
        SceneDocument document;
        openEmpty(directory, document);
        document.addItem("rectangle", 500, 500);
        document.undo();
        QVERIFY(!document.canUndo());
        document.addItem("rectangle", 500, 500);
        document.beginChange();
        document.endChange();
        document.undo();
        QCOMPARE(document.rowCount(), 0);
    }

    void resizeKeepsAMinimumAndStaysOnStage() {
        QTemporaryDir directory;
        SceneDocument document;
        openEmpty(directory, document);
        document.addItem("rectangle", 500, 500);
        document.resizeItem(0, 1, -40);
        QCOMPARE(document.scene().items[0].width, 8.0);
        QCOMPARE(document.scene().items[0].height, 8.0);
        document.resizeItem(0, 90000, 90000);
        const auto item = document.scene().items[0];
        QCOMPARE(item.x + item.width, 1920.0);
        QCOMPARE(item.y + item.height, 1080.0);
    }

    void textReportsItsDrawnSize() {
        QTemporaryDir directory;
        SceneDocument document;
        openEmpty(directory, document);
        document.addItem("text", 900, 500);
        QVERIFY(document.setItemValue(0, "text", "Hello"));
        QVERIFY(document.setItemValue(0, "scale", 10));
        const auto index = document.index(0);
        QCOMPARE(document.data(index, SceneDocument::WidthRole).toDouble(), (5.0 * 6 - 1) * 10);
        QCOMPARE(document.data(index, SceneDocument::HeightRole).toDouble(), 70.0);
    }

    void growingTextStaysOnStage() {
        QTemporaryDir directory;
        SceneDocument document;
        openEmpty(directory, document);
        document.addItem("text", 1800, 1000);
        QVERIFY(document.setItemValue(0, "scale", 40));
        const auto item = document.scene().items[0];
        QVERIFY(item.x + (item.text.size() * 6 - 1) * item.scale <= 1920);
        QVERIFY(item.y + 7 * item.scale <= 1080);
    }

    void invalidValuesAreRefusedAndExplained() {
        QTemporaryDir directory;
        SceneDocument document;
        openEmpty(directory, document);
        document.addItem("rectangle", 500, 500);
        const auto before = document.scene().items[0].color;
        QVERIFY(!document.setItemValue(0, "color", "blue"));
        QCOMPARE(document.scene().items[0].color, before);
        QVERIFY(!document.error().isEmpty());
        QVERIFY(document.setItemValue(0, "color", "#112233"));
        QVERIFY(document.error().isEmpty());
    }

    void numbersThatAreNotNumbersAreRefused() {
        QTemporaryDir directory;
        SceneDocument document;
        openEmpty(directory, document);
        document.addItem("rectangle", 500, 500);
        const auto before = document.scene().items[0].x;
        QVERIFY(!document.setItemValue(0, "x", std::numeric_limits<double>::quiet_NaN()));
        QCOMPARE(document.scene().items[0].x, before);
    }

    void unknownPropertiesAreRefused() {
        QTemporaryDir directory;
        SceneDocument document;
        openEmpty(directory, document);
        document.addItem("rectangle", 500, 500);
        QVERIFY(!document.setItemValue(0, "nonsense", 1));
        QVERIFY(!document.setItemValue(9, "name", "x"));
    }

    void removingClearsSelectionAndKeepsIdsUnique() {
        QTemporaryDir directory;
        SceneDocument document;
        openEmpty(directory, document);
        document.addItem("rectangle", 100, 100);
        document.addItem("rectangle", 300, 300);
        document.removeItem(0);
        QCOMPARE(document.selectedIndex(), -1);
        document.addItem("rectangle", 500, 500);
        QVERIFY(document.scene().items[0].id != document.scene().items[1].id);
        QCOMPARE(savedScene(directory).items.size(), 2);
    }

    void undoBringsBackADeletedItem() {
        QTemporaryDir directory;
        SceneDocument document;
        openEmpty(directory, document);
        document.addItem("text", 500, 500);
        document.removeItem(0);
        QCOMPARE(document.rowCount(), 0);
        document.undo();
        QCOMPARE(document.rowCount(), 1);
        QCOMPARE(document.scene().items[0].type, QString("text"));
    }

    void backgroundIsSavedAndUndoable() {
        QTemporaryDir directory;
        SceneDocument document;
        openEmpty(directory, document);
        const auto before = document.background();
        document.setBackground("#000000");
        QCOMPARE(savedScene(directory).background, QString("#000000"));
        document.undo();
        QCOMPARE(document.background(), before);
    }

    void selectedMirrorsTheCurrentItem() {
        QTemporaryDir directory;
        SceneDocument document;
        openEmpty(directory, document);
        QVERIFY(document.selected().isEmpty());
        document.addItem("rectangle", 500, 500);
        QSignalSpy changed(&document, &SceneDocument::selectionChanged);
        document.moveItem(0, 40, 50);
        QVERIFY(changed.size() >= 1);
        QCOMPARE(document.selected().value("x").toDouble(), 40.0);
        QCOMPARE(document.selected().value("type").toString(), QString("rectangle"));
        document.setSelectedIndex(-1);
        QVERIFY(document.selected().isEmpty());
    }

    void addingParticlesUsesFireworksDefaults() {
        QTemporaryDir directory;
        SceneDocument document;
        openEmpty(directory, document);
        const auto index = document.addItem("particles", 960, 540);
        QCOMPARE(index, 0);
        QCOMPARE(document.data(document.index(0), SceneDocument::TypeRole).toString(), QString("particles"));
        const auto selected = document.selected();
        QCOMPARE(selected.value("preset").toString(), QString("fireworks"));
        QVERIFY(selected.value("amount").toInt() > 0);
        QVERIFY(selected.value("looping").toBool());
        QVERIFY(selected.value("name").toString().startsWith("Particles"));
        const auto saved = savedScene(directory).items.first();
        QCOMPARE(saved.preset, QString("fireworks"));
        QVERIFY(saved.x >= 0);
        QVERIFY(saved.x + saved.width <= 1920);
        QVERIFY(saved.y + saved.height <= 1080);
    }

    void aPresetAppliesItsLookInOneUndo() {
        QTemporaryDir directory;
        SceneDocument document;
        openEmpty(directory, document);
        document.addItem("particles", 960, 540);
        const auto before = document.selected();
        QVERIFY(document.setItemValue(0, "preset", "snow"));
        const auto selected = document.selected();
        QCOMPARE(selected.value("preset").toString(), QString("snow"));
        QCOMPARE(selected.value("width").toDouble(), 1920.0);
        QCOMPARE(selected.value("height").toDouble(), 1080.0);
        QCOMPARE(selected.value("x").toDouble(), 0.0);
        QVERIFY(selected.value("amount").toInt() != before.value("amount").toInt());
        document.undo();
        QCOMPARE(document.selected().value("preset").toString(), QString("fireworks"));
    }

    void particleSettingsAreEditable() {
        QTemporaryDir directory;
        SceneDocument document;
        openEmpty(directory, document);
        document.addItem("particles", 960, 540);
        QVERIFY(document.setItemValue(0, "palette", "fire"));
        QVERIFY(document.setItemValue(0, "amount", 1234));
        QVERIFY(document.setItemValue(0, "spread", 77));
        QVERIFY(document.setItemValue(0, "speed", 55));
        QVERIFY(document.setItemValue(0, "gravity", -40));
        QVERIFY(document.setItemValue(0, "pixelSize", 4));
        QVERIFY(document.setItemValue(0, "looping", false));
        const auto saved = savedScene(directory).items.first();
        QCOMPARE(saved.palette, QString("fire"));
        QCOMPARE(saved.amount, 1234);
        QCOMPARE(saved.spread, 77);
        QCOMPARE(saved.speed, 55);
        QCOMPARE(saved.gravity, -40);
        QCOMPARE(saved.pixelSize, 4);
        QVERIFY(!saved.looping);
    }

    void badParticleSettingsAreRefusedAndExplained() {
        QTemporaryDir directory;
        SceneDocument document;
        openEmpty(directory, document);
        document.addItem("particles", 960, 540);
        QVERIFY(!document.setItemValue(0, "amount", 0));
        QVERIFY2(document.error().contains("amount"), qPrintable(document.error()));
        QVERIFY(!document.setItemValue(0, "preset", "lava"));
        QVERIFY(document.error().contains("preset"));
        QVERIFY(!document.setItemValue(0, "amount", QStringLiteral("many")));
        QCOMPARE(savedScene(directory).items.first().preset, QString("fireworks"));
    }

    void particleSettingsDoNotApplyToOtherItems() {
        QTemporaryDir directory;
        SceneDocument document;
        openEmpty(directory, document);
        document.addItem("rectangle", 960, 540);
        QVERIFY(!document.setItemValue(0, "preset", "snow"));
        QVERIFY(!document.setItemValue(0, "amount", 5));
        QCOMPARE(document.selected().value("width").toDouble(), 200.0);
    }

    void particlesCanBeResizedOnTheStage() {
        QTemporaryDir directory;
        SceneDocument document;
        openEmpty(directory, document);
        document.addItem("particles", 960, 540);
        document.resizeItem(0, 300, 200);
        QCOMPARE(document.selected().value("width").toDouble(), 300.0);
        QCOMPARE(document.selected().value("height").toDouble(), 200.0);
    }

    void aSceneOverTheBlockBudgetIsRefused() {
        QTemporaryDir directory;
        SceneDocument document;
        openEmpty(directory, document);
        document.addItem("particles", 960, 540);
        QVERIFY(document.setItemValue(0, "amount", 8000));
        document.addItem("particles", 960, 540);
        QVERIFY(document.setItemValue(1, "amount", 7990));
        QVERIFY(document.addItem("particles", 960, 540) == -1);
        QVERIFY2(document.error().contains("16000"), qPrintable(document.error()));
    }

    void aNewImageKeepsItsAspectAndFitsTheStage() {
        QTemporaryDir directory;
        SceneDocument document;
        openEmpty(directory, document);
        QCOMPARE(document.addImage("assets/big.png", 4000, 2000, "Big", 960, 540), 0);
        QCOMPARE(document.selectedIndex(), 0);
        const auto item = savedScene(directory).items.first();
        QCOMPARE(item.type, QString("image"));
        QCOMPARE(item.name, QString("Big"));
        QCOMPARE(item.source, QString("assets/big.png"));
        QCOMPARE(item.width, 960.0);
        QCOMPARE(item.height, 480.0);
        QCOMPARE(item.x + item.width / 2, 960.0);
        QCOMPARE(item.y + item.height / 2, 540.0);
    }

    void aTallImageFitsByItsHeight() {
        QTemporaryDir directory;
        SceneDocument document;
        openEmpty(directory, document);
        document.addImage("assets/tall.png", 1000, 3000, "Tall", 960, 540);
        QCOMPARE(document.scene().items[0].width, 180.0);
        QCOMPARE(document.scene().items[0].height, 540.0);
    }

    void aSmallImageKeepsItsPixelSize() {
        QTemporaryDir directory;
        SceneDocument document;
        openEmpty(directory, document);
        document.addImage("assets/icon.png", 64, 32, "Icon", 300, 300);
        QCOMPARE(document.scene().items[0].width, 64.0);
        QCOMPARE(document.scene().items[0].height, 32.0);
    }

    void aSliverOfAnImageStillGetsAVisibleSize() {
        QTemporaryDir directory;
        SceneDocument document;
        openEmpty(directory, document);
        QCOMPARE(document.addImage("assets/line.png", 4000, 1, "Line", 960, 540), 0);
        QCOMPARE(document.scene().items[0].width, 960.0);
        QCOMPARE(document.scene().items[0].height, 8.0);
    }

    void imagesDroppedAtTheEdgeStayOnStage() {
        QTemporaryDir directory;
        SceneDocument document;
        openEmpty(directory, document);
        document.addImage("assets/big.png", 800, 400, "Big", 1919, 1079);
        const auto item = document.scene().items[0];
        QCOMPARE(item.x + item.width, 1920.0);
        QCOMPARE(item.y + item.height, 1080.0);
    }

    void refusesImagesFromOutsideTheAssetsFolder() {
        QTemporaryDir directory;
        SceneDocument document;
        openEmpty(directory, document);
        for (const auto* source : {"../secret.png", "/etc/logo.png", "assets/../../x.png", "assets/photo.jpg"}) {
            QCOMPARE(document.addImage(source, 10, 10, "X", 100, 100), -1);
            QVERIFY(!document.error().isEmpty());
        }
        QCOMPARE(document.rowCount(), 0);
    }

    void refusesImagesPastTheItemLimit() {
        QTemporaryDir directory;
        SceneDocument document;
        openEmpty(directory, document);
        for (int i = 0; i < 256; ++i) QCOMPARE(document.addImage("assets/a.png", 10, 10, "A", 100, 100), i);
        QVERIFY(!document.roomProblem().isEmpty());
        QCOMPARE(document.addImage("assets/a.png", 10, 10, "A", 100, 100), -1);
        QVERIFY(document.error().contains("256"));
    }

    void imagesExposeTheirSourceToTheStage() {
        QTemporaryDir directory;
        SceneDocument document;
        openEmpty(directory, document);
        createFile(directory.path() + "/assets/logo.png");
        document.addImage("assets/logo.png", 10, 10, "Logo", 100, 100);
        document.addItem("rectangle", 500, 500);
        QVERIFY(document.roleNames().values().contains("itemSource"));
        QCOMPARE(document.data(document.index(0), SceneDocument::SourceRole).toString(), QString("assets/logo.png"));
        QCOMPARE(document.data(document.index(0), SceneDocument::ImageUrlRole).toUrl(), QUrl::fromLocalFile(directory.path() + "/assets/logo.png"));
        QVERIFY(document.data(document.index(1), SceneDocument::SourceRole).toString().isEmpty());
        document.setSelectedIndex(0);
        QCOMPARE(document.selected().value("source").toString(), QString("assets/logo.png"));
    }

    void anImageDragIsOneUndoAndSavesWhenFinished() {
        QTemporaryDir directory;
        SceneDocument document;
        openEmpty(directory, document);
        document.addImage("assets/a.png", 400, 200, "A", 960, 540);
        const auto startX = document.scene().items[0].x;
        document.beginChange();
        document.moveItem(0, 100, 100);
        document.moveItem(0, 120, 110);
        QCOMPARE(savedScene(directory).items[0].x, startX);
        document.endChange();
        QCOMPARE(savedScene(directory).items[0].x, 120.0);
        QCOMPARE(savedScene(directory).items[0].source, QString("assets/a.png"));
        document.undo();
        QCOMPARE(document.scene().items[0].x, startX);
    }

    void resizingAnImageHoldsItsAspect() {
        QTemporaryDir directory;
        SceneDocument document;
        openEmpty(directory, document);
        document.addImage("assets/a.png", 400, 200, "A", 960, 540);
        document.moveItem(0, 100, 100);
        document.resizeItem(0, 800, 100, true);
        QCOMPARE(document.scene().items[0].width, 800.0);
        QCOMPARE(document.scene().items[0].height, 400.0);
        QCOMPARE(savedScene(directory).items[0].width, 800.0);
    }

    void resizingAnImageFreelyChangesItsShape() {
        QTemporaryDir directory;
        SceneDocument document;
        openEmpty(directory, document);
        document.addImage("assets/a.png", 400, 200, "A", 960, 540);
        document.moveItem(0, 100, 100);
        document.resizeItem(0, 300, 300);
        QCOMPARE(document.scene().items[0].width, 300.0);
        QCOMPARE(document.scene().items[0].height, 300.0);
    }

    void aDragHoldsTheAspectItStartedWith() {
        QTemporaryDir directory;
        SceneDocument document;
        openEmpty(directory, document);
        document.addImage("assets/a.png", 300, 200, "A", 960, 540);
        document.moveItem(0, 0, 0);
        document.beginChange();
        for (double width = 300; width > 9; width -= 7) document.resizeItem(0, width, 1, true);
        for (double width = 9; width < 900; width += 11) document.resizeItem(0, width, 1, true);
        document.resizeItem(0, 900, 1, true);
        document.endChange();
        QCOMPARE(document.scene().items[0].width, 900.0);
        QCOMPARE(document.scene().items[0].height, 600.0);
    }

    void holdingTheAspectStaysOnStageAndAboveTheMinimum() {
        QTemporaryDir directory;
        SceneDocument document;
        openEmpty(directory, document);
        document.addImage("assets/a.png", 400, 200, "A", 960, 540);
        document.moveItem(0, 800, 700);
        document.resizeItem(0, 90000, 90000, true);
        auto item = document.scene().items[0];
        QCOMPARE(item.y + item.height, 1080.0);
        QCOMPARE(item.width, 760.0);
        document.resizeItem(0, 1, 1, true);
        item = document.scene().items[0];
        QCOMPARE(item.height, 8.0);
        QCOMPARE(item.width, 16.0);
    }

    void replacingAnImageKeepsItsAreaAndCenter() {
        QTemporaryDir directory;
        SceneDocument document;
        openEmpty(directory, document);
        document.addImage("assets/a.png", 400, 200, "A", 960, 540);
        QVERIFY(document.replaceImage(0, "assets/b.png", 100, 100));
        const auto item = document.scene().items[0];
        QCOMPARE(item.source, QString("assets/b.png"));
        QCOMPARE(item.name, QString("A"));
        QCOMPARE(item.width, 283.0);
        QCOMPARE(item.height, 283.0);
        QVERIFY(qAbs(item.x + item.width / 2 - 960) <= 1);
        QVERIFY(qAbs(item.y + item.height / 2 - 540) <= 1);
        QCOMPARE(savedScene(directory).items[0].source, QString("assets/b.png"));
        document.undo();
        QCOMPARE(document.scene().items[0].source, QString("assets/a.png"));
        QCOMPARE(document.scene().items[0].width, 400.0);
    }

    void replacingWithTheSameShapeKeepsTheExactSize() {
        QTemporaryDir directory;
        SceneDocument document;
        openEmpty(directory, document);
        document.addImage("assets/a.png", 400, 200, "A", 960, 540);
        QVERIFY(document.replaceImage(0, "assets/b.png", 800, 400));
        QCOMPARE(document.scene().items[0].width, 400.0);
        QCOMPARE(document.scene().items[0].height, 200.0);
    }

    void replacingStaysOnStage() {
        QTemporaryDir directory;
        SceneDocument document;
        openEmpty(directory, document);
        document.addImage("assets/a.png", 500, 500, "A", 960, 540);
        QVERIFY(document.setItemValue(0, "width", 1900));
        QVERIFY(document.setItemValue(0, "height", 1000));
        QVERIFY(document.replaceImage(0, "assets/wide.png", 4000, 100));
        const auto item = document.scene().items[0];
        QVERIFY(item.width <= 1920 && item.height <= 1080);
        QVERIFY(item.x >= 0 && item.x + item.width <= 1920);
    }

    void replacingRefusesOtherItemsAndBadSources() {
        QTemporaryDir directory;
        SceneDocument document;
        openEmpty(directory, document);
        document.addItem("rectangle", 500, 500);
        QVERIFY(!document.replaceImage(0, "assets/b.png", 10, 10));
        document.addImage("assets/a.png", 10, 10, "A", 100, 100);
        QVERIFY(!document.replaceImage(1, "../b.png", 10, 10));
        QVERIFY(!document.replaceImage(9, "assets/b.png", 10, 10));
        QCOMPARE(document.scene().items[1].source, QString("assets/a.png"));
    }

    void aMissingImageFileIsReportedNotFailed() {
        QTemporaryDir directory;
        SceneDocument document;
        openEmpty(directory, document);
        document.addImage("assets/gone.png", 100, 50, "Gone", 500, 500);
        QCOMPARE(document.rowCount(), 1);
        const auto index = document.index(0);
        QVERIFY(document.data(index, SceneDocument::ImageProblemRole).toString().contains("assets/gone.png"));
        QVERIFY(document.data(index, SceneDocument::ImageUrlRole).toUrl().isEmpty());
        QVERIFY(document.imageWarning().contains("assets/gone.png"));
        QVERIFY(document.selected().value("imageProblem").toString().contains("assets/gone.png"));
    }

    void noticesTheFileComingBackAndGoingAway() {
        QTemporaryDir directory;
        SceneDocument document;
        openEmpty(directory, document);
        document.addImage("assets/logo.png", 100, 50, "Logo", 500, 500);
        QVERIFY(!document.imageWarning().isEmpty());

        QSignalSpy changed(&document, &SceneDocument::dataChanged);
        createFile(directory.path() + "/assets/logo.png");
        document.recheckImages();
        QCOMPARE(changed.size(), 1);
        QVERIFY(document.imageWarning().isEmpty());
        QVERIFY(!document.data(document.index(0), SceneDocument::ImageUrlRole).toUrl().isEmpty());

        document.recheckImages();
        QCOMPARE(changed.size(), 1);

        QVERIFY(QFile::remove(directory.path() + "/assets/logo.png"));
        document.recheckImages();
        QCOMPARE(changed.size(), 2);
        QVERIFY(document.imageWarning().contains("assets/logo.png"));
    }

    void aSceneOpenedWithAMissingFileWarnsStraightAway() {
        QTemporaryDir directory;
        Scene scene;
        SceneItem item;
        item.id = "item-1";
        item.type = "image";
        item.width = 10;
        item.height = 10;
        item.source = "assets/lost.png";
        scene.items = {item};
        QString error;
        QVERIFY2(saveScene(scenePath(directory), scene, error), qPrintable(error));
        SceneDocument document;
        QVERIFY(document.open(scenePath(directory)));
        QVERIFY(document.imageWarning().contains("assets/lost.png"));
    }

    void aLinkedImageFileIsNotShown() {
        QTemporaryDir directory;
        QTemporaryDir outside;
        createFile(outside.path() + "/secret.png");
        QVERIFY(QDir(directory.path()).mkpath("assets"));
        QVERIFY(QFile::link(outside.path() + "/secret.png", directory.path() + "/assets/link.png"));
        SceneDocument document;
        openEmpty(directory, document);
        document.addImage("assets/link.png", 10, 10, "Link", 100, 100);
        QVERIFY(document.data(document.index(0), SceneDocument::ImageUrlRole).toUrl().isEmpty());
        QVERIFY(document.data(document.index(0), SceneDocument::ImageProblemRole).toString().contains("link"));
    }

    void aNoticeLastsUntilTheNextChange() {
        QTemporaryDir directory;
        SceneDocument document;
        openEmpty(directory, document);
        document.addItem("rectangle", 500, 500);
        QSignalSpy changed(&document, &SceneDocument::noticeChanged);
        document.setNotice("Scaled down", false);
        QCOMPARE(document.notice(), QString("Scaled down"));
        QVERIFY(!document.noticeIsProblem());
        document.setNotice("Could not read", true);
        QVERIFY(document.noticeIsProblem());
        QVERIFY(changed.size() >= 2);
        document.moveItem(0, 40, 50);
        QVERIFY(document.notice().isEmpty());
    }

    void closeForgetsTheScene() {
        QTemporaryDir directory;
        SceneDocument document;
        openEmpty(directory, document);
        document.addItem("rectangle", 500, 500);
        document.close();
        QVERIFY(!document.isLoaded());
        QCOMPARE(document.rowCount(), 0);
        QVERIFY(!document.canUndo());
    }
};

QTEST_GUILESS_MAIN(SceneDocumentTests)
#include "tst_scene_document.moc"
