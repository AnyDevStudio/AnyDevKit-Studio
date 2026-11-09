#include "GamePage.h"
#include "GamePageDocument.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace Studio;

namespace {

// The layout `adk init` writes, including its hand-formatted language line.
const QByteArray sampleParam =
    "{\n"
    "  \"titleId\": \"FAKE95558\",\n"
    "  \"attribute3\": 64,\n"
    "  \"requiredSystemSoftwareVersion\": \"0x0200000000000000\",\n"
    "  \"note\": \"a } b { \\\"en-US\\\": {\\\"titleName\\\": \\\"Decoy\\\"}\",\n"
    "  \"localizedParameters\": {\n"
    "    \"defaultLanguage\": \"en-US\",\n"
    "    \"tr-TR\": {\"titleName\": \"Oyunum\"},\n"
    "    \"en-US\": {\"titleName\": \"MyApp\"}\n"
    "  }\n"
    "}\n";

QJsonObject parse(const QByteArray& json) {
    return QJsonDocument::fromJson(json).object();
}

QString defaultTitle(const QByteArray& json) {
    const auto localized = parse(json).value("localizedParameters").toObject();
    return localized.value(localized.value("defaultLanguage").toString()).toObject().value("titleName").toString();
}

void writeBytes(const QString& path, const QByteArray& bytes) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(bytes);
}

QByteArray readBytes(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QString writeSourcePng(const QTemporaryDir& directory, const QString& name, const QSize& size, const QColor& color) {
    QImage image(size, QImage::Format_ARGB32);
    image.fill(color);
    const auto path = directory.filePath(name);
    if (!image.save(path)) return {};
    return path;
}

GameImageTarget target(GameImageKind kind) {
    return gameImageTarget(kind);
}

}

class GamePageTests : public QObject {
    Q_OBJECT

private slots:
    void targetsMatchWhatAnyDevKitPackages() {
        QCOMPARE(target(GameImageKind::Icon).fileName, QString("icon0.png"));
        QCOMPARE(target(GameImageKind::Icon).size, QSize(512, 512));
        QCOMPARE(target(GameImageKind::Background).fileName, QString("pic0.png"));
        QCOMPARE(target(GameImageKind::Background).size, QSize(3840, 2160));
        QCOMPARE(target(GameImageKind::Splash).fileName, QString("pic1.png"));
        QCOMPARE(target(GameImageKind::Splash).size, QSize(3840, 2160));
    }

    void centerCropKeepsTheTargetProportions_data() {
        QTest::addColumn<QSize>("source");
        QTest::addColumn<QSize>("destination");
        QTest::addColumn<QRect>("expected");

        QTest::newRow("wide source, square target") << QSize(4000, 2000) << QSize(512, 512) << QRect(1000, 0, 2000, 2000);
        QTest::newRow("tall source, square target") << QSize(2000, 3000) << QSize(512, 512) << QRect(0, 500, 2000, 2000);
        QTest::newRow("portrait photo, wide target") << QSize(1080, 1920) << QSize(3840, 2160) << QRect(0, 656, 1080, 608);
        QTest::newRow("matching proportions") << QSize(7680, 4320) << QSize(3840, 2160) << QRect(0, 0, 7680, 4320);
        QTest::newRow("exact size") << QSize(3840, 2160) << QSize(3840, 2160) << QRect(0, 0, 3840, 2160);
        QTest::newRow("sliver") << QSize(3, 2048) << QSize(512, 512) << QRect(0, 1022, 3, 3);
        QTest::newRow("one pixel") << QSize(1, 1) << QSize(3840, 2160) << QRect(0, 0, 1, 1);
        QTest::newRow("empty source") << QSize(0, 0) << QSize(512, 512) << QRect();
    }

    void centerCropKeepsTheTargetProportions() {
        QFETCH(QSize, source);
        QFETCH(QSize, destination);
        QFETCH(QRect, expected);
        const auto crop = centerCropRect(source, destination);
        QCOMPARE(crop, expected);
        if (!expected.isNull()) QVERIFY(QRect(QPoint(0, 0), source).contains(crop));
    }

