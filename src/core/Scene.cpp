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

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>

namespace Studio {

namespace {

// The version saved today. Older files still load: version 1 cannot hold images, neither 1 nor 2 can
// hold particles, before 4 a brace in a text was only a brace, before 5 there are no sprite sheets,
// tilemaps, world or camera, and before 6 a project is one scene and items have no physics.
constexpr int CurrentSchema = 6;
constexpr int OldestLoadableSchema = 1;
constexpr int ImageSchema = 2;
constexpr int ParticleSchema = 3;
constexpr int VariableTextSchema = 4;
constexpr int WorldSchema = 5;
constexpr int SceneSetSchema = 6;
constexpr int MaximumItems = 256;
constexpr int MaximumTextLength = 64;
constexpr int MaximumScale = 40;
// Every position and size becomes a float literal in the generated header, so it has to fit one.
constexpr double MaximumCoordinate = 1e38;

constexpr auto RectangleType = "rectangle";
constexpr auto TextType = "text";
constexpr auto ImageType = "image";
constexpr auto ParticlesType = "particles";
constexpr auto TilemapType = "tilemap";
constexpr auto SolidPhysics = "solid";
constexpr auto BodyPhysics = "body";
constexpr int MaximumParticleAmount = 8000;
constexpr int MinimumParticleSpeed = 10;
constexpr int MaximumParticleSpeed = 300;
constexpr int MaximumParticleGravity = 300;
constexpr int MinimumPixelSize = 2;
constexpr int MaximumPixelSize = 32;
// A value outside every range: a number of the wrong type in scene.json becomes this.
constexpr int InvalidNumber = std::numeric_limits<int>::min();
constexpr auto SceneFileName = "scene.json";
constexpr auto ImageFolderPrefix = "assets/";
constexpr auto GeneratedHeaderPath = "src/GeneratedScene.hpp";
constexpr auto GeneratedImageSourcePath = "src/GeneratedImages.cpp";
constexpr auto GeneratedImageFolder = "src/GeneratedImages";
constexpr auto TileFilePattern = "tilemap-*.tiles";
// Limits for decoded images (MaximumImageDimension is in Scene.h): the T# width and height fields
// allow more, but one scene has to fit in GPU memory next to everything else, and every byte also
// ends up in the game's executable.
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
    if (text.isEmpty()) return false;
    for (const auto character : text) {
        if (character.unicode() < 0x20 || character.unicode() > 0x7E) return false;
    }
    return true;
}

// {{ and }} count as the one character they show, and {name} as what is typed for it.
qsizetype textLength(const TextTemplate& parsed) {
    qsizetype length = 0;
    for (const auto& part : parsed.parts) length += part.variable.isEmpty() ? part.text.size() : part.variable.size() + 2;
    return length;
}

// The text a text item holds in the generated header: the plain text, with each variable widened to
// the number that takes the most characters so that its length is the quads the game must reserve.
QString widestText(const QString& text) {
    QString widest;
    for (const auto& part : parseTextTemplate(text).parts) widest += part.variable.isEmpty() ? part.text : QString(MaximumVariableCharacters, '0');
    return widest;
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

QString validateParticles(const SceneItem& item, qsizetype index) {
    if (!particlePresetNames().contains(item.preset)) return itemError(item, index, "preset must be one of: " + particlePresetNames().join(", ") + ".");
    if (!particlePaletteNames().contains(item.palette)) return itemError(item, index, "palette must be one of: " + particlePaletteNames().join(", ") + ".");
    const auto outside = [](int value, int lowest, int highest) { return value < lowest || value > highest; };
    if (outside(item.amount, 1, MaximumParticleAmount)) return itemError(item, index, QString("amount must be a whole number from 1 to %1.").arg(MaximumParticleAmount));
    if (outside(item.spread, 0, 100)) return itemError(item, index, "spread must be a whole number from 0 to 100.");
    if (outside(item.speed, MinimumParticleSpeed, MaximumParticleSpeed)) return itemError(item, index, QString("speed must be a whole number from %1 to %2.").arg(MinimumParticleSpeed).arg(MaximumParticleSpeed));
    if (outside(item.gravity, -MaximumParticleGravity, MaximumParticleGravity)) return itemError(item, index, QString("gravity must be a whole number from -%1 to %1.").arg(MaximumParticleGravity));
    if (outside(item.pixelSize, MinimumPixelSize, MaximumPixelSize)) return itemError(item, index, QString("pixelSize must be a whole number from %1 to %2.").arg(MinimumPixelSize).arg(MaximumPixelSize));
    return {};
}

QString validateSheet(const SceneItem& item, qsizetype index) {
    const auto hasFrames = item.frameWidth != 0 || item.frameHeight != 0;
    if (!hasFrames) {
        if (!item.animations.isEmpty() || !item.animation.isEmpty()) return itemError(item, index, "frameWidth and frameHeight are needed before an image can have animations.");
        return {};
    }
    const auto sizeProblem = [&](const char* field, int value) {
        if (value >= 1 && value <= MaximumImageDimension) return QString();
        return itemError(item, index, QString("%1 must be a whole number from 1 to %2.").arg(field).arg(MaximumImageDimension));
    };
    if (const auto problem = sizeProblem("frameWidth", item.frameWidth); !problem.isEmpty()) return problem;
    if (const auto problem = sizeProblem("frameHeight", item.frameHeight); !problem.isEmpty()) return problem;
    if (item.animations.size() > MaximumAnimationsPerSprite) return itemError(item, index, QString("a sprite can have at most %1 animations.").arg(MaximumAnimationsPerSprite));

    QSet<QString> names;
    for (const auto& animation : item.animations) {
        if (!isVariableName(animation.name)) return itemError(item, index, QString("the animation name \"%1\" is not allowed. A name is 1 to %2 letters, digits or _, and cannot start with a digit.").arg(animation.name).arg(MaximumVariableNameLength));
        if (names.contains(animation.name)) return itemError(item, index, QString("two animations are named \"%1\". Give each animation its own name.").arg(animation.name));
        names.insert(animation.name);
        if (animation.frames.isEmpty() || animation.frames.size() > MaximumFramesPerAnimation) return itemError(item, index, QString("animation \"%1\" needs 1 to %2 frames.").arg(animation.name).arg(MaximumFramesPerAnimation));
        for (const auto frame : animation.frames) {
            if (frame < 0 || frame > MaximumTileIndex) return itemError(item, index, QString("animation \"%1\" has frame %2, but frames are numbered from 0 to %3.").arg(animation.name).arg(frame).arg(MaximumTileIndex));
        }
        if (animation.fps < 1 || animation.fps > MaximumFramesPerSecond) return itemError(item, index, QString("animation \"%1\" needs fps from 1 to %2.").arg(animation.name).arg(MaximumFramesPerSecond));
    }
    if (!item.animation.isEmpty() && !names.contains(item.animation)) return itemError(item, index, QString("animation names \"%1\", which this sprite does not have.").arg(item.animation));
    return {};
}

QString validateTilemap(const SceneItem& item, qsizetype index) {
    if (!isImageSource(item.source)) return itemError(item, index, "source must be a .png file inside the project's assets folder, for example assets/tiles.png.");
    const auto tileSize = [&](const char* field, int value) {
        if (value >= MinimumTileDimension && value <= MaximumTileDimension) return QString();
        return itemError(item, index, QString("%1 must be a whole number from %2 to %3.").arg(field).arg(MinimumTileDimension).arg(MaximumTileDimension));
    };
    if (const auto problem = tileSize("tileWidth", item.tileWidth); !problem.isEmpty()) return problem;
    if (const auto problem = tileSize("tileHeight", item.tileHeight); !problem.isEmpty()) return problem;
    if (item.tileScale < 1 || item.tileScale > MaximumTileScale) return itemError(item, index, QString("tileScale must be a whole number from 1 to %1.").arg(MaximumTileScale));
    const auto sideProblem = [&](const char* field, int value) {
        if (value >= 1 && value <= MaximumTilemapSide) return QString();
        return itemError(item, index, QString("%1 must be a whole number from 1 to %2.").arg(field).arg(MaximumTilemapSide));
    };
    if (const auto problem = sideProblem("columns", item.columns); !problem.isEmpty()) return problem;
    if (const auto problem = sideProblem("rows", item.rows); !problem.isEmpty()) return problem;
    if (item.tiles.size() != qint64(item.columns) * item.rows) return itemError(item, index, QString("tiles must hold %1 tiles for this map, one row of %2 at a time.").arg(qint64(item.columns) * item.rows).arg(item.columns));
    for (const auto tile : item.tiles) {
        if (tile < -1 || tile > MaximumTileIndex) return itemError(item, index, QString("tiles has tile %1, but a tile is -1 for empty or a number from 0 to %2.").arg(tile).arg(MaximumTileIndex));
    }
    int previous = -1;
    for (const auto solid : item.solidTiles) {
        if (solid < 0 || solid > MaximumTileIndex) return itemError(item, index, QString("solidTiles has %1, but a tile number is 0 to %2.").arg(solid).arg(MaximumTileIndex));
        if (solid <= previous) return itemError(item, index, "solidTiles must be sorted from low to high, with no number twice.");
        previous = solid;
    }
    return {};
}

QString validatePhysics(const SceneItem& item, qsizetype index) {
    if (item.physics != SolidPhysics && item.physics != BodyPhysics) return itemError(item, index, QString("physics must be \"%1\" or \"%2\".").arg(SolidPhysics, BodyPhysics));
    if (item.type != RectangleType && item.type != ImageType) return itemError(item, index, "only a shape or an image can have physics. A tilemap blocks with its solid tiles instead.");
    if (item.screenFixed) return itemError(item, index, "an item that stays on the screen cannot have physics. Turn off Stays on screen or remove the physics.");
    if (item.controllable) return itemError(item, index, "an item with physics cannot also use Moves with the controller. Use Platformer controls for a body, or remove the physics.");
    if (item.physics != BodyPhysics) return {};
    if (item.width > MaximumBodySide || item.height > MaximumBodySide) return itemError(item, index, QString("a moving body can be at most %1 units on each side.").arg(MaximumBodySide));
    const auto outside = [](int value, int lowest, int highest) { return value < lowest || value > highest; };
    if (outside(item.gravityScale, 0, MaximumGravityScale)) return itemError(item, index, QString("gravity scale must be a whole percent from 0 to %1.").arg(MaximumGravityScale));
    if (outside(item.moveSpeed, MinimumMoveSpeed, MaximumMoveSpeed)) return itemError(item, index, QString("speed must be a whole number from %1 to %2.").arg(MinimumMoveSpeed).arg(MaximumMoveSpeed));
    if (outside(item.jumpHeight, MinimumJumpHeight, MaximumJumpHeight)) return itemError(item, index, QString("jump height must be a whole number from %1 to %2.").arg(MinimumJumpHeight).arg(MaximumJumpHeight));
    return {};
}

QString validateItem(const SceneItem& item, qsizetype index, QSet<QString>& usedIds) {
    if (item.id.isEmpty()) return itemError(item, index, "id must be a non-empty string.");
    if (usedIds.contains(item.id)) return itemError(item, index, QString("id \"%1\" is already used. Give each item its own id.").arg(item.id));
    usedIds.insert(item.id);

    const auto isImage = item.type == ImageType;
    const auto isParticles = item.type == ParticlesType;
    const auto isTilemap = item.type == TilemapType;
    if (item.type != RectangleType && item.type != TextType && !isImage && !isParticles && !isTilemap) return itemError(item, index, "type must be \"rectangle\", \"text\", \"image\", \"particles\" or \"tilemap\".");

    for (const auto& [field, value] : {std::pair<const char*, double>{"x", item.x}, {"y", item.y}}) {
        if (!isUsableNumber(value)) return itemError(item, index, QString("%1 must be a number between -1e38 and 1e38.").arg(field));
    }
    if (item.type == RectangleType || isImage || isParticles) {
        for (const auto& [field, value] : {std::pair<const char*, double>{"width", item.width}, {"height", item.height}}) {
            if (!isUsableNumber(value) || value <= 0) return itemError(item, index, QString("%1 must be a number above 0 and at most 1e38.").arg(field));
        }
    }

    if (!item.physics.isEmpty()) {
        const auto problem = validatePhysics(item, index);
        if (!problem.isEmpty()) return problem;
    }

    if (item.flipX && !isImage) return itemError(item, index, "only an image can be flipped.");
    if (item.pixelated && !isImage) return itemError(item, index, "only an image can be pixelated.");

    if (isImage) {
        if (!isImageSource(item.source)) return itemError(item, index, "source must be a .png file inside the project's assets folder, for example assets/logo.png.");
        return validateSheet(item, index);
    }
    if (isTilemap) return validateTilemap(item, index);

    if (!isColor(item.color)) return itemError(item, index, "color must look like #RRGGBB, for example #FFD23F.");

    if (isParticles) return validateParticles(item, index);
    if (item.type == TextType) {
        if (!isDisplayableText(item.text)) return itemError(item, index, "text must be 1 to 64 printable characters.");
        const auto parsed = parseTextTemplate(item.text);
        if (!parsed.problem.isEmpty()) return itemError(item, index, parsed.problem);
        if (textLength(parsed) > MaximumTextLength) return itemError(item, index, "text must be 1 to 64 printable characters.");
        if (item.scale < 1 || item.scale > MaximumScale) return itemError(item, index, "scale must be a whole number from 1 to 40.");
    }
    return {};
}

QJsonObject animationToJson(const SceneAnimation& animation) {
    QJsonArray frames;
    for (const auto frame : animation.frames) frames.append(frame);
    return {{"name", animation.name}, {"frames", frames}, {"fps", animation.fps}};
}

double numberOrInvalid(const QJsonValue& value) {
    return value.isDouble() ? value.toDouble() : std::numeric_limits<double>::quiet_NaN();
}

int wholeNumberOrInvalid(const QJsonValue& value) {
    if (!value.isDouble() || value.toDouble() != std::floor(value.toDouble()) || std::abs(value.toDouble()) > 1e8) return InvalidNumber;
    return static_cast<int>(value.toDouble());
}

QList<int> wholeNumberList(const QJsonValue& value) {
    QList<int> numbers;
    for (const auto& entry : value.toArray()) numbers.append(wholeNumberOrInvalid(entry));
    return numbers;
}

QString parseSheet(const QJsonObject& sheet, qsizetype index, SceneItem& item) {
    item.frameWidth = wholeNumberOrInvalid(sheet.value("frameWidth"));
    item.frameHeight = wholeNumberOrInvalid(sheet.value("frameHeight"));
    item.animation = sheet.value("animation").toString();
    const auto list = sheet.value("animations");
    if (!list.isUndefined() && !list.isArray()) return itemError(item, index, "animations must be a list of animations.");
    for (const auto& entry : list.toArray()) {
        if (!entry.isObject()) return itemError(item, index, "each animation must be a JSON object.");
        const auto object = entry.toObject();
        if (!object.value("frames").isArray()) return itemError(item, index, QString("animation \"%1\" needs a frames list.").arg(object.value("name").toString()));
        item.animations.append({object.value("name").toString(), wholeNumberList(object.value("frames")), wholeNumberOrInvalid(object.value("fps"))});
    }
    return {};
}

QString parseTilemap(const QJsonObject& object, qsizetype index, SceneItem& item) {
    item.source = object.value("source").toString();
    item.tileWidth = wholeNumberOrInvalid(object.value("tileWidth"));
    item.tileHeight = wholeNumberOrInvalid(object.value("tileHeight"));
    item.tileScale = wholeNumberOrInvalid(object.value("tileScale"));
    item.columns = wholeNumberOrInvalid(object.value("columns"));
    item.rows = wholeNumberOrInvalid(object.value("rows"));
    const auto rowsValue = object.value("tiles");
    if (!rowsValue.isArray()) return itemError(item, index, "tiles must be a list with one text per row of the map.");
    const auto rows = rowsValue.toArray();
    if (item.columns >= 1 && item.columns <= MaximumTilemapSide && item.rows >= 1 && item.rows <= MaximumTilemapSide) {
        if (rows.size() != item.rows) return itemError(item, index, QString("tiles has %1 rows, but the map has %2 rows.").arg(rows.size()).arg(item.rows));
        item.tiles.reserve(qint64(item.columns) * item.rows);
        for (const auto& row : rows) {
            QList<int> cells;
            if (!row.isString() || !decodeTileRow(row.toString(), item.columns, cells)) return itemError(item, index, QString("tiles has the row \"%1\", which is not %2 tiles written as value or value*count.").arg(row.toString().left(32)).arg(item.columns));
            item.tiles += cells;
        }
    }
    if (const auto solid = object.value("solidTiles"); solid.isArray()) item.solidTiles = wholeNumberList(solid);
    else if (!solid.isUndefined()) return itemError(item, index, "solidTiles must be a list of tile numbers.");
    if (item.tileWidth >= 0 && item.tileScale >= 0) updateTilemapSize(item);
    return {};
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
    } else if (item.type != TilemapType) {
        item.width = numberOrInvalid(object.value("width"));
        item.height = numberOrInvalid(object.value("height"));
    }
    if (item.type == ImageType) {
        item.source = object.value("source").toString();
        for (const auto& [field, target] : {std::pair<const char*, bool*>{"pixelated", &item.pixelated}, {"flipX", &item.flipX}}) {
            const auto flag = object.value(field);
            if (!flag.isUndefined() && !flag.isBool()) return itemError(item, index, QString("%1 must be true or false.").arg(field));
            *target = flag.toBool();
        }
        const auto sheet = object.value("sheet");
        if (!sheet.isUndefined()) {
            if (!sheet.isObject()) return itemError(item, index, "sheet must be a JSON object.");
            const auto problem = parseSheet(sheet.toObject(), index, item);
            if (!problem.isEmpty()) return problem;
        }
    }
    if (item.type == TilemapType) {
        const auto problem = parseTilemap(object, index, item);
        if (!problem.isEmpty()) return problem;
    }
    if (item.type == ParticlesType) {
        item.preset = object.value("preset").toString();
        item.palette = object.value("palette").toString();
        item.amount = wholeNumberOrInvalid(object.value("amount"));
        item.spread = wholeNumberOrInvalid(object.value("spread"));
        item.speed = wholeNumberOrInvalid(object.value("speed"));
        item.gravity = wholeNumberOrInvalid(object.value("gravity"));
        item.pixelSize = wholeNumberOrInvalid(object.value("pixelSize"));
        const auto looping = object.value("looping");
        if (!looping.isBool()) return itemError(item, index, "looping must be true or false.");
        item.looping = looping.toBool();
    }

    const auto physics = object.value("physics");
    if (!physics.isUndefined()) {
        if (!physics.isString()) return itemError(item, index, "physics must be \"solid\" or \"body\".");
        item.physics = physics.toString();
        if (item.physics == BodyPhysics) {
            const auto optionalNumber = [&](const char* field, int fallback) {
                const auto number = object.value(field);
                return number.isUndefined() ? fallback : wholeNumberOrInvalid(number);
            };
            item.gravityScale = optionalNumber("gravityScale", DefaultGravityScale);
            item.moveSpeed = optionalNumber("moveSpeed", DefaultMoveSpeed);
            item.jumpHeight = optionalNumber("jumpHeight", DefaultJumpHeight);
            const auto platformer = object.value("platformer");
            if (!platformer.isUndefined() && !platformer.isBool()) return itemError(item, index, "platformer must be true or false.");
            item.platformer = platformer.toBool();
        }
    }

    const auto controllable = object.value("controllable");
    if (!controllable.isUndefined() && !controllable.isBool()) return itemError(item, index, "controllable must be true or false.");
    item.controllable = controllable.toBool();
    const auto screenFixed = object.value("screenFixed");
    if (!screenFixed.isUndefined() && !screenFixed.isBool()) return itemError(item, index, "screenFixed must be true or false.");
    item.screenFixed = screenFixed.toBool();
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
    if (item.screenFixed) object.insert("screenFixed", true);
    if (!item.physics.isEmpty()) {
        object.insert("physics", item.physics);
        if (item.physics == BodyPhysics) {
            object.insert("gravityScale", item.gravityScale);
            object.insert("platformer", item.platformer);
            object.insert("moveSpeed", item.moveSpeed);
            object.insert("jumpHeight", item.jumpHeight);
        }
    }
    if (item.type != ImageType && item.type != TilemapType) object.insert("color", item.color);
    if (item.type == ImageType) {
        object.insert("source", item.source);
        if (item.pixelated) object.insert("pixelated", true);
        if (item.flipX) object.insert("flipX", true);
        if (isSpriteSheet(item)) {
            QJsonArray animations;
            for (const auto& animation : item.animations) animations.append(animationToJson(animation));
            QJsonObject sheet{{"frameWidth", item.frameWidth}, {"frameHeight", item.frameHeight}, {"animations", animations}};
            if (!item.animation.isEmpty()) sheet.insert("animation", item.animation);
            object.insert("sheet", sheet);
        }
    }
    if (item.type == TilemapType) {
        object.insert("source", item.source);
        object.insert("tileWidth", item.tileWidth);
        object.insert("tileHeight", item.tileHeight);
        object.insert("tileScale", item.tileScale);
        object.insert("columns", item.columns);
        object.insert("rows", item.rows);
        QJsonArray rows;
        for (int row = 0; row < item.rows; ++row) rows.append(encodeTileRow(item.tiles, row * item.columns, item.columns));
        object.insert("tiles", rows);
        if (!item.solidTiles.isEmpty()) {
            QJsonArray solid;
            for (const auto tile : item.solidTiles) solid.append(tile);
            object.insert("solidTiles", solid);
        }
    }
    if (item.type == ParticlesType) {
        object.insert("preset", item.preset);
        object.insert("palette", item.palette);
        object.insert("amount", item.amount);
        object.insert("spread", item.spread);
        object.insert("speed", item.speed);
        object.insert("gravity", item.gravity);
        object.insert("pixelSize", item.pixelSize);
        object.insert("looping", item.looping);
    }
    if (item.type == TextType) {
        object.insert("scale", item.scale);
        object.insert("text", item.text);
    } else if (item.type != TilemapType) {
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


QByteArray itemLine(const SceneItem& item, const QStringList& imageSources, int particleIndex, int tilemapIndex, int spriteIndex, int bodyIndex) {
    const auto isText = item.type == TextType;
    const auto isImage = item.type == ImageType;
    const auto isParticles = item.type == ParticlesType;
    const auto isTilemap = item.type == TilemapType;
    const auto shown = isText ? widestText(item.text) : QString();
    const auto width = isText ? double((shown.size() * 6 - 1) * item.scale) : item.width;
    const auto height = isText ? double(7 * item.scale) : item.height;
    QByteArray line = "    {Kind::";
    line += isText ? "Text" : isImage ? "Image" : isParticles ? "Particles" : isTilemap ? "Tilemap" : "Rectangle";
    for (const auto value : {item.x, item.y, width, height}) line += ", " + floatLiteral(value);
    line += ", " + ((isImage || isTilemap) ? QByteArray("0.0f, 0.0f, 0.0f") : colorLiterals(item.color));
    line += ", " + floatLiteral(isText ? item.scale : 0);
    line += ", \"" + (isText ? textLiteral(shown) : QByteArray()) + "\", ";
    line += item.controllable ? "true" : "false";
    const auto imageIndex = (isImage || isTilemap) ? QByteArray::number(imageSources.indexOf(item.source)) : QByteArray("-1");
    const auto hasPhysics = !item.physics.isEmpty();
    if (isTilemap || spriteIndex >= 0 || item.pixelated || item.flipX || item.screenFixed || hasPhysics) {
        // These items spell out every field, so the older lines below stay as they were.
        line += ", " + imageIndex + ", " + QByteArray::number(isParticles ? particleIndex : -1);
        line += ", " + QByteArray::number(isTilemap ? tilemapIndex : -1) + ", " + QByteArray::number(spriteIndex);
        line += QByteArray(", ") + (item.screenFixed ? "true" : "false");
        line += QByteArray(", ") + ((item.pixelated || isTilemap) ? "true" : "false");
        line += QByteArray(", ") + (item.flipX ? "true" : "false");
        if (hasPhysics) line += ", " + QByteArray::number(bodyIndex) + ", " + (item.physics == SolidPhysics ? "true" : "false");
    } else if (isImage) {
        line += ", " + imageIndex;
    } else if (isParticles) {
        line += ", -1, " + QByteArray::number(particleIndex);
    }
    line += "},\n";
    return line;
}

struct RuntimeFile {
    QString relativePath;
    QByteArray contents;
};

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
    if (scene.worldWidth < ScreenWidthUnits || scene.worldWidth > MaximumWorldSide) return QString("The world's width must be a whole number from %1 to %2.").arg(ScreenWidthUnits).arg(MaximumWorldSide);
    if (scene.worldHeight < ScreenHeightUnits || scene.worldHeight > MaximumWorldSide) return QString("The world's height must be a whole number from %1 to %2.").arg(ScreenHeightUnits).arg(MaximumWorldSide);
    const auto inCameraRange = [](double value, int worldSide, int screenSide) { return std::isfinite(value) && value >= 0 && value <= worldSide - screenSide; };
    if (!inCameraRange(scene.cameraX, scene.worldWidth, ScreenWidthUnits) || !inCameraRange(scene.cameraY, scene.worldHeight, ScreenHeightUnits)) return "The camera must start inside the world: from 0 up to the world's size minus the 1920x1080 screen.";
    if (scene.items.size() > MaximumItems) return QString("The scene has %1 items, but a scene can hold at most %2. Remove some items and try again.").arg(scene.items.size()).arg(MaximumItems);
    QSet<QString> usedIds;
    bool hasParticles = false;
    bool hasTilemap = false;
    bool hasSheet = false;
    for (qsizetype index = 0; index < scene.items.size(); ++index) {
        const auto problem = validateItem(scene.items[index], index, usedIds);
        if (!problem.isEmpty()) return problem;
        hasParticles = hasParticles || scene.items[index].type == ParticlesType;
        hasTilemap = hasTilemap || scene.items[index].type == TilemapType;
        hasSheet = hasSheet || isSpriteSheet(scene.items[index]);
    }
    qsizetype bodies = 0;
    qsizetype solids = 0;
    for (const auto& item : scene.items) {
        if (item.physics == BodyPhysics) ++bodies;
        if (item.physics == SolidPhysics) ++solids;
    }
    if (bodies > MaximumSceneBodies) return QString("This scene has %1 moving bodies, but a scene can have at most %2. Turn physics off on some items and try again.").arg(bodies).arg(MaximumSceneBodies);
    if (solids > MaximumSceneSolidItems) return QString("This scene has %1 solid items, but a scene can have at most %2. Paint walls on a tilemap instead, or turn physics off on some items.").arg(solids).arg(MaximumSceneSolidItems);
    if (const auto cells = sceneTileCells(scene); cells > MaximumSceneTileCells) {
        return QString("The tilemaps hold %1 tiles, but a scene can hold at most %2. Make a tilemap smaller or remove a layer.").arg(cells).arg(MaximumSceneTileCells);
    }
    // Only scenes that use them are bound by these, so older scenes keep working.
    if ((hasParticles || hasTilemap) && sceneQuadCount(scene) > MaximumSceneBlocks) {
        if (hasTilemap) return QString("This scene would draw %1 tiles and blocks at once, but a scene can draw at most %2. Make the tiles bigger or remove a layer.").arg(sceneQuadCount(scene)).arg(MaximumSceneBlocks);
        return QString("This scene would draw %1 blocks, but a scene with particles can draw at most %2. Lower Amount on a particles item or remove some text.").arg(sceneQuadCount(scene)).arg(MaximumSceneBlocks);
    }
    if ((hasTilemap || hasSheet) && sceneDrawCalls(scene) > MaximumSceneDrawCalls) {
        return QString("This scene would need %1 draw calls, but a scene with tiles or sprites can use at most %2. Put items that use the same picture next to each other in the Layers list, or remove some.").arg(sceneDrawCalls(scene)).arg(MaximumSceneDrawCalls);
    }
    return {};
}

bool isSpriteSheet(const SceneItem& item) {
    return item.type == ImageType && item.frameWidth > 0 && item.frameHeight > 0;
}

void updateTilemapSize(SceneItem& item) {
    item.width = static_cast<double>(item.columns) * item.tileWidth * item.tileScale;
    item.height = static_cast<double>(item.rows) * item.tileHeight * item.tileScale;
}

void resizeTilemap(SceneItem& item, int columns, int rows) {
    QList<int> resized(qint64(std::max(columns, 0)) * std::max(rows, 0), -1);
    for (int row = 0; row < std::min(rows, item.rows); ++row) {
        for (int column = 0; column < std::min(columns, item.columns); ++column) resized[row * columns + column] = item.tiles.value(row * item.columns + column, -1);
    }
    item.tiles = resized;
    item.columns = columns;
    item.rows = rows;
    updateTilemapSize(item);
}

int tilemapReservedQuads(const SceneItem& item) {
    const auto reserved = [](int cells, int cellSize, int screen) {
        return std::min(qint64(cells), qint64(std::ceil(static_cast<double>(screen) / std::max(cellSize, 1))) + 1);
    };
    return static_cast<int>(reserved(item.columns, item.tileWidth * item.tileScale, ScreenWidthUnits) * reserved(item.rows, item.tileHeight * item.tileScale, ScreenHeightUnits));
}

int sceneTileCells(const Scene& scene) {
    qint64 total = 0;
    for (const auto& item : scene.items) {
        if (item.type == TilemapType) total += qint64(item.columns) * item.rows;
    }
    return static_cast<int>(std::min<qint64>(total, std::numeric_limits<int>::max()));
}

int sceneDrawCalls(const Scene& scene) {
    int calls = 1; // the background opens the first solid draw
    QString open;
    for (const auto& item : scene.items) {
        if (item.type == ImageType || item.type == TilemapType) {
            const auto nearest = item.pixelated || item.type == TilemapType;
            const auto key = item.source + (nearest ? "#nearest" : "#linear");
            if (key != open) {
                ++calls;
                open = key;
            }
        } else if (!open.isEmpty()) {
            ++calls;
            open.clear();
        }
    }
    return calls;
}

QString encodeTileRow(const QList<int>& tiles, int first, int count) {
    QStringList runs;
    for (int index = first; index < first + count;) {
        const auto value = tiles.value(index, -1);
        int length = 1;
        while (index + length < first + count && tiles.value(index + length, -1) == value) ++length;
        runs << (length == 1 ? QString::number(value) : QString("%1*%2").arg(value).arg(length));
        index += length;
    }
    return runs.join(',');
}

bool decodeTileRow(const QString& text, int columns, QList<int>& out) {
    out.clear();
    if (text.isEmpty()) return false;
    for (const auto& run : text.split(',')) {
        const auto parts = run.split('*');
        if (parts.size() > 2 || parts.first().isEmpty() || parts.first().trimmed() != parts.first()) return false;
        bool valueOk = false;
        const auto value = parts.first().toInt(&valueOk);
        if (!valueOk) return false;
        int count = 1;
        if (parts.size() == 2) {
            bool countOk = false;
            count = parts.last().toInt(&countOk);
            if (!countOk || count < 1 || parts.last().trimmed() != parts.last()) return false;
        }
        if (count > columns - out.size()) return false;
        for (int index = 0; index < count; ++index) out.append(value);
    }
    return out.size() == columns;
}

QByteArray tilemapCellBytes(const SceneItem& item) {
    QByteArray bytes;
    bytes.reserve(item.tiles.size() * 4);
    for (const auto tile : item.tiles) {
        const auto value = static_cast<std::uint32_t>(tile);
        for (int byte = 0; byte < 4; ++byte) bytes.append(static_cast<char>((value >> (byte * 8)) & 0xff));
    }
    return bytes;
}

QString sceneImagesProblem(const Scene& scene, const QList<SceneImageData>& images) {
    const auto findImage = [&](const QString& source) -> const SceneImageData* {
        const auto found = std::ranges::find(images, source, &SceneImageData::source);
        return found == images.end() ? nullptr : &*found;
    };
    for (qsizetype index = 0; index < scene.items.size(); ++index) {
        const auto& item = scene.items[index];
        if (isSpriteSheet(item)) {
            const auto* image = findImage(item.source);
            if (!image) continue;
            if (item.frameWidth > image->width || item.frameHeight > image->height) {
                return itemError(item, index, QString("the frames are %1x%2 pixels, but %3 is only %4x%5. Choose a smaller frame size.").arg(item.frameWidth).arg(item.frameHeight).arg(item.source).arg(image->width).arg(image->height));
            }
            const auto frames = (image->width / item.frameWidth) * (image->height / item.frameHeight);
            for (const auto& animation : item.animations) {
                for (const auto frame : animation.frames) {
                    if (frame >= frames) return itemError(item, index, QString("animation \"%1\" uses frame %2, but the sheet has only %3 frames.").arg(animation.name).arg(frame).arg(frames));
                }
            }
        }
        if (item.type == TilemapType) {
            const auto* image = findImage(item.source);
            if (!image) continue;
            if (item.tileWidth > image->width || item.tileHeight > image->height) {
                return itemError(item, index, QString("the tiles are %1x%2 pixels, but %3 is only %4x%5. Choose a smaller tile size.").arg(item.tileWidth).arg(item.tileHeight).arg(item.source).arg(image->width).arg(image->height));
            }
            const auto tileCount = (image->width / item.tileWidth) * (image->height / item.tileHeight);
            for (const auto tile : item.tiles) {
                if (tile >= tileCount) return itemError(item, index, QString("it uses tile %1, but the tileset has only %2 tiles. Paint over it or choose a larger tileset.").arg(tile).arg(tileCount));
            }
            for (const auto tile : item.solidTiles) {
                if (tile >= tileCount) return itemError(item, index, QString("it marks tile %1 solid, but the tileset has only %2 tiles.").arg(tile).arg(tileCount));
            }
        }
    }
    return {};
}

bool isVariableName(const QString& name) {
    if (name.isEmpty() || name.size() > MaximumVariableNameLength) return false;
    for (qsizetype index = 0; index < name.size(); ++index) {
        const auto code = name[index].unicode();
        const auto isLetter = (code >= 'a' && code <= 'z') || (code >= 'A' && code <= 'Z');
        const auto isDigit = code >= '0' && code <= '9';
        if (!isLetter && code != '_' && (!isDigit || index == 0)) return false;
    }
    return true;
}

TextTemplate parseTextTemplate(const QString& text) {
    TextTemplate result;
    QString literal;
    const auto flush = [&] {
        if (literal.isEmpty()) return;
        result.parts.append({literal, {}});
        literal.clear();
    };
    for (qsizetype index = 0; index < text.size();) {
        const auto character = text[index];
        const auto doubled = index + 1 < text.size() && text[index + 1] == character;
        if (character == '{' && doubled) {
            literal += '{';
            index += 2;
        } else if (character == '}' && doubled) {
            literal += '}';
            index += 2;
        } else if (character == '{') {
            const auto end = text.indexOf('}', index + 1);
            if (end < 0) {
                result.problem = "text has a { with no matching }. To show a brace, type it twice: {{.";
                return result;
            }
            const auto name = text.mid(index + 1, end - index - 1);
            if (name.isEmpty()) {
                result.problem = "text has {} with no variable name in it. To show braces, type each one twice: {{ and }}.";
                return result;
            }
            if (!isVariableName(name)) {
                result.problem = QString("text uses {%1}, but a variable name is letters, digits and _, starting with a letter or _. To show a brace, type it twice: {{.").arg(name);
                return result;
            }
            flush();
            result.parts.append({{}, name});
            index = end + 1;
        } else if (character == '}') {
            result.problem = "text has a } with no matching {. To show a brace, type it twice: }}.";
            return result;
        } else {
            literal += character;
            ++index;
        }
    }
    flush();
    return result;
}

bool textUsesVariables(const QString& text) {
    return !textVariables(text).isEmpty();
}

QStringList textVariables(const QString& text) {
    QStringList names;
    for (const auto& part : parseTextTemplate(text).parts) {
        if (!part.variable.isEmpty() && !names.contains(part.variable)) names << part.variable;
    }
    return names;
}

QString displayedText(const QString& text, const QList<LogicVariable>& variables) {
    const auto parsed = parseTextTemplate(text);
    if (!parsed.problem.isEmpty()) return text;
    QString shown;
    for (const auto& part : parsed.parts) {
        if (part.variable.isEmpty()) {
            shown += part.text;
            continue;
        }
        const auto known = std::ranges::find(variables, part.variable, &LogicVariable::name);
        shown += known == variables.end() ? '{' + part.variable + '}' : QString::number(known->initial);
    }
    return shown;
}

QString renameVariableInText(const QString& text, const QString& name, const QString& newName) {
    const auto parsed = parseTextTemplate(text);
    if (!parsed.problem.isEmpty()) return text;
    QString renamed;
    for (const auto& part : parsed.parts) {
        if (part.variable.isEmpty()) {
            auto plain = part.text;
            renamed += plain.replace('{', "{{").replace('}', "}}");
        } else {
            renamed += '{' + (part.variable == name ? newName : part.variable) + '}';
        }
    }
    return renamed;
}

int sceneQuadCount(const Scene& scene) {
    qint64 total = 1; // the background
    for (const auto& item : scene.items) {
        if (item.type == TextType) total += widestText(item.text).size();
        else if (item.type == ParticlesType) total += item.amount;
        else if (item.type == TilemapType) total += tilemapReservedQuads(item);
        else total += 1;
    }
    return static_cast<int>(std::min<qint64>(total, std::numeric_limits<int>::max()));
}

QStringList particlePresetNames() { return {"fireworks", "fountain", "sparks", "snow", "rain"}; }
QStringList particlePaletteNames() { return {"color", "festival", "fire", "ice"}; }

void applyParticlePreset(SceneItem& item, const QString& preset) {
    const struct Look {
        const char* name;
        const char* palette;
        const char* color;
        int amount, spread, speed, gravity, pixelSize;
        double width, height;
    } looks[] = {
        {"fireworks", "festival", "#FFD23F", 600, 50, 100, 100, 12, 1400, 900},
        {"fountain", "color", "#67E3C8", 800, 12, 100, 100, 10, 300, 500},
        {"sparks", "fire", "#FF9A3C", 300, 100, 100, 100, 8, 240, 240},
        {"snow", "color", "#F3F1E2", 900, 40, 100, 100, 8, 1920, 1080},
        {"rain", "color", "#8FC7FF", 1400, 10, 100, 100, 6, 1920, 1080},
    };
    for (const auto& look : looks) {
        if (preset != look.name) continue;
        item.preset = preset;
        item.palette = look.palette;
        item.color = look.color;
        item.amount = look.amount;
        item.spread = look.spread;
        item.speed = look.speed;
        item.gravity = look.gravity;
        item.pixelSize = look.pixelSize;
        item.looping = true;
        item.width = look.width;
        item.height = look.height;
        return;
    }
    item.preset = preset; // an unknown name is left for validation to reject
}

namespace {

bool isSceneId(const QString& id) {
    if (id.isEmpty() || id.size() > 64) return false;
    for (const auto character : id) {
        const auto code = character.unicode();
        const auto isLetter = (code >= 'a' && code <= 'z') || (code >= 'A' && code <= 'Z');
        const auto isDigit = code >= '0' && code <= '9';
        if (!isLetter && !isDigit && code != '-' && code != '_') return false;
    }
    return true;
}

// Reads the background, world, camera and items of a scene. Older files hold exactly this at their top level.
QString parseSceneObject(const QJsonObject& root, int version, const QString& fileName, Scene& scene) {
    if (!root.value("items").isArray()) return QString("%1 has no items list. Add \"items\": [] to the file.").arg(fileName);
    scene.background = root.value("background").toString();
    const auto world = root.value("world");
    const auto camera = root.value("camera");
    if (!world.isUndefined() || !camera.isUndefined()) {
        if (version < WorldSchema) return QString("%1 uses a world or camera, which needs schemaVersion %2. Change schemaVersion to %2 and try again.").arg(fileName).arg(WorldSchema);
        if (!world.isUndefined()) {
            scene.worldWidth = world.isObject() ? wholeNumberOrInvalid(world.toObject().value("width")) : InvalidNumber;
            scene.worldHeight = world.isObject() ? wholeNumberOrInvalid(world.toObject().value("height")) : InvalidNumber;
        }
        if (!camera.isUndefined()) {
            scene.cameraX = camera.isObject() ? numberOrInvalid(camera.toObject().value("x")) : std::numeric_limits<double>::quiet_NaN();
            scene.cameraY = camera.isObject() ? numberOrInvalid(camera.toObject().value("y")) : std::numeric_limits<double>::quiet_NaN();
        }
    }
    const auto items = root.value("items").toArray();
    for (qsizetype index = 0; index < items.size(); ++index) {
        SceneItem item;
        const auto problem = parseItem(items.at(index), index, item);
        if (!problem.isEmpty()) return problem;
        if (version < ImageSchema && item.type == ImageType) return QString("%1 uses an image item, which needs schemaVersion %2. Change schemaVersion to %2 and try again.").arg(fileName).arg(ImageSchema);
        if (version < ParticleSchema && item.type == ParticlesType) return QString("%1 uses a particles item, which needs schemaVersion %2. Change schemaVersion to %2 and try again.").arg(fileName).arg(ParticleSchema);
        if (version < VariableTextSchema && item.type == TextType) item.text.replace('{', "{{").replace('}', "}}");
        const auto usesWorldItem = item.type == TilemapType || item.frameWidth != 0 || item.frameHeight != 0 || !item.animations.isEmpty() || item.pixelated || item.flipX || item.screenFixed;
        if (version < WorldSchema && usesWorldItem) return QString("%1 uses a tilemap, sprite sheet or screen-fixed item, which needs schemaVersion %2. Change schemaVersion to %2 and try again.").arg(fileName).arg(WorldSchema);
        if (version < SceneSetSchema && !item.physics.isEmpty()) return QString("%1 uses physics, which needs schemaVersion %2. Change schemaVersion to %2 and try again.").arg(fileName).arg(SceneSetSchema);
        scene.items.append(item);
    }
    return {};
}

QJsonObject sceneToJson(const Scene& scene) {
    QJsonArray items;
    for (const auto& item : scene.items) items.append(itemToJson(item));
    QJsonObject object{{"id", scene.id}, {"name", scene.name}, {"background", scene.background}, {"items", items}};
    // A stage-sized world saves no world and no camera, so the file reads as it did.
    if (scene.worldWidth != ScreenWidthUnits || scene.worldHeight != ScreenHeightUnits) object.insert("world", QJsonObject{{"width", scene.worldWidth}, {"height", scene.worldHeight}});
    if (scene.cameraX != 0 || scene.cameraY != 0) object.insert("camera", QJsonObject{{"x", scene.cameraX}, {"y", scene.cameraY}});
    return object;
}

}

QString validateSceneSet(const SceneSet& set) {
    if (set.scenes.isEmpty()) return "A project needs at least one scene.";
    if (set.scenes.size() > MaximumScenes) return QString("This project has %1 scenes, but a project can hold at most %2. Delete some scenes and try again.").arg(set.scenes.size()).arg(MaximumScenes);
    QSet<QString> ids;
    QSet<QString> names;
    for (const auto& scene : set.scenes) {
        if (!isSceneId(scene.id)) return QString("The scene id \"%1\" is not allowed. An id is 1 to 64 letters, digits, - or _.").arg(scene.id);
        if (ids.contains(scene.id)) return QString("Two scenes have the id \"%1\". Give each scene its own id.").arg(scene.id);
        ids.insert(scene.id);
        if (scene.name.isEmpty() || scene.name.size() > MaximumSceneNameLength) return QString("A scene name needs 1 to %1 characters. Rename the scene \"%2\" and try again.").arg(MaximumSceneNameLength).arg(scene.name);
        const auto folded = scene.name.toCaseFolded();
        if (names.contains(folded)) return QString("Two scenes are named \"%1\". Give each scene its own name.").arg(scene.name);
        names.insert(folded);
        if (const auto problem = validateScene(scene); !problem.isEmpty()) return set.scenes.size() == 1 ? problem : QString("In the scene \"%1\": %2").arg(scene.name, problem);
    }
    if (!ids.contains(set.startScene)) return QString("The start scene \"%1\" is not one of the project's scenes. Pick a start scene in the Scene tab.").arg(set.startScene);
    return {};
}

SceneSetLoadResult loadSceneSet(const QString& path) {
    const auto fileName = QFileInfo(path).fileName();
    const auto failure = [](const QString& message) { return SceneSetLoadResult{std::nullopt, message}; };
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return failure(QString("%1 could not be opened: %2. Check that the file exists and try again.").arg(fileName, file.errorString()));

    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) return failure(QString("%1 is not valid JSON: %2 at character %3. Fix the file or restore a backup.").arg(fileName, parseError.errorString(), QString::number(parseError.offset)));
    if (!document.isObject()) return failure(QString("%1 must contain a JSON object with the scene in it.").arg(fileName));
    const auto root = document.object();

    const auto schema = root.value("schemaVersion");
    const auto version = schema.toInt(-1);
    if (!schema.isDouble() || version < OldestLoadableSchema || version > CurrentSchema) return failure(QString("%1 needs a schemaVersion from %2 to %3, the versions this Studio supports.").arg(fileName).arg(OldestLoadableSchema).arg(CurrentSchema));

    SceneSet set;
    if (version < SceneSetSchema) {
        // An older project is one scene with the file's own fields.
        Scene scene;
        const auto problem = parseSceneObject(root, version, fileName, scene);
        if (!problem.isEmpty()) return failure(problem);
        set.scenes = {scene};
        set.startScene = scene.id;
    } else {
        const auto scenes = root.value("scenes");
        if (!scenes.isArray()) return failure(QString("%1 has no scenes list. Add a \"scenes\" list to the file.").arg(fileName));
        const auto startScene = root.value("startScene");
        if (!startScene.isString()) return failure(QString("%1 needs startScene to name one of its scenes.").arg(fileName));
        set.startScene = startScene.toString();
        for (const auto& value : scenes.toArray()) {
            if (!value.isObject()) return failure(QString("%1: each scene must be a JSON object.").arg(fileName));
            const auto object = value.toObject();
            Scene scene;
            scene.id = object.value("id").toString();
            scene.name = object.value("name").toString();
            const auto problem = parseSceneObject(object, version, fileName, scene);
            if (!problem.isEmpty()) return failure(problem);
            set.scenes.append(scene);
        }
    }
    const auto problem = validateSceneSet(set);
    if (!problem.isEmpty()) return failure(problem);
    return {set, {}};
}

SceneLoadResult loadScene(const QString& path) {
    const auto loaded = loadSceneSet(path);
    if (!loaded.set) return failure(loaded.error);
    const auto found = std::ranges::find(loaded.set->scenes, loaded.set->startScene, &Scene::id);
    return {found != loaded.set->scenes.end() ? *found : loaded.set->scenes.first(), {}};
}

bool saveSceneSet(const QString& path, const SceneSet& set, QString& error) {
    error = validateSceneSet(set);
    if (!error.isEmpty()) return false;

    QJsonArray scenes;
    for (const auto& scene : set.scenes) scenes.append(sceneToJson(scene));
    const QJsonObject root{{"schemaVersion", CurrentSchema}, {"startScene", set.startScene}, {"scenes", scenes}};
    const auto contents = QJsonDocument(root).toJson();

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(contents) != contents.size() || !file.commit()) {
        error = QString("%1 could not be saved: %2. Check that the folder exists and is writable.").arg(QFileInfo(path).fileName(), file.errorString());
        return false;
    }
    return true;
}

