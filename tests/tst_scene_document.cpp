#include "SceneDocument.h"

#include <QFile>
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