    void fittingCropsInsteadOfStretching() {
        // Red bands on the left and right would show up in a square if the image were squeezed.
        QImage wide(400, 200, QImage::Format_RGB32);
        wide.fill(Qt::red);
        QPainter(&wide).fillRect(100, 0, 200, 200, Qt::green);
        const auto fitted = fitGameImage(wide, QSize(64, 64));
        QCOMPARE(fitted.size(), QSize(64, 64));
        for (const auto& point : {QPoint(0, 0), QPoint(63, 63), QPoint(0, 63), QPoint(63, 0), QPoint(32, 32)}) QCOMPARE(fitted.pixelColor(point), QColor(Qt::green));
    }

    void fittingFlattensTransparencyOntoBlack() {
        QImage clear(32, 32, QImage::Format_ARGB32);
        clear.fill(QColor(255, 255, 255, 0));
        const auto fitted = fitGameImage(clear, QSize(16, 16));
        QCOMPARE(fitted.pixelColor(8, 8), QColor(Qt::black));
    }

    void editsOnlyTheDefaultTitle() {
        const auto edit = withDefaultTitle(sampleParam, "New Game");
        QVERIFY2(edit.error.isEmpty(), qPrintable(edit.error));
        QByteArray expected = sampleParam;
        expected.replace("{\"titleName\": \"MyApp\"}", "{\"titleName\": \"New Game\"}");
        QCOMPARE(edit.json, expected);
    }

    void escapesTitlesSafely() {
        const QString title = QString::fromUtf8("Say \"hi\" \\ Oyunum şğü");
        const auto edit = withDefaultTitle(sampleParam, title);
        QVERIFY2(edit.error.isEmpty(), qPrintable(edit.error));
        QCOMPARE(defaultTitle(edit.json), title);
        QCOMPARE(parse(edit.json).value("note").toString(), parse(sampleParam).value("note").toString());
        QVERIFY2(edit.json.contains(QString::fromUtf8("şğü").toUtf8()), edit.json.constData());
    }

    void trimsSurroundingSpaces() {
        const auto edit = withDefaultTitle(sampleParam, "  Spaced Out  ");
        QVERIFY2(edit.error.isEmpty(), qPrintable(edit.error));
        QCOMPARE(defaultTitle(edit.json), QString("Spaced Out"));
    }

    void rejectsTitlesAnyDevKitWouldRefuse_data() {
        QTest::addColumn<QString>("title");
        QTest::newRow("empty") << QString();
        QTest::newRow("blank") << QString("   ");
        QTest::newRow("control") << QString("Bad\x07Name");
        QTest::newRow("delete") << QString("Bad\x7fName");
    }

    void rejectsTitlesAnyDevKitWouldRefuse() {
        QFETCH(QString, title);
        const auto edit = withDefaultTitle(sampleParam, title);
        QVERIFY(!edit.error.isEmpty());
        QVERIFY(edit.json.isEmpty());
    }

    void explainsBrokenMetadata_data() {
        QTest::addColumn<QByteArray>("json");
        QTest::newRow("not json") << QByteArray("{ nope");
        QTest::newRow("no languages") << QByteArray("{\"titleId\": \"A\"}");
        QTest::newRow("no default language") << QByteArray("{\"localizedParameters\": {\"en-US\": {\"titleName\": \"A\"}}}");
    }

    void explainsBrokenMetadata() {
        QFETCH(QByteArray, json);
        const auto edit = withDefaultTitle(json, "Name");
        QVERIFY(!edit.error.isEmpty());
        QVERIFY(edit.json.isEmpty());
    }

    void addsAMissingTitleWithoutLosingFields() {
        const QByteArray json = "{\"titleId\": \"A\", \"attribute\": 7, \"localizedParameters\": {\"defaultLanguage\": \"en-US\", \"fr-FR\": {\"titleName\": \"Jeu\"}}}";
        const auto edit = withDefaultTitle(json, "Added");
        QVERIFY2(edit.error.isEmpty(), qPrintable(edit.error));
        QCOMPARE(defaultTitle(edit.json), QString("Added"));
        QCOMPARE(parse(edit.json).value("attribute").toInt(), 7);
        QCOMPARE(parse(edit.json).value("titleId").toString(), QString("A"));
        QCOMPARE(parse(edit.json).value("localizedParameters").toObject().value("fr-FR").toObject().value("titleName").toString(), QString("Jeu"));
    }

