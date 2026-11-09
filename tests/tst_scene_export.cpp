#include "SceneExport.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QtTest>

using namespace Studio;

namespace {

SceneItem imageItem(const QString& id, const QString& source) {
    SceneItem item;
    item.id = id;
    item.type = "image";
    item.name = id;
    item.width = 16;
    item.height = 16;
    item.source = source;
    return item;
}

void writePng(const QString& path, const QImage& image) {
    QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
    QVERIFY(image.save(path, "PNG"));
}

QByteArray readBytes(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

}

class SceneExportTests : public QObject {
    Q_OBJECT

private slots:
    void decodesToPremultipliedRgba8TopRowFirst() {
        QTemporaryDir directory;
        const auto root = directory.path();
        QImage image(2, 2, QImage::Format_RGBA8888);
        image.setPixelColor(0, 0, QColor(255, 0, 0, 255));
        image.setPixelColor(1, 0, QColor(0, 255, 0, 128));
        image.setPixelColor(0, 1, QColor(0, 0, 255, 255));
        image.setPixelColor(1, 1, QColor(10, 20, 30, 0));
        writePng(root + "/assets/test.png", image);

        Scene scene;
        scene.items = {imageItem("a", "assets/test.png")};
        QList<SceneImageData> images;
        QString error;
        QVERIFY2(decodeSceneImages(root, scene, images, error), qPrintable(error));
        QCOMPARE(images.size(), 1);
        QCOMPARE(images[0].source, QString("assets/test.png"));
        QCOMPARE(images[0].width, 2);
        QCOMPARE(images[0].height, 2);
        const QByteArray expected("\xff\x00\x00\xff" "\x00\x80\x00\x80" "\x00\x00\xff\xff" "\x00\x00\x00\x00", 16);
        QCOMPARE(images[0].pixels, expected);
    }

    void acceptsPalettedAndGrayscalePngs() {
        QTemporaryDir directory;
        const auto root = directory.path();
        QImage gray(1, 1, QImage::Format_Grayscale8);
        gray.setPixelColor(0, 0, QColor(50, 50, 50));
        writePng(root + "/assets/gray.png", gray);
        QImage indexed(1, 1, QImage::Format_Indexed8);
        indexed.setColorTable({qRgb(1, 2, 3)});
        indexed.setPixel(0, 0, 0);
        writePng(root + "/assets/indexed.png", indexed);

        Scene scene;
        scene.items = {imageItem("a", "assets/gray.png"), imageItem("b", "assets/indexed.png")};
        QList<SceneImageData> images;
        QString error;
        QVERIFY2(decodeSceneImages(root, scene, images, error), qPrintable(error));
        QCOMPARE(images[0].pixels, QByteArray("\x32\x32\x32\xff", 4));
        QCOMPARE(images[1].pixels, QByteArray("\x01\x02\x03\xff", 4));
    }

    void decodesEachSourceOnce() {
        QTemporaryDir directory;
        const auto root = directory.path();
        writePng(root + "/assets/one.png", QImage(4, 4, QImage::Format_RGBA8888));
        Scene scene;
        scene.items = {imageItem("a", "assets/one.png"), imageItem("b", "assets/one.png")};
        QList<SceneImageData> images;
        QString error;
        QVERIFY2(decodeSceneImages(root, scene, images, error), qPrintable(error));
        QCOMPARE(images.size(), 1);
    }

    void explainsUnusableFiles() {
        QTemporaryDir directory;
        const auto root = directory.path();
        writePng(root + "/assets/wide.png", QImage(4097, 1, QImage::Format_RGBA8888));
        QVERIFY(QDir(root + "/assets").mkpath("."));
        QFile bad(root + "/assets/bad.png");
        QVERIFY(bad.open(QIODevice::WriteOnly));
        bad.write("this is not a png");
        bad.close();

        const struct {
            const char* source;
            const char* expected;
        } cases[] = {
            {"assets/missing.png", "missing.png"},
            {"assets/bad.png", "bad.png"},
            {"assets/wide.png", "4096"},
        };
        for (const auto& entry : cases) {
            Scene scene;
            scene.items = {imageItem("a", entry.source)};
            QList<SceneImageData> images;
            QString error;
            QVERIFY2(!decodeSceneImages(root, scene, images, error), entry.source);
            QVERIFY2(error.contains(QLatin1String(entry.expected)), qPrintable(error));
        }
    }

    void refusesAPngInDisguiseWhenItIsAnotherFormat() {
        QTemporaryDir directory;
        const auto root = directory.path();
        QVERIFY(QDir(root).mkpath("assets"));
        QImage image(2, 2, QImage::Format_RGBA8888);
        image.fill(Qt::red);
        QVERIFY(image.save(root + "/assets/photo.png", "BMP"));
        Scene scene;
        scene.items = {imageItem("a", "assets/photo.png")};
        QList<SceneImageData> images;
        QString error;
        QVERIFY(!decodeSceneImages(root, scene, images, error));
        QVERIFY2(error.contains("PNG"), qPrintable(error));
    }

    void exportsTheSceneToProjectFiles() {
        QTemporaryDir directory;
        const auto root = directory.path();
        QImage image(2, 1, QImage::Format_RGBA8888);
        image.setPixelColor(0, 0, QColor(255, 0, 0, 255));
        image.setPixelColor(1, 0, QColor(0, 0, 255, 255));
        writePng(root + "/assets/a.png", image);
        Scene scene;
        scene.items = {imageItem("a", "assets/a.png")};
        QString error;
        QVERIFY2(exportScene(root, scene, error), qPrintable(error));
        QCOMPARE(readBytes(root + "/src/GeneratedImages/image-0.rgba"), QByteArray("\xff\x00\x00\xff\x00\x00\xff\xff", 8));
        QVERIFY(readBytes(root + "/src/GeneratedScene.hpp").contains("ImageCount = 1;"));
    }

    void exportingAnUnreadableImageWritesNothing() {
        QTemporaryDir directory;
        const auto root = directory.path();
        Scene scene;
        scene.items = {imageItem("a", "assets/missing.png")};
        QString error;
        QVERIFY(!exportScene(root, scene, error));
        QVERIFY2(error.contains("missing.png"), qPrintable(error));
        QVERIFY(!QFile::exists(root + "/src/GeneratedScene.hpp"));
    }

    void exportsASceneWithoutImages() {
        QTemporaryDir directory;
        Scene scene;
        QString error;
        QVERIFY2(exportScene(directory.path(), scene, error), qPrintable(error));
        QVERIFY(readBytes(directory.path() + "/src/GeneratedScene.hpp").contains("ImageCount = 0;"));
        QVERIFY(readBytes(directory.path() + "/src/GeneratedImages.cpp").contains("Images{}"));
    }
};

QTEST_GUILESS_MAIN(SceneExportTests)
#include "tst_scene_export.moc"
