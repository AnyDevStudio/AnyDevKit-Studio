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

// The items' ids back to front, the order they are drawn in.
QStringList itemIds(const Scene& scene) {
    QStringList ids;
    for (const auto& item : scene.items) ids.append(item.id);
    return ids;
}

QStringList itemIds(const SceneDocument& document) {
    return itemIds(document.scene());
}

// item-1, item-2, item-3 and so on, spaced out so they do not overlap.
void addRectangles(SceneDocument& document, int count) {
    for (int i = 0; i < count; ++i) QVERIFY(document.addItem("rectangle", 200 + i * 240, 200) >= 0);
}

QByteArray fileBytes(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
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
        for (int i = 0; i < MaximumSceneItems; ++i) QCOMPARE(document.addItem("rectangle", 100, 100), i);
        QCOMPARE(document.addItem("rectangle", 100, 100), -1);
        QVERIFY(document.error().contains(QString::number(MaximumSceneItems)));
        QCOMPARE(document.rowCount(), MaximumSceneItems);
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
        for (int i = 0; i < MaximumSceneItems; ++i) QCOMPARE(document.addImage("assets/a.png", 10, 10, "A", 100, 100), i);
        QVERIFY(!document.roomProblem().isEmpty());
        QCOMPARE(document.addImage("assets/a.png", 10, 10, "A", 100, 100), -1);
        QVERIFY(document.error().contains(QString::number(MaximumSceneItems)));
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
#ifdef Q_OS_WIN
        QSKIP("Creating symbolic links needs extra rights on Windows.");
#endif
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

    void reorderingMovesAnItemAndKeepsItSelected() {
        QTemporaryDir directory;
        SceneDocument document;
        openEmpty(directory, document);
        addRectangles(document, 3);
        document.setSelectedIndex(0);
        QVERIFY(document.reorderItem(0, 2));
        QCOMPARE(itemIds(document), QStringList({"item-2", "item-3", "item-1"}));
        QCOMPARE(document.selectedIndex(), 2);
        QCOMPARE(itemIds(savedScene(directory)), itemIds(document));
    }

    void reorderingFollowsTheSelectionOfAnotherItem() {
        QTemporaryDir directory;
        SceneDocument document;
        openEmpty(directory, document);
        addRectangles(document, 3);
        document.setSelectedIndex(1);
        QVERIFY(document.reorderItem(0, 2));
        QCOMPARE(document.scene().items[document.selectedIndex()].id, QString("item-2"));
    }

    void reorderingIsOneUndoStepAndUndoKeepsTheSelection() {
        QTemporaryDir directory;
        SceneDocument document;
        openEmpty(directory, document);
        addRectangles(document, 3);
        document.setSelectedIndex(0);
        const auto before = itemIds(document);
        QVERIFY(document.reorderItem(0, 2));
        document.undo();
        QCOMPARE(itemIds(document), before);
        QCOMPARE(itemIds(savedScene(directory)), before);
        QCOMPARE(document.scene().items[document.selectedIndex()].id, QString("item-1"));
        QCOMPARE(document.rowCount(), 3);
    }

    void undoFollowsAnItemItsChangeMoved() {
        QTemporaryDir directory;
        SceneDocument document;
        openEmpty(directory, document);
        addRectangles(document, 3);
        QVERIFY(document.reorderItem(2, 0));
        QVERIFY(document.setItemValue(0, "name", "Moved"));
        document.setSelectedIndex(0);
        document.undo();
        document.undo();
        QCOMPARE(itemIds(document), QStringList({"item-1", "item-2", "item-3"}));
        QCOMPARE(document.selectedIndex(), 2);
        QCOMPARE(document.scene().items[document.selectedIndex()].id, QString("item-3"));
    }

    void reorderingTellsViewsAboutTheMoveNotAReset() {
        QTemporaryDir directory;
        SceneDocument document;
        openEmpty(directory, document);
        addRectangles(document, 3);
        QSignalSpy moved(&document, &QAbstractItemModel::rowsMoved);
        QSignalSpy reset(&document, &QAbstractItemModel::modelReset);
        QVERIFY(document.reorderItem(0, 2));
        QVERIFY(document.reorderItem(2, 0));
        QCOMPARE(moved.size(), 2);
        QCOMPARE(reset.size(), 0);
    }

    void reorderingNowhereIsRefusedAndAddsNoUndoStep() {
        QTemporaryDir directory;
        SceneDocument document;
        openEmpty(directory, document);
        addRectangles(document, 2);
        QVERIFY(!document.reorderItem(0, 0));
        QVERIFY(!document.reorderItem(0, 5));
        QVERIFY(!document.reorderItem(-1, 1));
        document.undo();
        QCOMPARE(document.rowCount(), 1);
    }

    void reorderingIsRefusedDuringADrag() {
        QTemporaryDir directory;
        SceneDocument document;
        openEmpty(directory, document);
        addRectangles(document, 2);
        document.beginChange();
        QVERIFY(!document.reorderItem(0, 1));
        QCOMPARE(document.duplicateItem(0), -1);
        document.endChange();
        QCOMPARE(itemIds(document), QStringList({"item-1", "item-2"}));
    }

    void forwardAndBackwardMoveOneStepAndStopAtTheEnds() {
        QTemporaryDir directory;
        SceneDocument document;
        openEmpty(directory, document);
        addRectangles(document, 3);
        QVERIFY(document.bringItemForward(0));
        QCOMPARE(itemIds(document), QStringList({"item-2", "item-1", "item-3"}));
        QVERIFY(document.sendItemBackward(1));
        QCOMPARE(itemIds(document), QStringList({"item-1", "item-2", "item-3"}));
        QVERIFY(!document.sendItemBackward(0));
        QVERIFY(!document.bringItemForward(2));
        document.undo();
        document.undo();
        document.undo();
        QCOMPARE(document.rowCount(), 2);
    }

    void toFrontAndToBackGoAllTheWay() {
        QTemporaryDir directory;
        SceneDocument document;
        openEmpty(directory, document);
        addRectangles(document, 4);
        QVERIFY(document.bringItemToFront(1));
        QCOMPARE(itemIds(document), QStringList({"item-1", "item-3", "item-4", "item-2"}));
        QVERIFY(document.sendItemToBack(2));
        QCOMPARE(itemIds(document), QStringList({"item-4", "item-1", "item-3", "item-2"}));
        QVERIFY(!document.bringItemToFront(3));
        QVERIFY(!document.sendItemToBack(0));
    }

    // The panel lists front-most first, so its rows are the reverse of the items. Gap 0 is above
    // the top row and the highest gap is below the bottom row.
    void droppingBetweenRowsPlacesTheItemThere_data() {
        QTest::addColumn<int>("index");
        QTest::addColumn<int>("gap");
        QTest::addColumn<QStringList>("expected");

        QTest::newRow("back to front") << 0 << 0 << QStringList({"item-2", "item-3", "item-4", "item-1"});
        QTest::newRow("front to back") << 3 << 4 << QStringList({"item-4", "item-1", "item-2", "item-3"});
        QTest::newRow("up one row") << 1 << 1 << QStringList({"item-1", "item-3", "item-2", "item-4"});
        QTest::newRow("down one row") << 2 << 3 << QStringList({"item-1", "item-3", "item-2", "item-4"});
        QTest::newRow("past the bottom clamps") << 2 << 99 << QStringList({"item-3", "item-1", "item-2", "item-4"});
        QTest::newRow("past the top clamps") << 1 << -5 << QStringList({"item-1", "item-3", "item-4", "item-2"});
    }

    void droppingBetweenRowsPlacesTheItemThere() {
        QFETCH(int, index);
        QFETCH(int, gap);
        QFETCH(QStringList, expected);
        QTemporaryDir directory;
        SceneDocument document;
        openEmpty(directory, document);
        addRectangles(document, 4);
        QVERIFY(document.reorderItemToGap(index, gap));
        QCOMPARE(itemIds(document), expected);
        QCOMPARE(itemIds(savedScene(directory)), expected);
    }

    void droppingAnItemNextToItselfDoesNothing() {
        QTemporaryDir directory;
        SceneDocument document;
        openEmpty(directory, document);
        addRectangles(document, 4);
        // item-2 is index 1, so panel row 2: gaps 2 and 3 are the ones touching it.
        QVERIFY(!document.reorderItemToGap(1, 2));
        QVERIFY(!document.reorderItemToGap(1, 3));
        QCOMPARE(itemIds(document), QStringList({"item-1", "item-2", "item-3", "item-4"}));
        document.undo();
        QCOMPARE(document.rowCount(), 3);
    }

    void aDuplicateSitsJustAboveItsSourceAndIsSelected() {
        QTemporaryDir directory;
        SceneDocument document;
        openEmpty(directory, document);
        addRectangles(document, 2);
        QVERIFY(document.setItemValue(0, "name", "Player"));
        QVERIFY(document.setItemValue(0, "color", "#112233"));
        QSignalSpy inserted(&document, &QAbstractItemModel::rowsInserted);
        const auto row = document.duplicateItem(0);
        QCOMPARE(row, 1);
        QCOMPARE(inserted.size(), 1);
        QCOMPARE(document.selectedIndex(), 1);
        QCOMPARE(document.rowCount(), 3);

        const auto source = document.scene().items[0];
        const auto copy = document.scene().items[1];
        QCOMPARE(copy.name, QString("Player copy"));
        QCOMPARE(copy.color, source.color);
        QCOMPARE(copy.type, source.type);
        QCOMPARE(copy.width, source.width);
        QCOMPARE(copy.x, source.x + 32);
        QCOMPARE(copy.y, source.y + 32);
        QVERIFY(copy.id != source.id);
        QCOMPARE(document.selected().value("name").toString(), QString("Player copy"));
        QCOMPARE(itemIds(savedScene(directory)), itemIds(document));
    }

    void aDuplicateIsOneUndoStep() {
        QTemporaryDir directory;
        SceneDocument document;
        openEmpty(directory, document);
        addRectangles(document, 2);
        const auto before = itemIds(document);
        QVERIFY(document.duplicateItem(0) >= 0);
        document.undo();
        QCOMPARE(itemIds(document), before);
        QCOMPARE(itemIds(savedScene(directory)), before);
        QCOMPARE(document.selectedIndex(), -1);
    }

    void duplicatingKeepsEveryIdUniqueAcrossUndo() {
        QTemporaryDir directory;
        SceneDocument document;
        openEmpty(directory, document);
        addRectangles(document, 1);
        for (int i = 0; i < 6; ++i) QVERIFY(document.duplicateItem(document.selectedIndex()) >= 0);
        document.undo();
        QVERIFY(document.duplicateItem(document.rowCount() - 1) >= 0);
        auto ids = itemIds(document);
        const auto count = ids.size();
        ids.removeDuplicates();
        QCOMPARE(ids.size(), count);
    }

    void copyNamesCountUpInsteadOfStacking_data() {
        QTest::addColumn<QString>("source");
        QTest::addColumn<QStringList>("expected");
        QTest::newRow("plain") << "Coin" << QStringList({"Coin copy", "Coin copy 2", "Coin copy 3"});
        QTest::newRow("already a copy") << "Coin copy" << QStringList({"Coin copy 2", "Coin copy 3", "Coin copy 4"});
        QTest::newRow("a numbered copy") << "Coin copy 7" << QStringList({"Coin copy 8", "Coin copy 9", "Coin copy 10"});
        QTest::newRow("a name that merely ends in copy") << "Photocopy" << QStringList({"Photocopy copy", "Photocopy copy 2", "Photocopy copy 3"});
    }

    void copyNamesCountUpInsteadOfStacking() {
        QFETCH(QString, source);
        QFETCH(QStringList, expected);
        QTemporaryDir directory;
        SceneDocument document;
        openEmpty(directory, document);
        addRectangles(document, 1);
        QVERIFY(document.setItemValue(0, "name", source));
        QStringList names;
        for (int i = 0; i < 3; ++i) {
            const auto row = document.duplicateItem(i);
            names.append(document.scene().items[row].name);
        }
        QCOMPARE(names, expected);
    }

    void anUnnamedItemCopiesUnderItsId() {
        QTemporaryDir directory;
        SceneDocument document;
        openEmpty(directory, document);
        addRectangles(document, 1);
        QVERIFY(document.setItemValue(0, "name", ""));
        const auto row = document.duplicateItem(0);
        QCOMPARE(document.scene().items[row].name, QString("item-1 copy"));
    }

    void aCopyOfALongNameStillFitsTheNameField() {
        QTemporaryDir directory;
        SceneDocument document;
        openEmpty(directory, document);
        addRectangles(document, 1);
        const QString longName(64, 'N');
        QVERIFY(document.setItemValue(0, "name", longName));
        for (int i = 0; i < 12; ++i) {
            const auto row = document.duplicateItem(0);
            QVERIFY(document.scene().items[row].name.size() <= 64);
        }
        QVERIFY(document.scene().items[1].name.endsWith(" copy 12"));
        QStringList names;
        for (const auto& item : document.scene().items) names.append(item.name);
        auto unique = names;
        unique.removeDuplicates();
        QCOMPARE(unique.size(), names.size());
    }

    void aDuplicateAtTheStageEdgeStaysOnStageAndStillOffset_data() {
        QTest::addColumn<double>("x");
        QTest::addColumn<double>("y");
        QTest::addColumn<double>("expectedX");
        QTest::addColumn<double>("expectedY");
        // A 200 by 200 rectangle: the copy steps back rather than landing exactly on top of the source.
        QTest::newRow("bottom right") << 1720.0 << 880.0 << 1688.0 << 848.0;
        QTest::newRow("right edge only") << 1700.0 << 100.0 << 1668.0 << 132.0;
        QTest::newRow("bottom edge only") << 100.0 << 860.0 << 132.0 << 828.0;
        QTest::newRow("top left") << 0.0 << 0.0 << 32.0 << 32.0;
    }

    void aDuplicateAtTheStageEdgeStaysOnStageAndStillOffset() {
        QFETCH(double, x);
        QFETCH(double, y);
        QFETCH(double, expectedX);
        QFETCH(double, expectedY);
        QTemporaryDir directory;
        SceneDocument document;
        openEmpty(directory, document);
        addRectangles(document, 1);
        document.moveItem(0, x, y);
        const auto row = document.duplicateItem(0);
        const auto copy = document.scene().items[row];
        QCOMPARE(copy.x, expectedX);
        QCOMPARE(copy.y, expectedY);
        QVERIFY(copy.x + copy.width <= 1920);
        QVERIFY(copy.y + copy.height <= 1080);
    }

    void refusesToDuplicatePastTheLimitAndSaysSo() {
        QTemporaryDir directory;
        SceneDocument document;
        openEmpty(directory, document);
        for (int i = 0; i < MaximumSceneItems; ++i) document.addItem("rectangle", 100, 100);
        document.setSelectedIndex(5);
        const auto before = fileBytes(scenePath(directory));
        QCOMPARE(document.duplicateItem(5), -1);
        QCOMPARE(document.rowCount(), MaximumSceneItems);
        QVERIFY(document.noticeIsProblem());
        QVERIFY(document.notice().contains(QString::number(MaximumSceneItems)));
        QCOMPARE(document.selectedIndex(), 5);
        QCOMPARE(fileBytes(scenePath(directory)), before);
    }

    void refusesToDuplicatePastTheParticleBudgetAndSaysSo() {
        QTemporaryDir directory;
        SceneDocument document;
        openEmpty(directory, document);
        document.addItem("particles", 500, 500);
        QVERIFY(document.setItemValue(0, "amount", 8000));
        QCOMPARE(document.duplicateItem(0), -1);
        QCOMPARE(document.rowCount(), 1);
        QVERIFY(document.noticeIsProblem());
        QVERIFY(document.notice().contains("16000"));
    }

    void aDuplicateCopiesEveryKindOfItem() {
        QTemporaryDir directory;
        SceneDocument document;
        openEmpty(directory, document);
        createFile(directory.path() + "/assets/logo.png");
        document.addImage("assets/logo.png", 100, 50, "Logo", 500, 500);
        document.addItem("text", 500, 500);
        QVERIFY(document.setItemValue(1, "text", "Score"));
        document.addItem("particles", 500, 500);
        QVERIFY(document.duplicateItem(2) >= 0);
        QVERIFY(document.duplicateItem(1) >= 0);
        QVERIFY(document.duplicateItem(0) >= 0);
        QStringList types;
        for (const auto& item : document.scene().items) types.append(item.type);
        QCOMPARE(types, QStringList({"image", "image", "text", "text", "particles", "particles"}));
        QCOMPARE(document.scene().items[1].source, QString("assets/logo.png"));
        QCOMPARE(document.scene().items[3].text, QString("Score"));
    }

    void hidingIsNotSavedAndNotUndoable() {
        QTemporaryDir directory;
        SceneDocument document;
        openEmpty(directory, document);
        addRectangles(document, 1);
        const auto before = fileBytes(scenePath(directory));

        QSignalSpy changed(&document, &QAbstractItemModel::dataChanged);
        QVERIFY(!document.isItemHidden(0));
        document.setItemHidden(0, true);
        QVERIFY(document.isItemHidden(0));
        QCOMPARE(changed.size(), 1);
        QVERIFY(document.data(document.index(0), SceneDocument::HiddenRole).toBool());
        QCOMPARE(fileBytes(scenePath(directory)), before);

        document.setItemHidden(0, false);
        QVERIFY(!document.isItemHidden(0));
        QVERIFY(!document.data(document.index(0), SceneDocument::HiddenRole).toBool());
        QCOMPARE(fileBytes(scenePath(directory)), before);

        // The only undo step is still the add, so one undo empties the scene.
        document.undo();
        QCOMPARE(document.rowCount(), 0);
    }

    void anItemMarkedHiddenWhileEditingStartsHiddenAndKeepsTheMark() {
        QTemporaryDir directory;
        SceneDocument document;
        openEmpty(directory, document);
        addRectangles(document, 2);
        auto root = QJsonDocument::fromJson(fileBytes(scenePath(directory))).object();
        auto sets = root.value("scenes").toArray();
        auto scene = sets[0].toObject();
        auto items = scene.value("items").toArray();
        auto item = items[0].toObject();
        item.insert("hiddenWhileEditing", true);
        items[0] = item;
        scene.insert("items", items);
        sets[0] = scene;
        root.insert("scenes", sets);
        QFile file(scenePath(directory));
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(QJsonDocument(root).toJson());
        file.close();

        SceneDocument reopened;
        QVERIFY(reopened.open(scenePath(directory)));
        QVERIFY(reopened.isItemHidden(0));
        QVERIFY(!reopened.isItemHidden(1));

        // The eye still works for this session and saves nothing; the mark in the file stays.
        const auto before = fileBytes(scenePath(directory));
        reopened.setItemHidden(0, false);
        QVERIFY(!reopened.isItemHidden(0));
        QCOMPARE(fileBytes(scenePath(directory)), before);

        // Any later save writes the mark back.
        reopened.addItem("rectangle", 900, 900);
        const auto saved = QJsonDocument::fromJson(fileBytes(scenePath(directory))).object();
        QVERIFY(saved.value("scenes").toArray()[0].toObject().value("items").toArray()[0].toObject().value("hiddenWhileEditing").toBool());
        QVERIFY(!saved.value("scenes").toArray()[0].toObject().value("items").toArray()[1].toObject().contains("hiddenWhileEditing"));
    }

    void hidingAlreadyHiddenChangesNothing() {
        QTemporaryDir directory;
        SceneDocument document;
        openEmpty(directory, document);
        addRectangles(document, 1);
        document.setItemHidden(0, true);
        QSignalSpy changed(&document, &QAbstractItemModel::dataChanged);
        document.setItemHidden(0, true);
        QCOMPARE(changed.size(), 0);
    }

    void hiddenFollowsTheItemWhenItMoves() {
        QTemporaryDir directory;
        SceneDocument document;
        openEmpty(directory, document);
        addRectangles(document, 3);
        document.setItemHidden(0, true);
        QVERIFY(document.reorderItem(0, 2));
        QVERIFY(!document.isItemHidden(0));
        QVERIFY(document.isItemHidden(2));
        document.undo();
        QVERIFY(document.isItemHidden(0));
        QVERIFY(!document.isItemHidden(2));
    }

    void hiddenSurvivesDeleteAndUndo() {
        QTemporaryDir directory;
        SceneDocument document;
        openEmpty(directory, document);
        addRectangles(document, 2);
        document.setItemHidden(1, true);
        document.removeItem(1);
        document.undo();
        QVERIFY(document.isItemHidden(1));
        QVERIFY(!document.isItemHidden(0));
    }

    // Ids are reused once free, so a new item must not take over the hidden state of a removed one.
    void aNewItemNeverInheritsAHiddenState() {
        QTemporaryDir directory;
        SceneDocument document;
        openEmpty(directory, document);
        addRectangles(document, 2);
        document.setItemHidden(1, true);
        document.removeItem(1);
        const auto row = document.addItem("rectangle", 900, 900);
        QVERIFY(!document.isItemHidden(row));
        document.undo();
        document.undo();
        QCOMPARE(document.rowCount(), 2);
        QVERIFY(document.isItemHidden(1));
    }

    void aCopyOfAHiddenItemStartsVisible() {
        QTemporaryDir directory;
        SceneDocument document;
        openEmpty(directory, document);
        addRectangles(document, 1);
        document.setItemHidden(0, true);
        const auto row = document.duplicateItem(0);
        QVERIFY(!document.isItemHidden(row));
        QVERIFY(document.isItemHidden(0));
    }

    void reopeningForgetsWhatWasHidden() {
        QTemporaryDir directory;
        SceneDocument document;
        openEmpty(directory, document);
        addRectangles(document, 1);
        document.setItemHidden(0, true);
        QVERIFY(document.open(scenePath(directory)));
        QVERIFY(!document.isItemHidden(0));
    }

    void renamingGoesThroughUndoAndAutosave() {
        QTemporaryDir directory;
        SceneDocument document;
        openEmpty(directory, document);
        addRectangles(document, 1);
        const auto before = document.scene().items[0].name;
        QVERIFY(document.setItemValue(0, "name", "Hero"));
        QCOMPARE(savedScene(directory).items[0].name, QString("Hero"));
        document.undo();
        QCOMPARE(document.scene().items[0].name, before);
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
    void theStageShowsInitialValuesAndTheInspectorTheRealText() {
        QTemporaryDir directory;
        SceneDocument document;
        openEmpty(directory, document);
        document.addItem("text", 900, 500);
        QVERIFY(document.setItemValue(0, "text", "Score: {score}"));
        QVERIFY(document.setItemValue(0, "scale", 2));
        document.setVariables({{"score", 7}});
        const auto index = document.index(0);
        QCOMPARE(document.data(index, SceneDocument::TextRole).toString(), QString("Score: 7"));
        QCOMPARE(document.data(index, SceneDocument::WidthRole).toDouble(), (8.0 * 6 - 1) * 2);
        QCOMPARE(document.selected().value("text").toString(), QString("Score: {score}"));
        QCOMPARE(document.scene().items[0].text, QString("Score: {score}"));
        QCOMPARE(savedScene(directory).items[0].text, QString("Score: {score}"));
    }

    void anUnknownVariableStaysVisibleOnTheStage() {
        QTemporaryDir directory;
        SceneDocument document;
        openEmpty(directory, document);
        document.addItem("text", 900, 500);
        QVERIFY(document.setItemValue(0, "text", "{ghost}"));
        QCOMPARE(document.data(document.index(0), SceneDocument::TextRole).toString(), QString("{ghost}"));
    }

    void literalBracesShowOnceOnTheStage() {
        QTemporaryDir directory;
        SceneDocument document;
        openEmpty(directory, document);
        document.addItem("text", 900, 500);
        QVERIFY(document.setItemValue(0, "text", "{{hi}}"));
        QCOMPARE(document.data(document.index(0), SceneDocument::TextRole).toString(), QString("{hi}"));
        QCOMPARE(document.data(document.index(0), SceneDocument::WidthRole).toDouble(), (4.0 * 6 - 1) * 8);
    }

    void changingAnInitialValueRepaintsTheTextThatShowsIt() {
        QTemporaryDir directory;
        SceneDocument document;
        openEmpty(directory, document);
        document.addItem("text", 900, 500);
        document.addItem("text", 900, 700);
        QVERIFY(document.setItemValue(0, "text", "{score}"));
        document.setVariables({{"score", 1}});
        QSignalSpy spy(&document, &QAbstractItemModel::dataChanged);
        document.setVariables({{"score", 2}});
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).value<QModelIndex>().row(), 0);
        QCOMPARE(document.data(document.index(0), SceneDocument::TextRole).toString(), QString("2"));
        spy.clear();
        document.setVariables({{"score", 2}});
        QCOMPARE(spy.count(), 0);
    }

    void theInspectorCanListVariables() {
        QTemporaryDir directory;
        SceneDocument document;
        openEmpty(directory, document);
        document.setVariables({{"score", 0}, {"lives", 3}});
        QCOMPARE(document.variableNames(), QStringList({"score", "lives"}));
    }

    void renamingAVariableUpdatesTextAndUndoHistory() {
        QTemporaryDir directory;
        SceneDocument document;
        openEmpty(directory, document);
        document.addItem("text", 900, 500);
        QVERIFY(document.setItemValue(0, "text", "S: {score}"));
        document.setVariables({{"score", 1}});
        QVERIFY(document.setItemValue(0, "scale", 2));
        document.renameVariable("score", "points");
        document.setVariables({{"points", 1}});
        QCOMPARE(document.scene().items[0].text, QString("S: {points}"));
        QCOMPARE(savedScene(directory).items[0].text, QString("S: {points}"));
        QCOMPARE(document.data(document.index(0), SceneDocument::TextRole).toString(), QString("S: 1"));
        document.undo();
        QCOMPARE(document.scene().items[0].text, QString("S: {points}"));
        QCOMPARE(document.scene().items[0].scale, 8);
    }

    void renamingANameNothingUsesChangesNothing() {
        QTemporaryDir directory;
        SceneDocument document;
        openEmpty(directory, document);
        document.addItem("text", 900, 500);
        QVERIFY(document.setItemValue(0, "text", "Hello"));
        document.renameVariable("score", "points");
        QCOMPARE(document.scene().items[0].text, QString("Hello"));
    }
};

QTEST_GUILESS_MAIN(SceneDocumentTests)
#include "tst_scene_document.moc"
