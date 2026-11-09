#include "Scene.h"

#include "Project.h"

#include <QByteArrayList>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>

#include <cmath>
#include <limits>
#include <utility>

namespace Studio {

namespace {

// The version saved today. Version 1 files have no image items and still load.
constexpr int CurrentSchema = 2;
constexpr int OldestLoadableSchema = 1;
constexpr int MaximumItems = 256;
constexpr int MaximumTextLength = 64;
constexpr int MaximumScale = 40;
// Every position and size becomes a float literal in the generated header, so it has to fit one.
constexpr double MaximumCoordinate = 1e38;

constexpr auto RectangleType = "rectangle";
constexpr auto TextType = "text";
constexpr auto ImageType = "image";
constexpr auto SceneFileName = "scene.json";
constexpr auto ImageFolderPrefix = "assets/";
constexpr auto GeneratedHeaderPath = "src/GeneratedScene.hpp";
constexpr auto GeneratedImageSourcePath = "src/GeneratedImages.cpp";
constexpr auto GeneratedImageFolder = "src/GeneratedImages";
// Limits for decoded images: the T# width and height fields allow more, but one scene has to fit
// in GPU memory next to everything else, and every byte also ends up in the game's executable.
constexpr int MaximumImageDimension = 4096;
constexpr qint64 MaximumTotalImageBytes = 256 * 1024 * 1024;
constexpr auto BackupFolderName = ".studio-backup";

SceneLoadResult failure(const QString& message) {
    return {std::nullopt, message};
}

QString itemError(const SceneItem& item, qsizetype index, const QString& problem) {
    QString label = item.name;
    if (label.isEmpty()) label = item.id;
    if (label.isEmpty()) label = QString("number %1").arg(index + 1);
    return QString("Item %1: %2").arg(label, problem);
}

bool isHexDigit(QChar character) {
    const auto code = character.unicode();
    return (code >= '0' && code <= '9') || (code >= 'a' && code <= 'f') || (code >= 'A' && code <= 'F');
}

bool isColor(const QString& color) {
    if (color.size() != 7 || color[0] != '#') return false;
    for (qsizetype index = 1; index < color.size(); ++index) {
        if (!isHexDigit(color[index])) return false;
    }
    return true;
}

bool isDisplayableText(const QString& text) {
    if (text.isEmpty() || text.size() > MaximumTextLength) return false;
    for (const auto character : text) {
        if (character.unicode() < 0x20 || character.unicode() > 0x7E) return false;
    }
    return true;
}

// Path rules checked without touching the disk; imageFileProblem looks at the actual files.
bool isImageSource(const QString& source) {
    if (!source.startsWith(ImageFolderPrefix) || source.contains('\\') || source.contains(':')) return false;
    if (!source.endsWith(".png", Qt::CaseInsensitive)) return false;
    for (const auto& part : source.split('/')) {
        if (part.isEmpty() || part == "." || part == "..") return false;
    }
    return true;
}

bool isUsableNumber(double value) {
    return std::isfinite(value) && std::abs(value) <= MaximumCoordinate;
}

QString validateItem(const SceneItem& item, qsizetype index, QSet<QString>& usedIds) {
    if (item.id.isEmpty()) return itemError(item, index, "id must be a non-empty string.");
    if (usedIds.contains(item.id)) return itemError(item, index, QString("id \"%1\" is already used. Give each item its own id.").arg(item.id));
    usedIds.insert(item.id);

    const auto isImage = item.type == ImageType;
    if (item.type != RectangleType && item.type != TextType && !isImage) return itemError(item, index, "type must be \"rectangle\", \"text\" or \"image\".");

    for (const auto& [field, value] : {std::pair<const char*, double>{"x", item.x}, {"y", item.y}}) {
        if (!isUsableNumber(value)) return itemError(item, index, QString("%1 must be a number between -1e38 and 1e38.").arg(field));
    }
    if (item.type == RectangleType || isImage) {
        for (const auto& [field, value] : {std::pair<const char*, double>{"width", item.width}, {"height", item.height}}) {
            if (!isUsableNumber(value) || value <= 0) return itemError(item, index, QString("%1 must be a number above 0 and at most 1e38.").arg(field));
        }
    }

    if (isImage) {
        if (!isImageSource(item.source)) return itemError(item, index, "source must be a .png file inside the project's assets folder, for example assets/logo.png.");
        return {};
    }

    if (!isColor(item.color)) return itemError(item, index, "color must look like #RRGGBB, for example #FFD23F.");

    if (item.type == TextType) {
        if (!isDisplayableText(item.text)) return itemError(item, index, "text must be 1 to 64 printable characters.");
        if (item.scale < 1 || item.scale > MaximumScale) return itemError(item, index, "scale must be a whole number from 1 to 40.");
    }
    return {};
}

double numberOrInvalid(const QJsonValue& value) {
    return value.isDouble() ? value.toDouble() : std::numeric_limits<double>::quiet_NaN();
}

// Values of the wrong JSON type become values validateScene rejects, so each rule has one message.
QString parseItem(const QJsonValue& value, qsizetype index, SceneItem& item) {
    if (!value.isObject()) return QString("Item number %1 must be a JSON object.").arg(index + 1);
    const auto object = value.toObject();

    const auto name = object.value("name");
    if (!name.isUndefined() && !name.isString()) return QString("Item number %1: name must be a string.").arg(index + 1);
    item.name = name.toString();
    item.id = object.value("id").toString();
    item.type = object.value("type").toString();
    item.x = numberOrInvalid(object.value("x"));
    item.y = numberOrInvalid(object.value("y"));
    item.color = object.value("color").toString();

    if (item.type == TextType) {
        const auto scale = object.value("scale");
        item.scale = scale.isDouble() ? scale.toInt(0) : 0;
        item.text = object.value("text").toString();
    } else {
        item.width = numberOrInvalid(object.value("width"));
        item.height = numberOrInvalid(object.value("height"));
    }
    if (item.type == ImageType) item.source = object.value("source").toString();

    const auto controllable = object.value("controllable");
    if (!controllable.isUndefined() && !controllable.isBool()) return itemError(item, index, "controllable must be true or false.");
    item.controllable = controllable.toBool();
    return {};
}

QJsonObject itemToJson(const SceneItem& item) {
    QJsonObject object{
        {"id", item.id},
        {"type", item.type},
        {"name", item.name},
        {"x", item.x},
        {"y", item.y},
        {"controllable", item.controllable},
    };
    if (item.type != ImageType) object.insert("color", item.color);
    if (item.type == ImageType) object.insert("source", item.source);
    if (item.type == TextType) {
        object.insert("scale", item.scale);
        object.insert("text", item.text);
    } else {
        object.insert("width", item.width);
        object.insert("height", item.height);
    }
    return object;
}

// Six decimals at most, with trailing zeros dropped down to one, as the format's examples show.
QByteArray floatLiteral(double value) {
    auto text = QByteArray::number(value, 'f', 6);
    while (text.endsWith('0') && text.at(text.size() - 2) != '.') text.chop(1);
    if (text == "-0.0") text = "0.0";
    return text + 'f';
}

QByteArray colorLiterals(const QString& color) {
    QByteArrayList channels;
    for (const qsizetype start : {1, 3, 5}) channels << floatLiteral(color.mid(start, 2).toInt(nullptr, 16) / 255.0);
    return channels.join(", ");
}

QByteArray escapedText(const QString& text) {
    QByteArray escaped;
    for (const char character : text.toLatin1()) {
        if (character == '"' || character == '\\') escaped += '\\';
        escaped += character;
    }
    return escaped;
}

QByteArray itemLine(const SceneItem& item, const QStringList& imageSources) {
    const auto isText = item.type == TextType;
    const auto isImage = item.type == ImageType;
    const auto width = isText ? double((item.text.size() * 6 - 1) * item.scale) : item.width;
    const auto height = isText ? double(7 * item.scale) : item.height;
    QByteArray line = "    {Kind::";
    line += isText ? "Text" : isImage ? "Image" : "Rectangle";
    for (const auto value : {item.x, item.y, width, height}) line += ", " + floatLiteral(value);
    line += ", " + (isImage ? QByteArray("0.0f, 0.0f, 0.0f") : colorLiterals(item.color));
    line += ", " + floatLiteral(isText ? item.scale : 0);
    line += ", \"" + (isText ? escapedText(item.text) : QByteArray()) + "\", ";
    line += item.controllable ? "true" : "false";
    if (isImage) line += ", " + QByteArray::number(imageSources.indexOf(item.source));
    line += "},\n";
    return line;
}

Scene starterScene() {
    SceneItem player;
    player.id = "item-1";
    player.type = RectangleType;
    player.name = "Player";
    player.x = 120;
    player.y = 300;
    player.width = 160;
    player.height = 160;
    player.controllable = true;

    SceneItem title;
    title.id = "item-2";
    title.type = TextType;
    title.name = "Title";
    title.x = 400;
    title.y = 96;
    title.scale = 12;
    title.text = "Hello PS5";
    title.color = "#F3F1E2";

    Scene scene;
    scene.items = {player, title};
    return scene;
}

struct RuntimeFile {
    QString relativePath;
    QByteArray contents;
};

// A lexical check is not enough: a link inside the project could lead anywhere, so every part of
// the path is inspected. isSymLink also catches links whose target is missing, which exists() does not.
QString checkTargetPath(const QString& root, const QString& relativePath) {
    if (!isInsideDirectory(root + '/' + relativePath, root)) return QString("%1 is outside the project folder, so it was not installed.").arg(relativePath);
    const auto parts = relativePath.split('/');
    auto current = root;
    for (qsizetype index = 0; index < parts.size(); ++index) {
        current += '/' + parts[index];
        const QFileInfo info(current);
        const auto shown = parts.mid(0, index + 1).join('/');
        if (info.isSymLink() || info.isJunction()) return QString("%1 is a link, and Studio will not write through it. Replace it with a regular file or folder and try again.").arg(shown);
        if (!info.exists()) return {};
        const auto isLast = index == parts.size() - 1;
        if (isLast && info.isDir()) return QString("%1 is a folder, but Studio needs to write a file there. Move the folder and try again.").arg(shown);
        if (!isLast && !info.isDir()) return QString("%1 is a file, but Studio needs a folder there. Move the file and try again.").arg(shown);
    }
    return {};
}

bool writeFile(const QString& path, const QByteArray& contents, QString& problem) {
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        problem = "its folder could not be created";
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(contents) != contents.size() || !file.commit()) {
        problem = file.errorString();
        return false;
    }
    return true;
}

// True when `images` holds exactly the scene's sources, in order, each with a full set of pixels.
bool imagesMatchScene(const Scene& scene, const QList<SceneImageData>& images) {
    const auto sources = sceneImageSources(scene);
    if (images.size() != sources.size()) return false;
    for (qsizetype index = 0; index < images.size(); ++index) {
        const auto& image = images[index];
        if (image.source != sources[index] || image.width < 1 || image.height < 1) return false;
        if (image.pixels.size() != qint64(image.width) * image.height * 4) return false;
    }
    return true;
}

// Leaves an identical file alone so its timestamp does not trigger a needless rebuild.
bool writeIfChanged(const QString& path, const QByteArray& contents, QString& problem) {
    QFile existing(path);
    if (existing.size() == contents.size() && existing.open(QIODevice::ReadOnly) && existing.readAll() == contents) return true;
    return writeFile(path, contents, problem);
}

QString newBackupFolderName(const QString& root) {
    auto moment = QDateTime::currentDateTime();
    auto name = moment.toString("yyyyMMdd-HHmmss");
    // Two installs in the same second must not share a folder, or the first backup would be lost.
    while (QFileInfo::exists(root + '/' + BackupFolderName + '/' + name)) {
        moment = moment.addSecs(1);
        name = moment.toString("yyyyMMdd-HHmmss");
    }
    return name;
}

}

