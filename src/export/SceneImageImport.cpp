#include "SceneImageImport.h"

#include "Project.h"

#include <QBuffer>
#include <QColorSpace>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QSaveFile>

namespace Studio {

namespace {

constexpr auto AssetFolder = "assets/";
constexpr int MaximumNameLength = 48;
// Bigger pictures are refused before decoding: a small file can claim to be enormous.
constexpr qint64 MaximumDecodedBytes = 1024 * 1024 * 1024;

bool isAllowedFormat(const QByteArray& format) {
    const auto name = format.toLower();
    return name == "png" || name == "jpeg" || name == "jpg" || name == "webp";
}

bool startsLikeWebp(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    const auto header = file.read(12);
    return header.startsWith("RIFF") && header.mid(8, 4) == "WEBP";
}

// Lowercase ASCII only, so the name means the same thing on every system and needs no escaping.
QString safeBaseName(const QString& original) {
    QString safe;
    for (const auto character : original.toLower()) {
        const auto code = character.unicode();
        const auto keep = (code >= 'a' && code <= 'z') || (code >= '0' && code <= '9');
        if (keep) safe += character;
        else if (!safe.isEmpty() && !safe.endsWith('-')) safe += '-';
    }
    safe = safe.left(MaximumNameLength);
    while (safe.endsWith('-')) safe.chop(1);
    if (safe.isEmpty()) return "image";
    static const QStringList reserved{"con", "prn", "aux", "nul", "com1", "com2", "com3", "com4", "com5", "com6", "com7", "com8", "com9", "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9"};
    return reserved.contains(safe) ? "image-" + safe : safe;
}

QImage readPicture(const QString& path, const QFileInfo& info, QString& error) {
    const auto name = info.fileName();
    const auto chooseAnother = QString("Choose a PNG, JPEG or WebP file and try again.");

    QImageReader reader(path);
    reader.setDecideFormatFromContent(true);
    reader.setAutoTransform(true);
    const auto format = reader.format();
    if (format.isEmpty()) {
        error = startsLikeWebp(path) ? QString("%1 is a WebP image, but this copy of Studio has no WebP support. Save it as a PNG or JPEG and try again.").arg(name)
                                     : QString("%1 could not be opened as a picture. %2").arg(name, chooseAnother);
        return {};
    }
    if (!isAllowedFormat(format)) {
        error = QString("%1 is a %2 file. %3").arg(name, QString::fromLatin1(format.toUpper()), chooseAnother);
        return {};
    }

    const auto size = reader.size();
    if (size.isValid() && qint64(size.width()) * size.height() * 4 > MaximumDecodedBytes) {
        error = QString("%1 is %2x%3 pixels, too large to open. Save a smaller copy and try again.").arg(name).arg(size.width()).arg(size.height());
        return {};
    }

    // Qt's default limit refuses pictures that are fine to scale down, such as a 100 megapixel photo.
    const auto previousLimit = QImageReader::allocationLimit();
    QImageReader::setAllocationLimit(int(MaximumDecodedBytes / (1024 * 1024)));
    auto image = reader.read();
    QImageReader::setAllocationLimit(previousLimit);
    if (image.isNull()) {
        error = QString("%1 could not be read: %2. Try saving it again as a PNG.").arg(name, reader.errorString());
        return {};
    }
    // Phones save photos in wide-gamut profiles; the game draws plain sRGB.
    if (image.colorSpace().isValid() && image.colorSpace() != QColorSpace::SRgb) image.convertToColorSpace(QColorSpace::SRgb);
    return image;
}

QString scaleDownToLimit(QImage& image, const QString& name) {
    const auto original = image.size();
    if (original.width() <= MaximumImageDimension && original.height() <= MaximumImageDimension) return {};
    const auto target = original.scaled(MaximumImageDimension, MaximumImageDimension, Qt::KeepAspectRatio).expandedTo({1, 1});
    image.convertTo(image.hasAlphaChannel() ? QImage::Format_ARGB32_Premultiplied : QImage::Format_RGB32);
    image = image.scaled(target, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    return QString("%1 is %2x%3 pixels, more than the %4 pixels an image can have on each side, so it was scaled down to %5x%6.")
        .arg(name)
        .arg(original.width())
        .arg(original.height())
        .arg(MaximumImageDimension)
        .arg(target.width())
        .arg(target.height());
}

// Finds the name to use. A file already holding exactly these bytes is reused, because every
// separate file costs texture memory in the game.
bool chooseSource(const QString& root, const QString& baseName, const QByteArray& bytes, QString& source, bool& alreadyThere, QString& error) {
    alreadyThere = false;
    for (int number = 1;; ++number) {
        source = AssetFolder + (number == 1 ? baseName : QString("%1-%2").arg(baseName).arg(number)) + ".png";
        error = checkTargetPath(root, source);
        if (!error.isEmpty()) return false;
        const auto path = root + '/' + source;
        if (!QFileInfo::exists(path)) return true;
        QFile existing(path);
        if (existing.size() == bytes.size() && existing.open(QIODevice::ReadOnly) && existing.readAll() == bytes) {
            alreadyThere = true;
            return true;
        }
    }
}

qint64 imageBytesOtherThan(const QString& root, const Scene& scene, const QString& skipped) {
    qint64 total = 0;
    for (const auto& source : sceneImageSources(scene)) {
        if (source == skipped) continue;
        const auto size = QImageReader(root + '/' + source).size();
        if (size.isValid()) total += qint64(size.width()) * size.height() * 4;
    }
    return total;
}

}

bool importSceneImage(const QString& projectRoot, const Scene& scene, const QString& filePath, ImportedSceneImage& imported, QString& error) {
    error.clear();
    if (filePath.isEmpty()) {
        error = "That is not a file on this computer. Save the picture to a folder first, then add it.";
        return false;
    }
    const QFileInfo info(filePath);
    if (!info.exists()) {
        error = QString("%1 could not be found. Check that it still exists and try again.").arg(info.fileName());
        return false;
    }
    if (!info.isFile()) {
        error = QString("%1 is not a file. Choose a PNG, JPEG or WebP file and try again.").arg(info.fileName());
        return false;
    }

    auto picture = readPicture(filePath, info, error);
    if (picture.isNull()) return false;
    const auto notice = scaleDownToLimit(picture, info.fileName());

    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    if (!picture.save(&buffer, "PNG")) {
        error = QString("%1 could not be converted to PNG. Try saving it again in another program.").arg(info.fileName());
        return false;
    }

    const auto root = QDir::cleanPath(QDir(projectRoot).absolutePath());
    QString source;
    bool alreadyThere = false;
    if (!chooseSource(root, safeBaseName(info.completeBaseName()), bytes, source, alreadyThere, error)) return false;

    const auto sizeProblem = imageSizeProblem(picture.width(), picture.height(), imageBytesOtherThan(root, scene, source));
    if (!sizeProblem.isEmpty()) {
        error = QString("%1 was not added: %2.").arg(info.fileName(), sizeProblem);
        return false;
    }

    if (!alreadyThere) {
        const auto target = root + '/' + source;
        QSaveFile file(target);
        if (!QDir().mkpath(QFileInfo(target).absolutePath()) || !file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
            error = QString("%1 could not be saved in the assets folder: %2. Check that the project folder is writable.").arg(info.fileName(), file.errorString());
            return false;
        }
    }

    imported.source = source;
    imported.name = info.completeBaseName().left(MaximumNameLength);
    imported.notice = notice;
    imported.width = picture.width();
    imported.height = picture.height();
    return true;
}

}
