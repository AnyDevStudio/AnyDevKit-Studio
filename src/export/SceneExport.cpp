#include "SceneExport.h"

#include <QImage>
#include <QImageReader>
#include <QSet>

#include <algorithm>

namespace Studio {

namespace {

// All the images of every scene are uploaded when the game starts, so together they also have a limit.
constexpr qint64 MaximumProjectImageBytes = qint64(1024) * 1024 * 1024;

// All images of the given scenes, once each. Limits apply per scene (at most 256 MB of a scene's different
// images), and a project's images together must also fit, since they are all uploaded when the game starts.
bool decodeImages(const QString& projectRoot, const QStringList& sources, const QList<const Scene*>& scenes, QList<SceneImageData>& images, QString& error) {
    images.clear();
    error.clear();
    qint64 totalBytes = 0;

    for (const auto& source : sources) {
        const auto problem = imageFileProblem(projectRoot, source);
        if (!problem.isEmpty()) {
            error = problem;
            return false;
        }

        const auto fail = [&](const QString& why) {
            error = QString("The image %1 could not be used: %2.").arg(source, why);
            return false;
        };

        QImageReader reader(projectRoot + '/' + source);
        reader.setAutoDetectImageFormat(false);
        reader.setFormat("png");
        const auto size = reader.size();
        if (!size.isValid() || !reader.canRead()) return fail("it is not a valid PNG file");
        const auto sizeProblem = imageSizeProblem(size.width(), size.height(), 0);
        if (!sizeProblem.isEmpty()) return fail(sizeProblem);
        if (totalBytes + qint64(size.width()) * size.height() * 4 > MaximumProjectImageBytes) return fail(QString("the project's images together would use more than %1 MB of GPU memory. Remove or shrink some images and try again").arg(MaximumProjectImageBytes / (1024 * 1024)));

        const auto decoded = reader.read();
        if (decoded.isNull()) return fail(reader.errorString());
        const auto converted = decoded.convertToFormat(QImage::Format_RGBA8888_Premultiplied);

        SceneImageData image;
        image.source = source;
        image.width = converted.width();
        image.height = converted.height();
        const auto rowBytes = qsizetype(image.width) * 4;
        image.pixels.reserve(rowBytes * image.height);
        for (int row = 0; row < image.height; ++row) {
            image.pixels.append(reinterpret_cast<const char*>(converted.constScanLine(row)), rowBytes);
        }
        totalBytes += image.pixels.size();
        images.append(image);
    }
    for (const auto* scene : scenes) {
        qint64 sceneBytes = 0;
        for (const auto& source : sceneImageSources(*scene)) {
            const auto found = std::ranges::find(images, source, &SceneImageData::source);
            if (found != images.end()) sceneBytes += found->pixels.size();
        }
        if (const auto problem = imageSizeProblem(1, 1, sceneBytes - 4); !problem.isEmpty()) {
            error = QString("The scene \"%1\" uses too much image memory: %2.").arg(scene->name, problem);
            return false;
        }
    }
    return true;
}

}

bool decodeSceneImages(const QString& projectRoot, const Scene& scene, QList<SceneImageData>& images, QString& error) {
    return decodeImages(projectRoot, sceneImageSources(scene), {&scene}, images, error);
}

bool decodeSceneImages(const QString& projectRoot, const SceneSet& set, QList<SceneImageData>& images, QString& error) {
    QList<const Scene*> scenes;
    for (const auto& scene : set.scenes) scenes.append(&scene);
    return decodeImages(projectRoot, sceneImageSources(set), scenes, images, error);
}

bool exportScene(const QString& projectRoot, const Scene& scene, QString& error) {
    QList<SceneImageData> images;
    if (!decodeSceneImages(projectRoot, scene, images, error)) return false;
    return writeGeneratedScene(projectRoot, scene, images, error);
}

bool exportScene(const QString& projectRoot, const SceneSet& set, QString& error) {
    QList<SceneImageData> images;
    if (!decodeSceneImages(projectRoot, set, images, error)) return false;
    return writeGeneratedScene(projectRoot, set, images, error);
}

}