QString validateScene(const Scene& scene) {
    if (!isColor(scene.background)) return "Background color must look like #RRGGBB, for example #151C3A.";
    if (scene.items.size() > MaximumItems) return QString("The scene has %1 items, but a scene can hold at most %2. Remove some items and try again.").arg(scene.items.size()).arg(MaximumItems);
    QSet<QString> usedIds;
    for (qsizetype index = 0; index < scene.items.size(); ++index) {
        const auto problem = validateItem(scene.items[index], index, usedIds);
        if (!problem.isEmpty()) return problem;
    }
    return {};
}

SceneLoadResult loadScene(const QString& path) {
    const auto fileName = QFileInfo(path).fileName();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return failure(QString("%1 could not be opened: %2. Check that the file exists and try again.").arg(fileName, file.errorString()));

    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) return failure(QString("%1 is not valid JSON: %2 at character %3. Fix the file or restore a backup.").arg(fileName, parseError.errorString(), QString::number(parseError.offset)));
    if (!document.isObject()) return failure(QString("%1 must contain a JSON object with the scene in it.").arg(fileName));
    const auto root = document.object();

    const auto schema = root.value("schemaVersion");
    const auto version = schema.toInt(-1);
    if (!schema.isDouble() || version < OldestLoadableSchema || version > CurrentSchema) return failure(QString("%1 needs schemaVersion %2 or %3, the versions this Studio supports.").arg(fileName).arg(OldestLoadableSchema).arg(CurrentSchema));
    if (!root.value("items").isArray()) return failure(QString("%1 has no items list. Add \"items\": [] to the file.").arg(fileName));

    Scene scene;
    scene.background = root.value("background").toString();
    const auto items = root.value("items").toArray();
    for (qsizetype index = 0; index < items.size(); ++index) {
        SceneItem item;
        const auto problem = parseItem(items.at(index), index, item);
        if (!problem.isEmpty()) return failure(problem);
        if (version < CurrentSchema && item.type == ImageType) return failure(QString("%1 uses an image item, which needs schemaVersion %2. Change schemaVersion to %2 and try again.").arg(fileName).arg(CurrentSchema));
        scene.items.append(item);
    }

    const auto problem = validateScene(scene);
    if (!problem.isEmpty()) return failure(problem);
    return {scene, {}};
}