    void installsAnImageAtTheExactSize() {
        QTemporaryDir directory;
        const auto source = writeSourcePng(directory, "photo.png", QSize(900, 300), Qt::blue);
        const auto project = directory.filePath("project");
        QVERIFY(QDir().mkpath(project + "/app0/sce_sys"));
        const auto error = installGameImage(source, project, project + "/app0/sce_sys", target(GameImageKind::Icon));
        QVERIFY2(error.isEmpty(), qPrintable(error));
        const QImage written(project + "/app0/sce_sys/icon0.png");
        QCOMPARE(written.size(), QSize(512, 512));
        QCOMPARE(written.pixelColor(256, 256), QColor(Qt::blue));
    }

    void createsTheSystemFolderWhenNeeded() {
        QTemporaryDir directory;
        const auto source = writeSourcePng(directory, "photo.png", QSize(64, 64), Qt::red);
        const auto project = directory.filePath("project");
        QVERIFY(QDir().mkpath(project + "/app0"));
        const auto error = installGameImage(source, project, project + "/app0/sce_sys", target(GameImageKind::Icon));
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(QFile::exists(project + "/app0/sce_sys/icon0.png"));
    }

    void refusesAFileThatIsNotAnImage() {
        QTemporaryDir directory;
        const auto source = directory.filePath("notes.txt");
        writeBytes(source, "this is not a picture");
        const auto project = directory.filePath("project");
        QVERIFY(QDir().mkpath(project + "/app0/sce_sys"));
        const auto error = installGameImage(source, project, project + "/app0/sce_sys", target(GameImageKind::Icon));
        QVERIFY(error.contains("notes.txt"));
        QVERIFY(!QFile::exists(project + "/app0/sce_sys/icon0.png"));
    }

    void explainsAPictureThatIsTooLarge() {
        QTemporaryDir directory;
        const auto source = writeSourcePng(directory, "huge.png", QSize(2000, 2000), Qt::red);
        const auto project = directory.filePath("project");
        QVERIFY(QDir().mkpath(project + "/app0/sce_sys"));
        const auto limit = QImageReader::allocationLimit();
        QImageReader::setAllocationLimit(1);
        const auto error = installGameImage(source, project, project + "/app0/sce_sys", target(GameImageKind::Icon));
        QImageReader::setAllocationLimit(limit);
        QVERIFY2(error.contains("huge.png") && error.contains("too large"), qPrintable(error));
        QVERIFY(!QFile::exists(project + "/app0/sce_sys/icon0.png"));
    }

#ifndef Q_OS_WIN
    void refusesToWriteThroughALink() {
        QTemporaryDir directory;
        const auto source = writeSourcePng(directory, "photo.png", QSize(64, 64), Qt::red);
        const auto outside = directory.filePath("outside");
        QVERIFY(QDir().mkpath(outside));
        const auto project = directory.filePath("project");
        QVERIFY(QDir().mkpath(project + "/app0"));
        QVERIFY(QFile::link(outside, project + "/app0/sce_sys"));
        const auto error = installGameImage(source, project, project + "/app0/sce_sys", target(GameImageKind::Icon));
        QVERIFY(!error.isEmpty());
        QVERIFY(!QFile::exists(outside + "/icon0.png"));
    }

    void refusesToReplaceAFileThatIsALink() {
        QTemporaryDir directory;
        const auto source = writeSourcePng(directory, "photo.png", QSize(64, 64), Qt::red);
        const auto outside = directory.filePath("outside.png");
        writeBytes(outside, "untouched");
        const auto project = directory.filePath("project");
        QVERIFY(QDir().mkpath(project + "/app0/sce_sys"));
        QVERIFY(QFile::link(outside, project + "/app0/sce_sys/icon0.png"));
        const auto error = installGameImage(source, project, project + "/app0/sce_sys", target(GameImageKind::Icon));
        QVERIFY(!error.isEmpty());
        QCOMPARE(readBytes(outside), QByteArray("untouched"));
    }
#endif

    void refusesATargetOutsideTheProject() {
        QTemporaryDir directory;
        const auto source = writeSourcePng(directory, "photo.png", QSize(64, 64), Qt::red);
        const auto project = directory.filePath("project");
        QVERIFY(QDir().mkpath(project));
        const auto error = installGameImage(source, project, directory.filePath("elsewhere"), target(GameImageKind::Icon));
        QVERIFY(!error.isEmpty());
        QVERIFY(!QFile::exists(directory.filePath("elsewhere/icon0.png")));
    }

