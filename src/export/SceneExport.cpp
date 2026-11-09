#include "SceneExport.h"

#include <QImage>
#include <QImageReader>
#include <QSet>

namespace Studio {

bool decodeSceneImages(const QString& projectRoot, const Scene& scene, QList<SceneImageData>& images, QString& error) {
    images.clear();
    error.clear();
    qint64 totalBytes = 0;

    for (const auto& source : sceneImageSources(scene)) {
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
        const auto sizeProblem = imageSizeProblem(size.width(), size.height(), totalBytes);
        if (!sizeProblem.isEmpty()) return fail(sizeProblem);

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
    return true;
}

bool exportScene(const QString& projectRoot, const Scene& scene, QString& error) {
    QList<SceneImageData> images;
    if (!decodeSceneImages(projectRoot, scene, images, error)) return false;
    return writeGeneratedScene(projectRoot, scene, images, error);
}

}