bool saveScene(const QString& path, const Scene& scene, QString& error) {
    error = validateScene(scene);
    if (!error.isEmpty()) return false;

    QJsonArray items;
    for (const auto& item : scene.items) items.append(itemToJson(item));
    const QJsonObject root{{"schemaVersion", CurrentSchema}, {"background", scene.background}, {"items", items}};
    const auto contents = QJsonDocument(root).toJson();

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(contents) != contents.size() || !file.commit()) {
        error = QString("%1 could not be saved: %2. Check that the folder exists and is writable.").arg(QFileInfo(path).fileName(), file.errorString());
        return false;
    }
    return true;
}

QStringList sceneImageSources(const Scene& scene) {
    QStringList sources;
    for (const auto& item : scene.items) {
        if (item.type == ImageType && !sources.contains(item.source)) sources << item.source;
    }
    return sources;
}

QString imageSizeProblem(int width, int height, qint64 bytesSoFar) {
    if (width < 1 || height < 1) return "it is empty. Choose an image that has pixels";
    if (width > MaximumImageDimension || height > MaximumImageDimension) return QString("it is %1x%2 pixels, and an image can be at most %3 pixels on each side. Scale it down and try again").arg(width).arg(height).arg(MaximumImageDimension);
    if (bytesSoFar + qint64(width) * height * 4 > MaximumTotalImageBytes) return QString("the scene's images would use more than %1 MB of GPU memory. Remove or shrink some images and try again").arg(MaximumTotalImageBytes / (1024 * 1024));
    return {};
}