    void reportsWhenTheFileCannotBeWritten() {
        QTemporaryDir directory;
        const auto source = writeSourcePng(directory, "photo.png", QSize(64, 64), Qt::red);
        const auto project = directory.filePath("project");
        // A folder where the file should go cannot be replaced.
        QVERIFY(QDir().mkpath(project + "/app0/sce_sys/icon0.png"));
        const auto error = installGameImage(source, project, project + "/app0/sce_sys", target(GameImageKind::Icon));
        QVERIFY(error.contains("icon0.png"));
    }

    void removesAnImageFromTheProject() {
        QTemporaryDir directory;
        const auto project = directory.filePath("project");
        writeBytes(project + "/app0/sce_sys/pic1.png", "x");
        auto error = removeGameImage(project, project + "/app0/sce_sys", target(GameImageKind::Splash));
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(!QFile::exists(project + "/app0/sce_sys/pic1.png"));
        error = removeGameImage(project, project + "/app0/sce_sys", target(GameImageKind::Splash));
        QVERIFY2(error.isEmpty(), qPrintable(error));
    }

    void documentLoadsWhatTheProjectAlreadyHas() {
        QTemporaryDir directory;
        const auto project = directory.filePath("project");
        writeBytes(project + "/app0/sce_sys/param.json", sampleParam);
        QVERIFY(QImage(QSize(4, 4), QImage::Format_RGB32).save(project + "/app0/sce_sys/icon0.png"));
        GamePageDocument document;
        QSignalSpy changes(&document, &GamePageDocument::changed);
        document.open(project, project + "/app0", project + "/app0/sce_sys/param.json", "MyApp");
        QVERIFY(document.isLoaded());
        QCOMPARE(document.title(), QString("MyApp"));
        QVERIFY(document.iconUrl().isLocalFile() && document.iconUrl().toLocalFile().endsWith("icon0.png"));
        QVERIFY(document.backgroundUrl().isEmpty());
        QVERIFY(document.splashUrl().isEmpty());
        QVERIFY(!changes.isEmpty());
    }

    void documentSavesTheTitleToTheProject() {
        QTemporaryDir directory;
        const auto project = directory.filePath("project");
        writeBytes(project + "/app0/sce_sys/param.json", sampleParam);
        GamePageDocument document;
        document.open(project, project + "/app0", project + "/app0/sce_sys/param.json", "MyApp");
        QSignalSpy saved(&document, &GamePageDocument::titleSaved);
        document.setTitle("Fresh Title");
        QVERIFY(document.error().isEmpty());
        QCOMPARE(document.title(), QString("Fresh Title"));
        QCOMPARE(saved.size(), 1);
        QCOMPARE(saved.at(0).at(0).toString(), QString("Fresh Title"));
        const auto written = readBytes(project + "/app0/sce_sys/param.json");
        QCOMPARE(defaultTitle(written), QString("Fresh Title"));
        QCOMPARE(parse(written).value("titleId").toString(), QString("FAKE95558"));
    }

    void documentTellsWhenTheTitleCannotBeSaved() {
        QTemporaryDir directory;
        const auto project = directory.filePath("project");
        writeBytes(project + "/app0/sce_sys/param.json", "{ not json");
        GamePageDocument document;
        document.open(project, project + "/app0", project + "/app0/sce_sys/param.json", "MyApp");
        document.setTitle("Fresh Title");
        QVERIFY(!document.error().isEmpty());
        QCOMPARE(document.title(), QString("MyApp"));
        QCOMPARE(readBytes(project + "/app0/sce_sys/param.json"), QByteArray("{ not json"));
    }

