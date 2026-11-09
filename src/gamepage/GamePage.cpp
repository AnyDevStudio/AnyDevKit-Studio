#include "GamePage.h"

#include "Project.h"

#include <QColorSpace>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QSaveFile>

#include <cstring>
#include <optional>

namespace Studio {

namespace {

qsizetype skipSpace(const QByteArray& text, qsizetype index) {
    while (index < text.size() && std::strchr(" \t\r\n", text[index])) ++index;
    return index;
}

// Returns the index just past the closing quote of the string starting at index, or -1.
qsizetype skipString(const QByteArray& text, qsizetype index) {
    for (++index; index < text.size(); ++index) {
        if (text[index] == '\\') ++index;
        else if (text[index] == '"') return index + 1;
    }
    return -1;
}

// Returns the index just past the value starting at index, or -1.
qsizetype skipValue(const QByteArray& text, qsizetype index) {
    if (index >= text.size()) return -1;
    if (text[index] == '"') return skipString(text, index);
    if (text[index] != '{' && text[index] != '[') {
        while (index < text.size() && !std::strchr(",}] \t\r\n", text[index])) ++index;
        return index;
    }
    int depth = 0;
    for (; index < text.size(); ++index) {
        const auto character = text[index];
        if (character == '"') {
            index = skipString(text, index) - 1;
            if (index < 0) return -1;
        } else if (character == '{' || character == '[') {
            ++depth;
        } else if ((character == '}' || character == ']') && --depth == 0) {
            return index + 1;
        }
    }
    return -1;
}

// Returns where the value of key begins in the object starting at objectStart, or -1.
// When a key repeats, the last one wins, which is how QJsonDocument reads it.
qsizetype findMember(const QByteArray& text, qsizetype objectStart, const QByteArray& key) {
    if (objectStart < 0 || objectStart >= text.size() || text[objectStart] != '{') return -1;
    const auto quoted = '"' + key + '"';
    qsizetype found = -1;
    auto index = skipSpace(text, objectStart + 1);
    while (index < text.size() && text[index] != '}') {
        const auto keyEnd = skipString(text, index);
        if (keyEnd < 0) return -1;
        const auto matches = text.mid(index, keyEnd - index) == quoted;
        index = skipSpace(text, skipSpace(text, keyEnd) + 1);
        if (matches) found = index;
        index = skipValue(text, index);
        if (index < 0) return -1;
        index = skipSpace(text, index);
        if (index < text.size() && text[index] == ',') index = skipSpace(text, index + 1);
    }
    return found;
}

QByteArray jsonString(const QString& value) {
    const auto array = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
    return array.mid(1, array.size() - 2);
}

// Replaces only the title's characters, so the rest of the file keeps its exact bytes.
std::optional<QByteArray> spliceDefaultTitle(const QByteArray& json, const QString& language, const QString& title) {
    const auto root = skipSpace(json, 0);
    const auto localized = findMember(json, root, "localizedParameters");
    const auto entry = findMember(json, localized, language.toUtf8());
    const auto value = findMember(json, entry, "titleName");
    if (value < 0 || json[value] != '"') return std::nullopt;
    const auto end = skipString(json, value);
    if (end < 0) return std::nullopt;
    auto result = json;
    result.replace(value, end - value, jsonString(title));
    return result;
}

// Mirrors the checks AnyDevKit makes when it packages the title.
QString titleProblem(const QString& title) {
    if (title.isEmpty()) return "Type a name for the game. The name on the console was not changed.";
    for (const auto character : title) {
        if (character.unicode() < 0x20 || character.unicode() == 0x7f) return "The name contains a control character the console cannot show. Retype the name and try again.";
    }
    if (!title.isValidUtf16()) return "The name contains text that cannot be stored. Retype the name and try again.";
    return {};
}

QString relativeTo(const QString& root, const QString& path) {
    return QDir(root).relativeFilePath(path);
}

QString writeProblem(const QString& root, const QString& path) {
    return checkTargetPath(root, relativeTo(root, path));
}

QImage readPicture(const QString& path, QString& error) {
    const auto name = QFileInfo(path).fileName();
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const auto size = reader.size();
    auto image = reader.read();
    if (image.isNull()) {
        const auto tooLarge = size.isValid() && qint64(size.width()) * size.height() * 4 > qint64(QImageReader::allocationLimit()) * 1024 * 1024;
        error = tooLarge ? QString("%1 is too large to open. Save a smaller copy of the picture and try again.").arg(name) : QString("%1 could not be opened as a picture. Choose a PNG or JPEG file and try again.").arg(name);
        return {};
    }
    if (image.colorSpace().isValid() && image.colorSpace() != QColorSpace::SRgb) image.convertToColorSpace(QColorSpace::SRgb);
    return image;
}

}

GameImageTarget gameImageTarget(GameImageKind kind) {
    switch (kind) {
    case GameImageKind::Icon: return {kind, "icon", "Icon", "icon0.png", {512, 512}};
    case GameImageKind::Background: return {kind, "background", "Background", "pic0.png", {3840, 2160}};
    case GameImageKind::Splash: return {kind, "splash", "Splash", "pic1.png", {3840, 2160}};
    }
    Q_UNREACHABLE();
}

QList<GameImageTarget> gameImageTargets() {
    return {gameImageTarget(GameImageKind::Icon), gameImageTarget(GameImageKind::Background), gameImageTarget(GameImageKind::Splash)};
}

QString installDefaultGamePictures(const QString& sourceFolder, const QString& targetFolder) {
    if (!QDir().mkpath(targetFolder)) return QString("Could not create %1.").arg(targetFolder);
    for (const auto* name : {"icon0.png", "pic0.png", "pic1.png"}) {
        const auto source = sourceFolder + '/' + name;
        const auto target = targetFolder + '/' + name;
        if (!QFile::exists(source) || QFile::exists(target)) continue;
        if (!QFile::copy(source, target)) return QString("Could not add %1 to the project.").arg(name);
        // Files copied out of Qt resources arrive read-only.
        QFile::setPermissions(target, QFile::permissions(target) | QFile::WriteOwner);
    }
    return {};
}

QRect centerCropRect(const QSize& source, const QSize& target) {
    if (source.isEmpty() || target.isEmpty()) return {};
    const qint64 sourceWidth = source.width();
    const qint64 sourceHeight = source.height();
    const qint64 targetWidth = target.width();
    const qint64 targetHeight = target.height();
    auto width = sourceWidth;
    auto height = (sourceWidth * targetHeight + targetWidth / 2) / targetWidth;
    if (height > sourceHeight) {
        height = sourceHeight;
        width = (sourceHeight * targetWidth + targetHeight / 2) / targetHeight;
    }
    width = qBound<qint64>(1, width, sourceWidth);
    height = qBound<qint64>(1, height, sourceHeight);
    return QRect(int((sourceWidth - width) / 2), int((sourceHeight - height) / 2), int(width), int(height));
}

QImage fitGameImage(const QImage& source, const QSize& target) {
    const auto crop = source.copy(centerCropRect(source.size(), target));
    const auto scaled = crop.convertToFormat(QImage::Format_ARGB32_Premultiplied).scaled(target, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    QImage flat(target, QImage::Format_RGB32);
    flat.fill(Qt::black);
    QPainter painter(&flat);
    painter.drawImage(0, 0, scaled);
    painter.end();
    return flat;
}

TitleEdit withDefaultTitle(const QByteArray& json, const QString& title) {
    const auto clean = title.trimmed();
    if (const auto problem = titleProblem(clean); !problem.isEmpty()) return {{}, problem};

    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) return {{}, "param.json is not valid JSON, so the name was not saved. Fix the file or create the project again with adk init."};

    auto root = document.object();
    auto localized = root.value("localizedParameters").toObject();
    const auto language = localized.value("defaultLanguage").toString();
    if (language.isEmpty()) return {{}, "param.json has no default language, so Studio cannot tell which name to change. Create the project again with adk init."};

    auto entry = localized.value(language).toObject();
    entry["titleName"] = clean;
    localized[language] = entry;
    root["localizedParameters"] = localized;

    // Fall back to rewriting the document if the file holds something unusual the splice cannot find.
    if (const auto spliced = spliceDefaultTitle(json, language, clean); spliced && QJsonDocument::fromJson(*spliced).object() == root) return {*spliced, {}};
    return {QJsonDocument(root).toJson(), {}};
}

QString installGameImage(const QString& sourcePath, const QString& projectRoot, const QString& systemFolder, const GameImageTarget& target) {
    const auto destination = QDir::cleanPath(systemFolder + '/' + target.fileName);
    const auto shown = relativeTo(projectRoot, destination);
    if (const auto problem = writeProblem(projectRoot, destination); !problem.isEmpty()) return problem;

    QString error;
    const auto image = readPicture(sourcePath, error);
    if (!error.isEmpty()) return error;

    const auto fitted = fitGameImage(image, target.size);
    if (fitted.isNull()) return QString("The picture could not be resized to %1 x %2. Choose a smaller picture and try again.").arg(target.size.width()).arg(target.size.height());
    if (!QDir().mkpath(QFileInfo(destination).absolutePath())) return QString("The folder for %1 could not be created. Check that the project folder can be written to.").arg(shown);

    QSaveFile file(destination);
    if (!file.open(QIODevice::WriteOnly) || !fitted.save(&file, "PNG") || !file.commit()) return QString("%1 could not be saved: %2. Check that the project folder can be written to.").arg(shown, file.errorString());
    return {};
}

QString removeGameImage(const QString& projectRoot, const QString& systemFolder, const GameImageTarget& target) {
    const auto destination = QDir::cleanPath(systemFolder + '/' + target.fileName);
    if (const auto problem = writeProblem(projectRoot, destination); !problem.isEmpty()) return problem;
    if (!QFile::exists(destination) || QFile::remove(destination)) return {};
    return QString("%1 could not be removed. Check that the project folder can be written to.").arg(relativeTo(projectRoot, destination));
}

}