QString imageFileProblem(const QString& projectRoot, const QString& source) {
    const auto root = QDir::cleanPath(QDir(projectRoot).absolutePath());
    if (!isImageSource(source) || !isInsideDirectory(root + '/' + source, root)) return QString("%1 is not a .png file inside the project's assets folder.").arg(source);
    const auto parts = source.split('/');
    auto current = root;
    for (qsizetype index = 0; index < parts.size(); ++index) {
        current += '/' + parts[index];
        const QFileInfo info(current);
        const auto shown = parts.mid(0, index + 1).join('/');
        if (info.isSymLink() || info.isJunction()) return QString("%1 is a link, so the image %2 was not read. Replace the link with a regular file or folder and try again.").arg(shown, source);
        if (!info.exists()) return QString("%1 could not be found. Add the file to the project or remove the image from the scene.").arg(source);
        const auto isLast = index == parts.size() - 1;
        if (isLast && !info.isFile()) return QString("%1 is a folder, not an image file. Choose a .png file.").arg(source);
        if (!isLast && !info.isDir()) return QString("%1 is a file, but the image %2 needs a folder there.").arg(shown, source);
    }
    return {};
}

QByteArray generateImageSource(const QList<SceneImageData>& images) {
    QByteArray source = "// Generated by AnyDevKit Studio from scene.json. Do not edit.\n#include \"GeneratedScene.hpp\"\n\n";
    if (!images.isEmpty()) {
        // Pixels are raw files pulled in by the assembler, so the compiler never parses them.
        source += "// SCENE_IMAGE_DIRECTORY comes from CMakeLists.txt and points at src/GeneratedImages.\n";
    }
    for (qsizetype index = 0; index < images.size(); ++index) {
        const auto number = QByteArray::number(index);
        const auto symbol = "StudioSceneImage" + number;
        source += "__asm__(\".pushsection .rodata\\n.balign 16\\n.hidden " + symbol + "\\n" + symbol + ":\\n.incbin \\\"\" SCENE_IMAGE_DIRECTORY \"/image-" + number + ".rgba\\\"\\n.popsection\\n\");\n";
        source += "extern \"C\" const std::uint8_t " + symbol + "[];\n";
    }
    source += "\nnamespace GeneratedScene {\n\n";
    if (images.isEmpty()) {
        source += "const std::array<Image, 0> Images{};\n";
    } else {
        source += "const std::array<Image, " + QByteArray::number(images.size()) + "> Images{{\n";
        for (qsizetype index = 0; index < images.size(); ++index) {
            source += "    {StudioSceneImage" + QByteArray::number(index) + ", " + QByteArray::number(images[index].width) + ", " + QByteArray::number(images[index].height) + "},\n";
        }
        source += "}};\n";
    }
    source += "\n}\n";
    return source;
}

