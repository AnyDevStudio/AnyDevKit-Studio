#include "SceneExport.h"
#include "SceneImageImport.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
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

    void importConvertsAPngIntoTheAssetsFolder() {
        QTemporaryDir project, desktop;
        QImage image(20, 10, QImage::Format_RGBA8888);
        image.fill(QColor(255, 0, 0, 128));
        writePng(desktop.path() + "/Logo.png", image);

        ImportedSceneImage imported;
        QString error;
        QVERIFY2(importSceneImage(project.path(), {}, desktop.path() + "/Logo.png", imported, error), qPrintable(error));
        QCOMPARE(imported.source, QString("assets/logo.png"));
        QCOMPARE(imported.name, QString("Logo"));
        QCOMPARE(imported.width, 20);
        QCOMPARE(imported.height, 10);
        QVERIFY(imported.notice.isEmpty());

        const QImage saved(project.path() + "/assets/logo.png");
        QCOMPARE(saved.size(), QSize(20, 10));
        QVERIFY(saved.hasAlphaChannel());
        QCOMPARE(saved.pixelColor(0, 0).alpha(), 128);
    }

    void importTurnsAJpegIntoAPng() {
        QTemporaryDir project, desktop;
        QImage image(8, 8, QImage::Format_RGB32);
        image.fill(Qt::blue);
        QVERIFY(image.save(desktop.path() + "/Sky.JPG", "JPEG"));

        ImportedSceneImage imported;
        QString error;
        QVERIFY2(importSceneImage(project.path(), {}, desktop.path() + "/Sky.JPG", imported, error), qPrintable(error));
        QCOMPARE(imported.source, QString("assets/sky.png"));
        QByteArray expected("\x89PNG", 4);
        QCOMPARE(readBytes(project.path() + "/assets/sky.png").left(4), expected);
    }

    void importFollowsAJpegsRotation() {
        QTemporaryDir project, desktop;
        QImage image(40, 20, QImage::Format_RGB32);
        image.fill(Qt::green);
        QImageWriter writer(desktop.path() + "/phone.jpg", "jpeg");
        writer.setTransformation(QImageIOHandler::TransformationRotate90);
        QVERIFY2(writer.write(image), qPrintable(writer.errorString()));

        ImportedSceneImage imported;
        QString error;
        QVERIFY2(importSceneImage(project.path(), {}, desktop.path() + "/phone.jpg", imported, error), qPrintable(error));
        QCOMPARE(imported.width, 20);
        QCOMPARE(imported.height, 40);
    }

    void importGivesEveryFileASafeUniqueName() {
        QTemporaryDir project, desktop;
        QImage first(4, 4, QImage::Format_RGBA8888);
        first.fill(Qt::red);
        QImage second(4, 4, QImage::Format_RGBA8888);
        second.fill(Qt::green);

        const struct {
            const char* file;
            const char* expected;
        } cases[] = {
            {"My Photo (1)!.png", "assets/my-photo-1.png"},
            {"G\xC3\xB6rsel 2.png", "assets/g-rsel-2.png"},
            {"\xE5\x9B\xBE.png", "assets/image.png"},
            {".hidden.png", "assets/hidden.png"},
#ifndef Q_OS_WIN
            // Windows reserves this name, so the source picture cannot exist there.
            {"con.png", "assets/image-con.png"},
#endif
        };
        auto colorIndex = 0;
        for (const auto& entry : cases) {
            auto image = first.copy();
            image.setPixelColor(0, 0, QColor(colorIndex++, 0, 0));
            const auto path = desktop.path() + '/' + QString::fromUtf8(entry.file);
            writePng(path, image);
            ImportedSceneImage imported;
            QString error;
            QVERIFY2(importSceneImage(project.path(), {}, path, imported, error), qPrintable(error));
            QCOMPARE(imported.source, QString(entry.expected));
            QVERIFY(QFile::exists(project.path() + '/' + imported.source));
        }

        writePng(desktop.path() + "/one/logo.png", first);
        writePng(desktop.path() + "/two/logo.png", second);
        ImportedSceneImage imported;
        QString error;
        QVERIFY2(importSceneImage(project.path(), {}, desktop.path() + "/one/logo.png", imported, error), qPrintable(error));
        QCOMPARE(imported.source, QString("assets/logo.png"));
        QVERIFY2(importSceneImage(project.path(), {}, desktop.path() + "/two/logo.png", imported, error), qPrintable(error));
        QCOMPARE(imported.source, QString("assets/logo-2.png"));
    }

    void importingTheSamePictureTwiceReusesTheFile() {
        QTemporaryDir project, desktop;
        QImage image(4, 4, QImage::Format_RGBA8888);
        image.fill(Qt::red);
        writePng(desktop.path() + "/logo.png", image);
        ImportedSceneImage imported;
        QString error;
        QVERIFY2(importSceneImage(project.path(), {}, desktop.path() + "/logo.png", imported, error), qPrintable(error));
        QVERIFY2(importSceneImage(project.path(), {}, desktop.path() + "/logo.png", imported, error), qPrintable(error));
        QCOMPARE(imported.source, QString("assets/logo.png"));
        QCOMPARE(QDir(project.path() + "/assets").entryList(QDir::Files).size(), 1);
    }

    void importScalesDownPastTheLimitAndSaysSo() {
        QTemporaryDir project, desktop;
        QImage wide(5000, 1000, QImage::Format_RGBA8888);
        wide.fill(QColor(0, 255, 0, 255));
        for (int x = 0; x < 2500; ++x) wide.setPixelColor(x, 500, QColor(0, 0, 0, 0));
        writePng(desktop.path() + "/wide.png", wide);

        ImportedSceneImage imported;
        QString error;
        QVERIFY2(importSceneImage(project.path(), {}, desktop.path() + "/wide.png", imported, error), qPrintable(error));
        QCOMPARE(imported.width, 4096);
        QCOMPARE(imported.height, 819);
        QVERIFY2(imported.notice.contains("4096"), qPrintable(imported.notice));
        QVERIFY2(imported.notice.contains("wide.png"), qPrintable(imported.notice));
        const QImage saved(project.path() + "/assets/wide.png");
        QCOMPARE(saved.size(), QSize(4096, 819));

        Scene scene;
        scene.items = {imageItem("a", imported.source)};
        QList<SceneImageData> images;
        QVERIFY2(decodeSceneImages(project.path(), scene, images, error), qPrintable(error));
    }

    void importAcceptsImagesExactlyAtTheLimit() {
        QTemporaryDir project, desktop;
        QImage image(4096, 16, QImage::Format_RGB32);
        image.fill(Qt::red);
        writePng(desktop.path() + "/limit.png", image);
        ImportedSceneImage imported;
        QString error;
        QVERIFY2(importSceneImage(project.path(), {}, desktop.path() + "/limit.png", imported, error), qPrintable(error));
        QCOMPARE(imported.width, 4096);
        QVERIFY(imported.notice.isEmpty());
    }

    void importExplainsFilesItCannotUse() {
        QTemporaryDir project, desktop;
        QFile text(desktop.path() + "/notes.png");
        QVERIFY(text.open(QIODevice::WriteOnly));
        text.write("just some words");
        text.close();
        QImage image(2, 2, QImage::Format_RGB32);
        image.fill(Qt::red);
        QVERIFY(image.save(desktop.path() + "/old.bmp", "BMP"));

        const struct {
            QString path;
            const char* expected;
        } cases[] = {
            {desktop.path() + "/notes.png", "notes.png"},
            {desktop.path() + "/old.bmp", "PNG, JPEG or WebP"},
            {desktop.path() + "/missing.png", "missing.png"},
            {desktop.path(), "file"},
        };
        for (const auto& entry : cases) {
            ImportedSceneImage imported;
            QString error;
            QVERIFY2(!importSceneImage(project.path(), {}, entry.path, imported, error), qPrintable(entry.path));
            QVERIFY2(error.contains(QLatin1String(entry.expected)), qPrintable(error));
        }
        QVERIFY(!QDir(project.path() + "/assets").exists() || QDir(project.path() + "/assets").isEmpty());
    }

    void importHandlesWebpOrExplainsWhyNot() {
        QTemporaryDir project, desktop;
        const auto path = desktop.path() + "/photo.webp";
        ImportedSceneImage imported;
        QString error;
        if (QImageReader::supportedImageFormats().contains("webp")) {
            QImage image(6, 3, QImage::Format_RGBA8888);
            image.fill(QColor(9, 80, 200));
            QVERIFY(image.save(path, "WEBP"));
            QVERIFY2(importSceneImage(project.path(), {}, path, imported, error), qPrintable(error));
            QCOMPARE(imported.source, QString("assets/photo.png"));
            QCOMPARE(imported.width, 6);
        } else {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(QByteArray("RIFF\x10\x00\x00\x00WEBPVP8 ", 16));
            file.close();
            QVERIFY(!importSceneImage(project.path(), {}, path, imported, error));
            QVERIFY2(error.contains("WebP"), qPrintable(error));
            QVERIFY2(error.contains("PNG"), qPrintable(error));
        }
    }

    void importRefusesToWriteThroughALinkedAssetsFolder() {
#ifdef Q_OS_WIN
        QSKIP("Creating symbolic links needs extra rights on Windows.");
#endif
        QTemporaryDir project, desktop, elsewhere;
        QVERIFY(QFile::link(elsewhere.path(), project.path() + "/assets"));
        QImage image(4, 4, QImage::Format_RGB32);
        image.fill(Qt::red);
        writePng(desktop.path() + "/logo.png", image);
        ImportedSceneImage imported;
        QString error;
        QVERIFY(!importSceneImage(project.path(), {}, desktop.path() + "/logo.png", imported, error));
        QVERIFY2(error.contains("link"), qPrintable(error));
        QVERIFY(QDir(elsewhere.path()).isEmpty());
    }

    void importRefusesImagesPastTheScenesMemoryLimit() {
        QTemporaryDir project, desktop;
        QImage large(4096, 4096, QImage::Format_RGB32);
        large.fill(Qt::red);
        Scene scene;
        for (int index = 0; index < 4; ++index) {
            const auto source = QString("assets/full-%1.png").arg(index);
            writePng(project.path() + '/' + source, large);
            scene.items.append(imageItem(QString("item-%1").arg(index), source));
        }
        QImage more(2, 2, QImage::Format_RGB32);
        more.fill(Qt::green);
        writePng(desktop.path() + "/more.png", more);
        ImportedSceneImage imported;
        QString error;
        QVERIFY(!importSceneImage(project.path(), scene, desktop.path() + "/more.png", imported, error));
        QVERIFY2(error.contains("MB"), qPrintable(error));
        QVERIFY(!QFile::exists(project.path() + "/assets/more.png"));

        // A file the scene already uses is one texture, so importing it again costs nothing.
        writePng(desktop.path() + "/full-0.png", large);
        QVERIFY2(importSceneImage(project.path(), scene, desktop.path() + "/full-0.png", imported, error), qPrintable(error));
        QCOMPARE(imported.source, QString("assets/full-0.png"));
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