    void documentInstallsImagesInTheBackground() {
        QTemporaryDir directory;
        const auto source = writeSourcePng(directory, "photo.png", QSize(900, 300), Qt::green);
        const auto project = directory.filePath("project");
        writeBytes(project + "/app0/sce_sys/param.json", sampleParam);
        GamePageDocument document;
        document.open(project, project + "/app0", project + "/app0/sce_sys/param.json", "MyApp");
        QSignalSpy saved(&document, &GamePageDocument::filesChanged);
        const auto before = document.iconUrl();
        document.chooseImage("icon", QUrl::fromLocalFile(source));
        QVERIFY(document.isBusy());
        QTRY_VERIFY_WITH_TIMEOUT(!document.isBusy(), 20000);
        QVERIFY2(document.error().isEmpty(), qPrintable(document.error()));
        QCOMPARE(saved.size(), 1);
        QVERIFY(!document.iconUrl().isEmpty());
        QVERIFY(document.iconUrl() != before);
        QCOMPARE(QImage(project + "/app0/sce_sys/icon0.png").size(), QSize(512, 512));
    }

    void documentChangesTheImageUrlWhenAnImageIsReplaced() {
        QTemporaryDir directory;
        const auto source = writeSourcePng(directory, "photo.png", QSize(64, 64), Qt::green);
        const auto project = directory.filePath("project");
        writeBytes(project + "/app0/sce_sys/param.json", sampleParam);
        GamePageDocument document;
        document.open(project, project + "/app0", project + "/app0/sce_sys/param.json", "MyApp");
        document.chooseImage("icon", QUrl::fromLocalFile(source));
        QTRY_VERIFY_WITH_TIMEOUT(!document.isBusy(), 20000);
        const auto first = document.iconUrl();
        document.chooseImage("icon", QUrl::fromLocalFile(source));
        QTRY_VERIFY_WITH_TIMEOUT(!document.isBusy(), 20000);
        QVERIFY(document.iconUrl() != first);
    }

    void documentReportsFailedImages() {
        QTemporaryDir directory;
        const auto source = directory.filePath("notes.txt");
        writeBytes(source, "not a picture");
        const auto project = directory.filePath("project");
        writeBytes(project + "/app0/sce_sys/param.json", sampleParam);
        GamePageDocument document;
        document.open(project, project + "/app0", project + "/app0/sce_sys/param.json", "MyApp");
        document.chooseImage("background", QUrl::fromLocalFile(source));
        QTRY_VERIFY_WITH_TIMEOUT(!document.isBusy(), 20000);
        QVERIFY(document.error().contains("notes.txt"));
        QVERIFY(document.backgroundUrl().isEmpty());
    }

    void documentClearsTheSplashSoTheBackgroundIsUsed() {
        QTemporaryDir directory;
        const auto project = directory.filePath("project");
        writeBytes(project + "/app0/sce_sys/param.json", sampleParam);
        writeBytes(project + "/app0/sce_sys/pic1.png", "x");
        GamePageDocument document;
        document.open(project, project + "/app0", project + "/app0/sce_sys/param.json", "MyApp");
        QVERIFY(!document.splashUrl().isEmpty());
        document.removeSplash();
        QVERIFY(document.error().isEmpty());
        QVERIFY(document.splashUrl().isEmpty());
        QVERIFY(!QFile::exists(project + "/app0/sce_sys/pic1.png"));
    }

    void documentRefusesChangesWhileBuilding() {
        QTemporaryDir directory;
        const auto project = directory.filePath("project");
        writeBytes(project + "/app0/sce_sys/param.json", sampleParam);
        const auto source = writeSourcePng(directory, "photo.png", QSize(64, 64), Qt::green);
        GamePageDocument document;
        document.open(project, project + "/app0", project + "/app0/sce_sys/param.json", "MyApp");
        document.setBuilding(true);
        document.setTitle("Too Late");
        QVERIFY(!document.error().isEmpty());
        QCOMPARE(defaultTitle(readBytes(project + "/app0/sce_sys/param.json")), QString("MyApp"));
        document.chooseImage("icon", QUrl::fromLocalFile(source));
        QVERIFY(!document.isBusy());
        QVERIFY(!QFile::exists(project + "/app0/sce_sys/icon0.png"));
    }

    void documentForgetsTheProjectWhenClosed() {
        QTemporaryDir directory;
        const auto project = directory.filePath("project");
        writeBytes(project + "/app0/sce_sys/param.json", sampleParam);
        GamePageDocument document;
        document.open(project, project + "/app0", project + "/app0/sce_sys/param.json", "MyApp");
        document.close();
        QVERIFY(!document.isLoaded());
        QVERIFY(document.title().isEmpty());
    }
};

QTEST_GUILESS_MAIN(GamePageTests)
#include "tst_gamepage.moc"