QByteArray generateSceneHeader(const Scene& scene, const QList<SceneImageData>& images) {
    if (!validateScene(scene).isEmpty() || !imagesMatchScene(scene, images)) return {};
    const auto imageSources = sceneImageSources(scene);

    QByteArray header = R"cpp(// Generated by AnyDevKit Studio from scene.json. Do not edit.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace GeneratedScene {

enum class Kind : std::uint8_t { Rectangle, Text, Image };

struct Item {
    Kind kind;
    float x, y, width, height;
    float red, green, blue;
    float textScale;
    std::string_view text;
    bool controllable;
    std::int32_t image = -1; // index into Images for an image item
};

// Row by row, top row first, 4 bytes per pixel: red, green, blue, alpha, with alpha already applied.
struct Image {
    const std::uint8_t* pixels;
    std::uint32_t width, height;
};

)cpp";
    header += "inline constexpr std::array<float, 3> Background{" + colorLiterals(scene.background) + "};\n";
    header += "inline constexpr std::size_t ImageCount = " + QByteArray::number(images.size()) + ";\n";
    header += "extern const std::array<Image, ImageCount> Images; // defined in src/GeneratedImages.cpp\n";
    if (scene.items.isEmpty()) {
        header += "inline constexpr std::array<Item, 0> Items{};\n";
    } else {
        header += "inline constexpr std::array<Item, " + QByteArray::number(scene.items.size()) + "> Items{{\n";
        for (const auto& item : scene.items) header += itemLine(item, imageSources);
        header += "}};\n";
    }
    header += "\n}\n";
    return header;
}

bool writeGeneratedScene(const QString& projectRoot, const Scene& scene, const QList<SceneImageData>& images, QString& error) {
    error.clear();
    const auto header = generateSceneHeader(scene, images);
    if (header.isEmpty()) {
        error = "The scene could not be exported because it has a problem or its images do not match. Check the scene and try again.";
        return false;
    }
    const auto root = QDir::cleanPath(QDir(projectRoot).absolutePath());

    QList<RuntimeFile> files{{GeneratedHeaderPath, header}, {GeneratedImageSourcePath, generateImageSource(images)}};
    QSet<QString> keptImages;
    for (qsizetype index = 0; index < images.size(); ++index) {
        const auto name = QString("image-%1.rgba").arg(index);
        files.append({QString(GeneratedImageFolder) + '/' + name, images[index].pixels});
        keptImages.insert(name);
    }

    for (const auto& file : files) {
        const auto problem = checkTargetPath(root, file.relativePath);
        if (!problem.isEmpty()) {
            error = problem;
            return false;
        }
    }
    for (const auto& file : files) {
        QString problem;
        if (!writeIfChanged(root + '/' + file.relativePath, file.contents, problem)) {
            error = QString("%1 could not be written: %2.").arg(file.relativePath, problem);
            return false;
        }
    }

    // Pixels of images the scene no longer uses would keep bloating the build; only this folder's own files go.
    const QFileInfo folderInfo(root + '/' + GeneratedImageFolder);
    if (!folderInfo.isDir() || folderInfo.isSymLink() || folderInfo.isJunction()) return true;
    const QDir imageFolder(folderInfo.filePath());
    for (const auto& name : imageFolder.entryList({"image-*.rgba"}, QDir::Files)) {
        if (!keptImages.contains(name)) QFile::remove(imageFolder.filePath(name));
    }
    return true;
}