bool saveScene(const QString& path, const Scene& scene, QString& error) {
    SceneSet set;
    set.scenes = {scene};
    set.startScene = scene.id;
    return saveSceneSet(path, set, error);
}

QByteArray textLiteral(const QString& text) {
    QByteArray escaped;
    auto previous = '\0';
    for (const char character : text.toLatin1()) {
        // A second '?' is escaped because "??=" and friends are trigraphs, which the game's -Werror rejects.
        if (character == '"' || character == '\\' || (character == '?' && previous == '?')) escaped += '\\';
        escaped += character;
        previous = character;
    }
    return escaped;
}

QStringList sceneImageSources(const SceneSet& set) {
    QStringList sources;
    for (const auto& scene : set.scenes) {
        for (const auto& source : sceneImageSources(scene)) {
            if (!sources.contains(source)) sources << source;
        }
    }
    return sources;
}

int sceneItemCount(const SceneSet& set) {
    int count = 0;
    for (const auto& scene : set.scenes) count += static_cast<int>(scene.items.size());
    return count;
}

QStringList sceneImageSources(const Scene& scene) {
    QStringList sources;
    for (const auto& item : scene.items) {
        if ((item.type == ImageType || item.type == TilemapType) && !sources.contains(item.source)) sources << item.source;
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

QByteArray generateImageSource(const QList<SceneImageData>& images, int tilemapCount) {
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
    for (int index = 0; index < tilemapCount; ++index) {
        const auto number = QByteArray::number(index);
        const auto symbol = "StudioSceneTiles" + number;
        source += "__asm__(\".pushsection .rodata\\n.balign 16\\n.hidden " + symbol + "\\n" + symbol + ":\\n.incbin \\\"\" SCENE_IMAGE_DIRECTORY \"/tilemap-" + number + ".tiles\\\"\\n.popsection\\n\");\n";
        source += "extern \"C\" const std::int32_t " + symbol + "[];\n";
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
    if (tilemapCount == 0) {
        source += "const std::array<const std::int32_t*, 0> TilemapCells{};\n";
    } else {
        source += "const std::array<const std::int32_t*, TilemapCount> TilemapCells{{\n";
        for (int index = 0; index < tilemapCount; ++index) source += "    StudioSceneTiles" + QByteArray::number(index) + ",\n";
        source += "}};\n";
    }
    source += "\n}\n";
    return source;
}

// Stable across runs and across saves, so a scene's particles play the same way every time.
std::uint32_t particleSeed(const QString& id) {
    std::uint32_t hash = 2166136261u;
    for (const char byte : id.toUtf8()) hash = (hash ^ static_cast<unsigned char>(byte)) * 16777619u;
    return hash;
}

QByteArray particleEffectLine(const SceneItem& item) {
    QByteArray line = "    {" + QByteArray::number(particlePresetNames().indexOf(item.preset));
    line += ", " + QByteArray::number(particlePaletteNames().indexOf(item.palette));
    line += ", " + QByteArray::number(item.amount) + ", " + QByteArray::number(particleSeed(item.id)) + "u";
    for (const auto value : {item.spread / 100.0, item.speed / 100.0, item.gravity / 100.0, double(item.pixelSize)}) line += ", " + floatLiteral(value);
    line += item.looping ? ", true},\n" : ", false},\n";
    return line;
}

// Sprites and their animations, each table in the order the items that use them appear.
QByteArray spriteTables(const Scene& scene, const QList<SceneImageData>& images) {
    QByteArray sprites, animations, frames;
    int spriteCount = 0, animationCount = 0, frameCount = 0;
    for (const auto& item : scene.items) {
        if (!isSpriteSheet(item)) continue;
        const auto& image = *std::ranges::find(images, item.source, &SceneImageData::source);
        int startIndex = 0;
        if (!item.animation.isEmpty()) {
            const auto found = std::ranges::find(item.animations, item.animation, &SceneAnimation::name);
            if (found != item.animations.end()) startIndex = static_cast<int>(found - item.animations.begin());
        }
        sprites += "    {" + QByteArray::number(item.frameWidth) + ", " + QByteArray::number(item.frameHeight)
            + ", " + QByteArray::number(image.width / item.frameWidth) + ", " + QByteArray::number(image.height / item.frameHeight)
            + ", " + QByteArray::number(animationCount) + ", " + QByteArray::number(item.animations.size())
            + ", " + QByteArray::number(startIndex) + "},\n";
        for (const auto& animation : item.animations) {
            animations += "    {" + QByteArray::number(frameCount) + ", " + QByteArray::number(animation.frames.size()) + ", " + QByteArray::number(animation.fps) + "},\n";
            for (const auto frame : animation.frames) frames += (frameCount++ == 0 ? "" : ", ") + QByteArray::number(frame);
            ++animationCount;
        }
        ++spriteCount;
    }
    const auto table = [](const char* type, const char* name, int count, const QByteArray& body) {
        if (count == 0) return QByteArray("inline constexpr std::array<") + type + ", 0> " + name + "{};\n";
        return QByteArray("inline constexpr std::array<") + type + ", " + QByteArray::number(count) + "> " + name + "{{\n" + body + "}};\n";
    };
    QByteArray text = "inline constexpr std::size_t SpriteCount = " + QByteArray::number(spriteCount) + ";\n";
    text += table("Sprite", "Sprites", spriteCount, sprites);
    text += table("Animation", "Animations", animationCount, animations);
    text += frameCount == 0 ? QByteArray("inline constexpr std::array<std::uint16_t, 0> AnimationFrames{};\n")
                            : "inline constexpr std::array<std::uint16_t, " + QByteArray::number(frameCount) + "> AnimationFrames{{" + frames + "}};\n";
    return text;
}

// Solid tiles are flagged per tile index and kept here for collisions added later; nothing reads them yet.
QByteArray tilemapTables(const Scene& scene, const QList<SceneImageData>& images) {
    const auto imageSources = sceneImageSources(scene);
    QByteArray tilemaps, solid;
    int tilemapCount = 0, solidCount = 0;
    for (const auto& item : scene.items) {
        if (item.type != TilemapType) continue;
        const auto& image = *std::ranges::find(images, item.source, &SceneImageData::source);
        tilemaps += "    {" + QByteArray::number(item.columns) + ", " + QByteArray::number(item.rows) + ", " + QByteArray::number(item.tileWidth) + ", " + QByteArray::number(item.tileHeight)
            + ", " + QByteArray::number(item.tileWidth * item.tileScale) + ", " + QByteArray::number(item.tileHeight * item.tileScale)
            + ", " + QByteArray::number(image.width / item.tileWidth) + ", " + QByteArray::number(imageSources.indexOf(item.source))
            + ", " + QByteArray::number(tilemapReservedQuads(item)) + ", " + QByteArray::number(solidCount) + ", " + QByteArray::number(item.solidTiles.size()) + "},\n";
        for (const auto tile : item.solidTiles) solid += (solidCount++ == 0 ? "" : ", ") + QByteArray::number(tile);
        ++tilemapCount;
    }
    QByteArray text = "inline constexpr std::size_t TilemapCount = " + QByteArray::number(tilemapCount) + ";\n";
    text += tilemapCount == 0 ? QByteArray("inline constexpr std::array<Tilemap, 0> Tilemaps{};\n")
                              : "inline constexpr std::array<Tilemap, " + QByteArray::number(tilemapCount) + "> Tilemaps{{\n" + tilemaps + "}};\n";
    text += solidCount == 0 ? QByteArray("inline constexpr std::array<std::uint16_t, 0> SolidTiles{};\n")
                            : "inline constexpr std::array<std::uint16_t, " + QByteArray::number(solidCount) + "> SolidTiles{{" + solid + "}};\n";
    text += "extern const std::array<const std::int32_t*, TilemapCount> TilemapCells; // defined in src/GeneratedImages.cpp, columns * rows cells each\n";
    return text;
}

namespace {

// Every scene's items in one list, in scene order. Generated tables and item indexes are numbered across
// the whole game, so a scene is just a slice of them (see the Scenes table in the generated header).
Scene flattened(const SceneSet& set) {
    Scene flat;
    flat.background = set.scenes.first().background;
    for (const auto& scene : set.scenes) flat.items += scene.items;
    return flat;
}

const Scene& startSceneOf(const SceneSet& set) {
    const auto found = std::ranges::find(set.scenes, set.startScene, &Scene::id);
    return found != set.scenes.end() ? *found : set.scenes.first();
}

QByteArray flatSceneHeader(const Scene& scene, const SceneSet& set, const QList<SceneImageData>& images) {
    if (!imagesMatchScene(scene, images) || !sceneImagesProblem(scene, images).isEmpty()) return {};
    const auto imageSources = sceneImageSources(scene);
    const auto& start = startSceneOf(set);

    QByteArray header = R"cpp(// Generated by AnyDevKit Studio from scene.json. Do not edit.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace GeneratedScene {

enum class Kind : std::uint8_t { Rectangle, Text, Image, Particles, Tilemap };

struct Item {
    Kind kind;
    float x, y, width, height;
    float red, green, blue;
    float textScale;
    std::string_view text;
    bool controllable;
    std::int32_t image = -1; // index into Images for an image item
    std::int32_t particles = -1; // index into ParticleEffects for a particles item
    std::int32_t tilemap = -1; // index into Tilemaps for a tilemap item
    std::int32_t sprite = -1; // index into Sprites for an image that is a sprite sheet
    bool screenFixed = false; // stays at the same place on the screen as the camera moves
    bool pixelated = false; // drawn with nearest-neighbour sampling, on whole pixels
    bool flipped = false; // drawn mirrored left to right when the game starts
    std::int32_t body = -1; // index into Bodies for a moving body
    bool solid = false; // a wall or platform that moving bodies collide with
};

// Row by row, top row first, 4 bytes per pixel: red, green, blue, alpha, with alpha already applied.
struct Image {
    const std::uint8_t* pixels;
    std::uint32_t width, height;
};

// One particles item. preset and palette are the numbers of Particles::Preset and Particles::Palette
// in the runtime. spread, speed and gravity are the percentages of scene.json divided by 100.
struct ParticleEffect {
    std::uint8_t preset, palette;
    std::uint32_t amount, seed;
    float spread, speed, gravity, pixelSize;
    bool looping;
};

// One animation of a sprite. Its frames are AnimationFrames[firstFrame] onward, frameCount of them.
struct Animation {
    std::uint32_t firstFrame, frameCount, framesPerSecond;
};

// A sprite sheet's layout, and which of Animations are its own.
struct Sprite {
    std::uint32_t frameWidth, frameHeight, columns, rows, firstAnimation, animationCount, startAnimation;
};

// A tilemap. Its cells live in TilemapCells; solid tiles are SolidTiles[firstSolid] onward, solidCount of them.
struct Tilemap {
    std::uint32_t columns, rows, tileWidth, tileHeight, cellWidth, cellHeight, tilesetColumns;
    std::int32_t image;
    std::uint32_t reservedQuads, firstSolid, solidCount;
};

// How a moving body behaves. gravityPercent, moveSpeed and jumpHeight are the numbers from scene.json.
struct Body {
    std::int32_t gravityPercent, moveSpeed, jumpHeight;
    bool platformer;
};

// One scene of the game: which of Items are its own, its world and where its camera starts.
struct SceneInfo {
    std::uint32_t firstItem, itemCount;
    float worldWidth, worldHeight, cameraStartX, cameraStartY;
    std::array<float, 3> background;
};

)cpp";
    header += "inline constexpr float WorldWidth = " + floatLiteral(start.worldWidth) + ";\n";
    header += "inline constexpr float WorldHeight = " + floatLiteral(start.worldHeight) + ";\n";
    header += "inline constexpr float CameraStartX = " + floatLiteral(start.cameraX) + ";\n";
    header += "inline constexpr float CameraStartY = " + floatLiteral(start.cameraY) + ";\n";
    header += "inline constexpr std::array<float, 3> Background{" + colorLiterals(start.background) + "};\n";
    header += "inline constexpr std::size_t ImageCount = " + QByteArray::number(images.size()) + ";\n";
    header += "extern const std::array<Image, ImageCount> Images; // defined in src/GeneratedImages.cpp\n";
    int particleEffects = 0;
    qint64 particleCapacity = 0;
    QByteArray particleLines;
    for (const auto& item : scene.items) {
        if (item.type != ParticlesType) continue;
        ++particleEffects;
        particleCapacity += item.amount;
        particleLines += particleEffectLine(item);
    }
    header += "inline constexpr std::size_t ParticleEffectCount = " + QByteArray::number(particleEffects) + ";\n";
    header += "inline constexpr std::size_t ParticleCapacity = " + QByteArray::number(particleCapacity) + "; // the sum of every amount, sized before the first frame\n";
    if (particleEffects == 0) {
        header += "inline constexpr std::array<ParticleEffect, 0> ParticleEffects{};\n";
    } else {
        header += "inline constexpr std::array<ParticleEffect, " + QByteArray::number(particleEffects) + "> ParticleEffects{{\n" + particleLines + "}};\n";
    }
    header += spriteTables(scene, images);
    header += tilemapTables(scene, images);

    int bodyCount = 0;
    int solidCount = 0;
    QByteArray bodies;
    for (const auto& item : scene.items) {
        if (item.physics == SolidPhysics) ++solidCount;
        if (item.physics != BodyPhysics) continue;
        ++bodyCount;
        bodies += "    {" + QByteArray::number(item.gravityScale) + ", " + QByteArray::number(item.moveSpeed) + ", " + QByteArray::number(item.jumpHeight) + (item.platformer ? ", true" : ", false") + "},\n";
    }
    header += "inline constexpr std::size_t BodyCount = " + QByteArray::number(bodyCount) + ";\n";
    header += bodyCount == 0 ? QByteArray("inline constexpr std::array<Body, 0> Bodies{};\n") : "inline constexpr std::array<Body, " + QByteArray::number(bodyCount) + "> Bodies{{\n" + bodies + "}};\n";
    header += "inline constexpr std::size_t SolidItemCount = " + QByteArray::number(solidCount) + ";\n";

    QByteArray sceneLines;
    int firstItem = 0;
    int startIndex = 0;
    for (qsizetype index = 0; index < set.scenes.size(); ++index) {
        const auto& oneScene = set.scenes[index];
        if (oneScene.id == set.startScene) startIndex = static_cast<int>(index);
        sceneLines += "    {" + QByteArray::number(firstItem) + ", " + QByteArray::number(oneScene.items.size()) + ", " + floatLiteral(oneScene.worldWidth) + ", " + floatLiteral(oneScene.worldHeight) + ", " + floatLiteral(oneScene.cameraX) + ", " + floatLiteral(oneScene.cameraY) + ", {" + colorLiterals(oneScene.background) + "}},\n";
        firstItem += static_cast<int>(oneScene.items.size());
    }
    header += "inline constexpr std::size_t SceneCount = " + QByteArray::number(set.scenes.size()) + ";\n";
    header += "inline constexpr std::size_t StartScene = " + QByteArray::number(startIndex) + ";\n";
    header += "inline constexpr std::array<SceneInfo, " + QByteArray::number(set.scenes.size()) + "> Scenes{{\n" + sceneLines + "}};\n";

    if (scene.items.isEmpty()) {
        header += "inline constexpr std::array<Item, 0> Items{};\n";
    } else {
        header += "inline constexpr std::array<Item, " + QByteArray::number(scene.items.size()) + "> Items{{\n";
        int particleIndex = 0;
        int tilemapIndex = 0;
        int spriteIndex = 0;
        int bodyIndex = 0;
        for (const auto& item : scene.items) {
            const auto isSheet = isSpriteSheet(item);
            const auto isBody = item.physics == BodyPhysics;
            header += itemLine(item, imageSources, particleIndex, tilemapIndex, isSheet ? spriteIndex : -1, isBody ? bodyIndex : -1);
            if (item.type == ParticlesType) ++particleIndex;
            if (item.type == TilemapType) ++tilemapIndex;
            if (isSheet) ++spriteIndex;
            if (isBody) ++bodyIndex;
        }
        header += "}};\n";
    }
    header += "\n}\n";
    return header;
}

}

QByteArray generateSceneHeader(const SceneSet& set, const QList<SceneImageData>& images) {
    if (!validateSceneSet(set).isEmpty()) return {};
    return flatSceneHeader(flattened(set), set, images);
}

QByteArray generateSceneHeader(const Scene& scene, const QList<SceneImageData>& images) {
    SceneSet set;
    set.scenes = {scene};
    set.startScene = scene.id;
    return generateSceneHeader(set, images);
}

bool writeGeneratedScene(const QString& projectRoot, const Scene& scene, const QList<SceneImageData>& images, QString& error) {
    SceneSet set;
    set.scenes = {scene};
    set.startScene = scene.id;
    return writeGeneratedScene(projectRoot, set, images, error);
}

bool writeGeneratedScene(const QString& projectRoot, const SceneSet& set, const QList<SceneImageData>& images, QString& error) {
    error.clear();
    const auto scene = validateSceneSet(set).isEmpty() ? flattened(set) : Scene{};
    if (const auto problem = sceneImagesProblem(scene, images); !problem.isEmpty()) {
        error = problem;
        return false;
    }
    const auto header = generateSceneHeader(set, images);
    if (header.isEmpty()) {
        error = "The scene could not be exported because it has a problem or its images do not match. Check the scene and try again.";
        return false;
    }
    const auto root = QDir::cleanPath(QDir(projectRoot).absolutePath());

    QList<SceneItem> tilemaps;
    for (const auto& item : scene.items) {
        if (item.type == TilemapType) tilemaps.append(item);
    }

    QList<RuntimeFile> files{{GeneratedHeaderPath, header}, {GeneratedImageSourcePath, generateImageSource(images, tilemaps.size())}};
    QSet<QString> keptImages;
    QSet<QString> keptTiles;
    for (qsizetype index = 0; index < tilemaps.size(); ++index) {
        const auto name = QString("tilemap-%1.tiles").arg(index);
        files.append({QString(GeneratedImageFolder) + '/' + name, tilemapCellBytes(tilemaps[index])});
        keptTiles.insert(name);
    }
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
    for (const auto& name : imageFolder.entryList({TileFilePattern}, QDir::Files)) {
        if (!keptTiles.contains(name)) QFile::remove(imageFolder.filePath(name));
    }
    return true;
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

Scene appStarterScene() {
    const auto textItem = [](const QString& id, const QString& name, double x, double y, int scale, const QString& text, const QString& color) {
        SceneItem item;
        item.id = id;
        item.type = TextType;
        item.name = name;
        item.x = x;
        item.y = y;
        item.scale = scale;
        item.text = text;
        item.color = color;
        return item;
    };
    const auto rectangle = [](const QString& id, const QString& name, double x, double y, double width, double height, const QString& color) {
        SceneItem item;
        item.id = id;
        item.type = RectangleType;
        item.name = name;
        item.x = x;
        item.y = y;
        item.width = width;
        item.height = height;
        item.color = color;
        return item;
    };

    constexpr double StageWidth = 1920;
    constexpr double HeaderHeight = 200;
    constexpr double ButtonWidth = 560;
    constexpr double ButtonHeight = 120;
    constexpr double ButtonGap = 28;
    constexpr double ButtonsTop = 432;
    constexpr int LabelScale = 6;
    constexpr int TitleScale = 10;

    Scene scene;
    scene.items << rectangle("item-1", "Header", 0, 0, StageWidth, HeaderHeight, "#1C2549");
    scene.items << textItem("item-2", "Title", 128, (HeaderHeight - 7 * TitleScale) / 2, TitleScale, "Home", "#F3F1E2");

    struct Button { QString name; QString fill; QString ink; };
    const QList<Button> buttons{{"Start", "#FFD23F", "#151C3A"}, {"Settings", "#2A3463", "#F3F1E2"}, {"About", "#2A3463", "#F3F1E2"}};
    for (qsizetype index = 0; index < buttons.size(); ++index) {
        const auto& button = buttons[index];
        const double y = ButtonsTop + index * (ButtonHeight + ButtonGap);
        const double x = (StageWidth - ButtonWidth) / 2;
        // Each character is a 5x7 cell that advances 6, so the label's drawn size is known exactly.
        const double labelWidth = (button.name.size() * 6 - 1) * LabelScale;
        const double labelHeight = 7 * LabelScale;
        scene.items << rectangle(QString("item-%1").arg(3 + index * 2), button.name + " button", x, y, ButtonWidth, ButtonHeight, button.fill);
        scene.items << textItem(QString("item-%1").arg(4 + index * 2), button.name + " label", x + (ButtonWidth - labelWidth) / 2, y + (ButtonHeight - labelHeight) / 2, LabelScale, button.name, button.ink);
    }
    return scene;
}

bool sceneRuntimeNeedsUpdate(const QString& projectRoot) {
    QFile file(QDir::cleanPath(QDir(projectRoot).absolutePath()) + "/include/Scene/SceneState.hpp");
    if (!file.open(QIODevice::ReadOnly)) return false;
    return !file.readAll().contains("GeneratedVariables.hpp");
}

bool sceneRuntimeNeedsUpdate(const QString& projectRoot, const Scene& scene) {
    const auto usesWorld = scene.worldWidth != ScreenWidthUnits || scene.worldHeight != ScreenHeightUnits || scene.cameraX != 0 || scene.cameraY != 0;
    const auto usesWorldItem = std::ranges::any_of(scene.items, [](const SceneItem& item) {
        return item.type == TilemapType || item.frameWidth != 0 || item.frameHeight != 0 || !item.animations.isEmpty() || item.pixelated || item.flipX || item.screenFixed;
    });
    return (usesWorld || usesWorldItem) && sceneRuntimeLacksWorld(projectRoot);
}

bool sceneRuntimeLacksPhysics(const QString& projectRoot) {
    QFile file(QDir::cleanPath(QDir(projectRoot).absolutePath()) + "/include/Scene/SceneState.hpp");
    if (!file.open(QIODevice::ReadOnly)) return false;
    return !file.readAll().contains("Scene/Physics2D.hpp");
}

bool sceneSetUsesPhysics(const SceneSet& set) {
    if (set.scenes.size() > 1) return true;
    for (const auto& scene : set.scenes) {
        if (std::ranges::any_of(scene.items, [](const SceneItem& item) { return !item.physics.isEmpty(); })) return true;
    }
    return false;
}

bool sceneRuntimeLacksWorld(const QString& projectRoot) {
    QFile file(QDir::cleanPath(QDir(projectRoot).absolutePath()) + "/include/Scene/SceneState.hpp");
    if (!file.open(QIODevice::ReadOnly)) return false;
    return !file.readAll().contains("Scene/World2D.hpp");
}

bool installSceneRuntime(const QString& runtimeDirectory, const QString& projectRoot, const QString& projectName, QString& error, const Scene& starter) {
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
        if (!saveScene(scenePath, starter, problem)) return fail(problem + recovery);
    }
    return true;
}

}