bool installSceneRuntime(const QString& runtimeDirectory, const QString& projectRoot, const QString& projectName, QString& error) {
    error.clear();
    const auto fail = [&](const QString& message) {
        error = message;
        return false;
    };

    if (projectName.isEmpty()) return fail("The project has no name. Give it a name and try again.");
    if (runtimeDirectory.isEmpty()) return fail("No runtime folder was given, so nothing was installed.");
    if (projectRoot.isEmpty()) return fail("No project folder was given, so nothing was installed.");
    const auto source = QDir::cleanPath(runtimeDirectory);
    const auto root = QDir::cleanPath(QDir(projectRoot).absolutePath());
    if (!QDir(source).exists()) return fail(QString("The runtime folder %1 does not exist. Reinstall Studio and try again.").arg(source));
    if (!QDir(root).exists()) return fail(QString("The project folder %1 does not exist. Choose an existing project.").arg(root));

    const auto scenePath = root + '/' + SceneFileName;
    const auto sceneExists = QFileInfo::exists(scenePath);

    // Everything is read and checked first, so a refused install leaves the project untouched.
    QList<RuntimeFile> files;
    QSet<QString> targets;
    const auto prefixLength = source.endsWith('/') ? source.size() : source.size() + 1;
    QDirIterator iterator(source, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const auto path = iterator.next();
        auto relativePath = path.mid(prefixLength);
        if (relativePath.endsWith(".in")) relativePath.chop(3);
        if (relativePath.isEmpty() || relativePath.endsWith('/')) return fail(QString("The runtime file %1 has no name once .in is removed.").arg(path));
        if (targets.contains(relativePath)) return fail(QString("Two runtime files would both install to %1. Rename one of them.").arg(relativePath));
        targets.insert(relativePath);
        if (relativePath == SceneFileName && sceneExists) continue; // the scene is the user's work

        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) return fail(QString("The runtime file %1 could not be read: %2.").arg(path, file.errorString()));
        // Bytes rather than text, so binary files come through intact.
        files.append({relativePath, file.readAll().replace("@NAME@", projectName.toUtf8())});
    }

    QList<QString> overwritten;
    for (const auto& file : files) {
        const auto problem = checkTargetPath(root, file.relativePath);
        if (!problem.isEmpty()) return fail(problem);
        if (QFileInfo::exists(root + '/' + file.relativePath)) overwritten << file.relativePath;
    }
    const auto needsStarterScene = !sceneExists && !targets.contains(SceneFileName);
    if (needsStarterScene) {
        const auto problem = checkTargetPath(root, SceneFileName);
        if (!problem.isEmpty()) return fail(problem);
    }

    QString backupRelative;
    if (!overwritten.isEmpty()) {
        backupRelative = QString(BackupFolderName) + '/' + newBackupFolderName(root);
        for (const auto& relativePath : overwritten) {
            const auto problem = checkTargetPath(root, backupRelative + '/' + relativePath);
            if (!problem.isEmpty()) return fail(problem);
        }
        for (const auto& relativePath : overwritten) {
            const auto backupPath = root + '/' + backupRelative + '/' + relativePath;
            if (!QDir().mkpath(QFileInfo(backupPath).absolutePath()) || !QFile::copy(root + '/' + relativePath, backupPath)) {
                return fail(QString("%1 could not be backed up to %2, so the install stopped before changing any project file.").arg(relativePath, backupPath));
            }
        }
    }

    const auto recovery = backupRelative.isEmpty() ? QString() : QString(" Files replaced so far are backed up in %1.").arg(backupRelative);
    for (const auto& file : files) {
        QString problem;
        if (!writeFile(root + '/' + file.relativePath, file.contents, problem)) return fail(QString("%1 could not be written: %2.%3").arg(file.relativePath, problem, recovery));
    }

    if (needsStarterScene) {
        QString problem;
        if (!saveScene(scenePath, starterScene(), problem)) return fail(problem + recovery);
    }
    return true;
}

}
