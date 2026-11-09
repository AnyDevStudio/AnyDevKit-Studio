#include "SceneDocument.h"

#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSize>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <utility>

namespace Studio {

namespace {

constexpr double StageWidth = ScreenWidthUnits;
constexpr double StageHeight = ScreenHeightUnits;
constexpr int DefaultFps = 8;
constexpr int NewTileSize = 16;
constexpr int NewTileScale = 4;
constexpr double MinimumSize = 8;
constexpr int MaximumItems = 256;
constexpr int MaximumUndoSteps = 200;
constexpr double DuplicateOffset = 32;
constexpr qsizetype MaximumNameLength = 64; // the Name field's limit, so a copy's name stays editable
// A new picture never covers more than this, so the rest of the scene stays reachable.
constexpr double LargestNewImageWidth = StageWidth / 2;
constexpr double LargestNewImageHeight = StageHeight / 2;

double drawnHeight(const SceneItem& item) {
    return item.type == "text" ? 7.0 * item.scale : item.height;
}

// One step along, or one step back where going on would leave the stage, so a copy never lands
// exactly on top of an original that sits against an edge.
double offsetForCopy(double position, double size, double stageSize) {
    const auto stepped = position + DuplicateOffset;
    return stepped + size <= stageSize ? stepped : position - DuplicateOffset;
}

// "Coin", "Coin copy" and "Coin copy 7" all count up from "Coin", so copies do not stack up words.
QString copyName(const SceneItem& source, const QList<SceneItem>& items) {
    static const QRegularExpression existingCopy(" copy(?: (\\d+))?$");
    auto base = source.name.isEmpty() ? source.id : source.name;
    int number = 1;
    if (const auto match = existingCopy.match(base); match.hasMatch()) {
        number = match.captured(1).isEmpty() ? 2 : match.captured(1).toInt() + 1;
        base.chop(match.capturedLength());
    }
    for (;; ++number) {
        const auto ending = number == 1 ? QString(" copy") : QString(" copy %1").arg(number);
        const auto name = base.left(MaximumNameLength - ending.size()) + ending;
        if (std::ranges::none_of(items, [&](const SceneItem& item) { return item.name == name; })) return name;
    }
}

}

SceneDocument::SceneDocument(QObject* parent) : QAbstractListModel(parent) {}

double SceneDocument::drawnWidth(const SceneItem& item) const {
    return item.type == "text" ? static_cast<double>(displayedText(item.text, variables).size() * 6 - 1) * item.scale : item.width;
}

int SceneDocument::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(document.items.size());
}

QVariant SceneDocument::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || !isValidIndex(index.row())) return {};
    const auto& item = document.items[index.row()];
    switch (role) {
    case IdRole: return item.id;
    case TypeRole: return item.type;
    case NameRole: return item.name;
    case XRole: return item.x;
    case YRole: return item.y;
    case WidthRole: return drawnWidth(item);
    case HeightRole: return drawnHeight(item);
    case ColorRole: return item.color;
    case TextRole: return displayedText(item.text, variables);
    case ScaleRole: return item.scale;
    case ControllableRole: return item.controllable;
    case ParticlesRole: return QVariantMap{
        {"preset", item.preset},
        {"palette", item.palette},
        {"color", item.color},
        {"amount", item.amount},
        {"spread", item.spread},
        {"speed", item.speed},
        {"gravity", item.gravity},
        {"pixelSize", item.pixelSize},
        {"looping", item.looping},
        {"id", item.id},
    };
    case SourceRole: return item.source;
    case ImageUrlRole: return (item.type == "image" || item.type == "tilemap") && !imageProblems.contains(item.source) ? QUrl::fromLocalFile(projectRoot() + '/' + item.source) : QUrl();
    case ImageProblemRole: return imageProblems.value(item.source);
    case HiddenRole: return hiddenItems.contains(item.id);
    case ScreenFixedRole: return item.screenFixed;
    case SpriteRole: {
        QVariantList frames;
        int fps = DefaultFps;
        if (!item.animations.isEmpty()) {
            const auto chosen = std::ranges::find(item.animations, item.animation, &SceneAnimation::name);
            const auto& animation = chosen == item.animations.end() ? item.animations.first() : *chosen;
            for (const auto frame : animation.frames) frames.append(frame);
            fps = animation.fps;
        }
        return QVariantMap{{"frameWidth", item.frameWidth}, {"frameHeight", item.frameHeight}, {"flipX", item.flipX}, {"pixelated", item.pixelated}, {"frames", frames}, {"fps", fps}};
    }
    case TilemapRole: return QVariantMap{{"tileWidth", item.tileWidth}, {"tileHeight", item.tileHeight}, {"tileScale", item.tileScale}, {"columns", item.columns}, {"rows", item.rows}};
    default: return {};
    }
}

QHash<int, QByteArray> SceneDocument::roleNames() const {
    return {
        {IdRole, "itemId"},
        {TypeRole, "itemType"},
        {NameRole, "itemName"},
        {XRole, "itemX"},
        {YRole, "itemY"},
        {WidthRole, "itemWidth"},
        {HeightRole, "itemHeight"},
        {ColorRole, "itemColor"},
        {TextRole, "itemText"},
        {ScaleRole, "itemScale"},
        {ControllableRole, "itemControllable"},
        {ParticlesRole, "itemParticles"},
        {SourceRole, "itemSource"},
        {ImageUrlRole, "itemImageUrl"},
        {ImageProblemRole, "itemImageProblem"},
        {HiddenRole, "itemHidden"},
        {ScreenFixedRole, "itemScreenFixed"},
        {SpriteRole, "itemSprite"},
        {TilemapRole, "itemTilemap"},
    };
}

bool SceneDocument::open(const QString& newPath) {
    close();
    if (!QFile::exists(newPath)) return false;
    const auto result = loadScene(newPath);
    if (!result.scene) {
        setError(result.error);
        return false;
    }
    beginResetModel();
    path = newPath;
    document = *result.scene;
    loaded = true;
    refreshImageProblems();
    endResetModel();
    setError({});
    emit loadedChanged();
    emit countChanged();
    emit backgroundChanged();
    emit worldChanged();
    return true;
}

void SceneDocument::close() {
    beginResetModel();
    path.clear();
    document = {};
    history.clear();
    loaded = false;
    changing = false;
    changedDuringChange = false;
    selection = -1;
    imageProblems.clear();
    hiddenItems.clear();
    endResetModel();
    setError({});
    setNotice({}, false);
    emit loadedChanged();
    emit countChanged();
    emit backgroundChanged();
    emit selectionChanged();
    emit undoChanged();
    emit imagesChanged();
    emit worldChanged();
}

void SceneDocument::setError(const QString& text) {
    if (text == message) return;
    message = text;
    emit errorChanged();
}

const Scene& SceneDocument::scene() const { return document; }
bool SceneDocument::isLoaded() const { return loaded; }
int SceneDocument::count() const { return rowCount(); }
QString SceneDocument::background() const { return document.background; }
int SceneDocument::selectedIndex() const { return selection; }
bool SceneDocument::canUndo() const { return !history.isEmpty(); }
QString SceneDocument::error() const { return message; }
QString SceneDocument::notice() const { return noticeText; }
bool SceneDocument::noticeIsProblem() const { return noticeProblem; }
QStringList SceneDocument::variableNames() const {
    QStringList names;
    for (const auto& variable : variables) names << variable.name;
    return names;
}

int SceneDocument::worldWidth() const { return document.worldWidth; }
int SceneDocument::worldHeight() const { return document.worldHeight; }
double SceneDocument::cameraX() const { return document.cameraX; }
double SceneDocument::cameraY() const { return document.cameraY; }

QUrl SceneDocument::selectedTilesetUrl() const {
    if (!isValidIndex(selection)) return {};
    const auto& item = document.items[selection];
    if ((item.type != "tilemap") || imageProblems.contains(item.source)) return {};
    return QUrl::fromLocalFile(projectRoot() + '/' + item.source);
}

QString SceneDocument::projectRoot() const { return path.isEmpty() ? QString() : QFileInfo(path).absolutePath(); }

QString SceneDocument::imageWarning() const {
    if (imageProblems.isEmpty()) return {};
    if (imageProblems.size() == 1) return imageProblems.cbegin().value();
    auto sources = imageProblems.keys();
    sources.sort();
    return QString("%1 images cannot be shown: %2. Put the files back in the project's assets folder, or select each image and choose Replace image.").arg(sources.size()).arg(sources.join(", "));
}

QString SceneDocument::roomProblem() const {
    return document.items.size() >= MaximumItems ? QString("A scene can hold at most %1 items.").arg(MaximumItems) : QString();
}

void SceneDocument::setNotice(const QString& text, bool problem) {
    if (text == noticeText && problem == noticeProblem) return;
    noticeText = text;
    noticeProblem = problem;
    emit noticeChanged();
}

void SceneDocument::setSelectedIndex(int index) {
    const auto next = isValidIndex(index) ? index : -1;
    if (next == selection) return;
    selection = next;
    setNotice({}, false);
    emit selectionChanged();
}

QVariantMap SceneDocument::selected() const {
    if (!isValidIndex(selection)) return {};
    const auto& item = document.items[selection];
    QVariantList animations;
    for (const auto& animation : item.animations) {
        QStringList frames;
        for (const auto frame : animation.frames) frames << QString::number(frame);
        animations.append(QVariantMap{{"name", animation.name}, {"frames", frames.join(", ")}, {"fps", animation.fps}});
    }
    return {
        {"screenFixed", item.screenFixed},
        {"pixelated", item.pixelated},
        {"flipX", item.flipX},
        {"frameWidth", item.frameWidth},
        {"frameHeight", item.frameHeight},
        {"animation", item.animation},
        {"animations", animations},
        {"tileWidth", item.tileWidth},
        {"tileHeight", item.tileHeight},
        {"tileScale", item.tileScale},
        {"columns", item.columns},
        {"rows", item.rows},
        {"type", item.type},
        {"name", item.name},
        {"x", item.x},
        {"y", item.y},
        {"width", item.width},
        {"height", item.height},
        {"color", item.color},
        {"text", item.text},
        {"scale", item.scale},
        {"controllable", item.controllable},
        {"preset", item.preset},
        {"palette", item.palette},
        {"amount", item.amount},
        {"spread", item.spread},
        {"speed", item.speed},
        {"gravity", item.gravity},
        {"pixelSize", item.pixelSize},
        {"looping", item.looping},
        {"source", item.source},
        {"imageProblem", imageProblems.value(item.source)},
    };
}

bool SceneDocument::isValidIndex(int index) const {
    return index >= 0 && index < document.items.size();
}

bool SceneDocument::isTilemap(int index) const {
    return isValidIndex(index) && document.items[index].type == "tilemap";
}

QSize SceneDocument::itemBounds(const SceneItem& item) const {
    return item.screenFixed ? QSize(ScreenWidthUnits, ScreenHeightUnits) : QSize(document.worldWidth, document.worldHeight);
}

void SceneDocument::pushUndo() {
    history.append(document);
    if (history.size() > MaximumUndoSteps) history.removeFirst();
    emit undoChanged();
}

bool SceneDocument::commit(const Scene& candidate) {
    const auto problem = validateScene(candidate);
    if (!problem.isEmpty()) {
        setError(problem);
        return false;
    }
    setNotice({}, false);
    pushUndo();
    document = candidate;
    refreshImageProblems();
    QString failure;
    setError(saveScene(path, document, failure) ? QString() : failure);
    return true;
}

void SceneDocument::notifyItem(int index) {
    const auto changed = this->index(index);
    emit dataChanged(changed, changed);
    if (index == selection) emit selectionChanged();
}

void SceneDocument::placeInsideStage(SceneItem& item) const {
    const auto bounds = itemBounds(item);
    item.x = std::round(std::clamp(item.x, 0.0, std::max(0.0, double(bounds.width()) - drawnWidth(item))));
    item.y = std::round(std::clamp(item.y, 0.0, std::max(0.0, double(bounds.height()) - drawnHeight(item))));
}

int SceneDocument::indexOfItem(const QString& id) const {
    if (id.isEmpty()) return -1;
    for (int row = 0; row < document.items.size(); ++row) {
        if (document.items[row].id == id) return row;
    }
    return -1;
}

QString SceneDocument::nextItemId() const {
    for (int number = 1;; ++number) {
        const auto id = QString("item-%1").arg(number);
        // A hidden id may come back through undo, so a new item must not pick up its hidden state.
        if (!hiddenItems.contains(id) && std::ranges::none_of(document.items, [&](const SceneItem& item) { return item.id == id; })) return id;
    }
}

bool SceneDocument::refreshImageProblems() {
    QHash<QString, QString> found;
    const auto root = projectRoot();
    for (const auto& source : sceneImageSources(document)) {
        const auto problem = imageFileProblem(root, source);
        if (!problem.isEmpty()) found.insert(source, problem);
    }
    if (found == imageProblems) return false;
    imageProblems = found;
    emit imagesChanged();
    return true;
}

void SceneDocument::clearNotice() { setNotice({}, false); }

void SceneDocument::recheckImages() {
    if (!loaded) return;
    const auto before = imageProblems;
    if (!refreshImageProblems()) return;
    for (int row = 0; row < document.items.size(); ++row) {
        const auto& source = document.items[row].source;
        if (before.value(source) != imageProblems.value(source)) notifyItem(row);
    }
}

int SceneDocument::addItem(const QString& type, double centerX, double centerY) {
    if (!loaded || changing) return -1;
    if (const auto problem = roomProblem(); !problem.isEmpty()) {
        setError(problem);
        return -1;
    }

    SceneItem item;
    item.id = nextItemId();
    item.type = type;
    const auto number = item.id.mid(5);
    if (type == "text") {
        item.name = "Text " + number;
        item.text = "Text";
        item.color = "#F3F1E2";
    } else if (type == "particles") {
        item.name = "Particles " + number;
        applyParticlePreset(item, "fireworks");
    } else {
        item.name = "Shape " + number;
        item.width = 200;
        item.height = 200;
    }
    item.x = centerX - drawnWidth(item) / 2;
    item.y = centerY - drawnHeight(item) / 2;
    return insertItem(item);
}

int SceneDocument::addImage(const QString& source, int pixelWidth, int pixelHeight, const QString& name, double centerX, double centerY) {
    if (!loaded || changing || pixelWidth < 1 || pixelHeight < 1) return -1;
    if (const auto problem = roomProblem(); !problem.isEmpty()) {
        setError(problem);
        return -1;
    }

    SceneItem item;
    item.id = nextItemId();
    item.type = "image";
    item.name = name;
    item.source = source;
    const auto scale = std::min({1.0, LargestNewImageWidth / pixelWidth, LargestNewImageHeight / pixelHeight});
    item.width = std::max(MinimumSize, std::round(pixelWidth * scale));
    item.height = std::max(MinimumSize, std::round(pixelHeight * scale));
    item.x = centerX - item.width / 2;
    item.y = centerY - item.height / 2;
    return insertItem(item);
}

bool SceneDocument::replaceImage(int index, const QString& source, int pixelWidth, int pixelHeight) {
    if (!isValidIndex(index) || changing || pixelWidth < 1 || pixelHeight < 1 || document.items[index].type != "image") return false;
    auto item = document.items[index];
    const auto centerX = item.x + item.width / 2;
    const auto centerY = item.y + item.height / 2;
    const auto newAspect = static_cast<double>(pixelWidth) / pixelHeight;
    if (std::abs(item.width / item.height / newAspect - 1) > 0.005) {
        // The same area under the new shape, so swapping pictures back and forth does not shrink them.
        auto height = std::sqrt(item.width * item.height / newAspect);
        auto width = height * newAspect;
        const auto fit = std::min({1.0, StageWidth / width, StageHeight / height});
        item.width = std::max(MinimumSize, std::round(width * fit));
        item.height = std::max(MinimumSize, std::round(height * fit));
    }
    item.source = source;
    item.x = centerX - item.width / 2;
    item.y = centerY - item.height / 2;
    placeInsideStage(item);

    auto candidate = document;
    candidate.items[index] = item;
    if (!commit(candidate)) return false;
    notifyItem(index);
    return true;
}

int SceneDocument::addTilemap(const QString& source, int pixelWidth, int pixelHeight, const QString& name, double centerX, double centerY) {
    Q_UNUSED(centerX)
    Q_UNUSED(centerY)
    if (!loaded || changing) return -1;
    if (pixelWidth < NewTileSize || pixelHeight < NewTileSize) {
        setError(QString("This picture is %1x%2 pixels, but a tileset needs at least one %3x%3 tile. Choose a larger picture.").arg(pixelWidth).arg(pixelHeight).arg(NewTileSize));
        return -1;
    }
    if (const auto problem = roomProblem(); !problem.isEmpty()) {
        setError(problem);
        return -1;
    }

    SceneItem item;
    item.id = nextItemId();
    item.type = "tilemap";
    item.name = name;
    item.source = source;
    item.tileWidth = NewTileSize;
    item.tileHeight = NewTileSize;
    item.tileScale = NewTileScale;
    const auto cell = NewTileSize * NewTileScale;
    item.columns = std::min(static_cast<int>(std::ceil(double(document.worldWidth) / cell)), MaximumTilemapSide);
    item.rows = std::min(static_cast<int>(std::ceil(double(document.worldHeight) / cell)), MaximumTilemapSide);
    item.tiles = QList<int>(qint64(item.columns) * item.rows, -1);
    updateTilemapSize(item);
    return insertItem(item);
}

bool SceneDocument::commitItem(int index, const SceneItem& item) {
    auto candidate = document;
    candidate.items[index] = item;
    if (const auto problem = validateScene(candidate); !problem.isEmpty()) {
        setError(problem);
        return false;
    }
    auto placed = item;
    placeInsideStage(placed);
    candidate.items[index] = placed;
    if (!commit(candidate)) return false;
    notifyItem(index);
    return true;
}

bool SceneDocument::changeTilemapCell(SceneItem& item, int column, int row, int tile) const {
    const auto cell = row * item.columns + column;
    if (item.tiles[cell] == tile) return false;
    item.tiles[cell] = tile;
    return true;
}

int SceneDocument::tileAt(int index, int column, int row) const {
    if (!isTilemap(index)) return -1;
    const auto& item = document.items[index];
    if (column < 0 || column >= item.columns || row < 0 || row >= item.rows) return -1;
    return item.tiles.value(row * item.columns + column, -1);
}

bool SceneDocument::paintTile(int index, int column, int row, int tile) {
    if (!isTilemap(index) || tile < -1 || tile > MaximumTileIndex) return false;
    const auto& current = document.items[index];
    if (column < 0 || column >= current.columns || row < 0 || row >= current.rows) return false;
    if (current.tiles[row * current.columns + column] == tile) return true;
    if (changing) {
        changeTilemapCell(document.items[index], column, row, tile);
        changedDuringChange = true;
        notifyItem(index);
        return true;
    }
    auto item = current;
    changeTilemapCell(item, column, row, tile);
    return commitItem(index, item);
}

int SceneDocument::fillTiles(int index, int column, int row, int tile) {
    if (!isTilemap(index) || changing || tile < -1 || tile > MaximumTileIndex) return 0;
    auto item = document.items[index];
    if (column < 0 || column >= item.columns || row < 0 || row >= item.rows) return 0;
    const auto target = item.tiles[row * item.columns + column];
    if (target == tile) return 0;

    int painted = 0;
    QList<std::pair<int, int>> pending{{column, row}};
    while (!pending.isEmpty()) {
        const auto [x, y] = pending.takeLast();
        const auto cell = y * item.columns + x;
        if (item.tiles[cell] != target) continue;
        item.tiles[cell] = tile;
        ++painted;
        if (x > 0) pending.append({x - 1, y});
        if (x + 1 < item.columns) pending.append({x + 1, y});
        if (y > 0) pending.append({x, y - 1});
        if (y + 1 < item.rows) pending.append({x, y + 1});
    }
    return commitItem(index, item) ? painted : 0;
}

bool SceneDocument::setTileSolid(int index, int tile, bool solid) {
    if (!isTilemap(index) || tile < 0 || tile > MaximumTileIndex) return false;
    auto item = document.items[index];
    const auto position = std::lower_bound(item.solidTiles.begin(), item.solidTiles.end(), tile);
    const auto present = position != item.solidTiles.end() && *position == tile;
    if (present == solid) return true;
    if (solid) item.solidTiles.insert(position, tile);
    else item.solidTiles.erase(position);
    return commitItem(index, item);
}

bool SceneDocument::isTileSolid(int index, int tile) const {
    return isTilemap(index) && document.items[index].solidTiles.contains(tile);
}

QVariantList SceneDocument::solidTiles(int index) const {
    QVariantList list;
    if (isTilemap(index)) {
        for (const auto tile : document.items[index].solidTiles) list.append(tile);
    }
    return list;
}

bool SceneDocument::addAnimation(int index, const QString& name) {
    if (!isValidIndex(index) || changing) return false;
    auto item = document.items[index];
    if (!isSpriteSheet(item)) {
        setError("Set a frame width and frame height on this image first, so it has frames to animate.");
        return false;
    }
    item.animations.append({name, {0}, DefaultFps});
    if (item.animation.isEmpty()) item.animation = name;
    return commitItem(index, item);
}

bool SceneDocument::removeAnimation(int index, const QString& name) {
    if (!isValidIndex(index) || changing) return false;
    auto item = document.items[index];
    const auto found = std::ranges::find(item.animations, name, &SceneAnimation::name);
    if (found == item.animations.end()) return false;
    item.animations.erase(found);
    if (item.animation == name) item.animation = item.animations.isEmpty() ? QString() : item.animations.first().name;
    return commitItem(index, item);
}

bool SceneDocument::setAnimationFrames(int index, const QString& name, const QVariantList& frames) {
    if (!isValidIndex(index) || changing) return false;
    auto item = document.items[index];
    const auto found = std::ranges::find(item.animations, name, &SceneAnimation::name);
    if (found == item.animations.end()) return false;
    QList<int> list;
    for (const auto& frame : frames) {
        bool isNumber = false;
        const auto number = frame.toDouble(&isNumber);
        if (!isNumber || number != std::floor(number) || !std::isfinite(number)) {
            setError(QString("Frames must be whole numbers, like 0, 1, 2. \"%1\" is not.").arg(frame.toString()));
            return false;
        }
        list.append(static_cast<int>(std::clamp(number, -1.0, 1e8)));
    }
    found->frames = list;
    return commitItem(index, item);
}

bool SceneDocument::setAnimationFps(int index, const QString& name, int fps) {
    if (!isValidIndex(index) || changing) return false;
    auto item = document.items[index];
    const auto found = std::ranges::find(item.animations, name, &SceneAnimation::name);
    if (found == item.animations.end()) return false;
    found->fps = fps;
    return commitItem(index, item);
}

bool SceneDocument::setWorldSize(int width, int height) {
    if (!loaded || changing) return false;
    auto candidate = document;
    candidate.worldWidth = width;
    candidate.worldHeight = height;
    candidate.cameraX = std::clamp(document.cameraX, 0.0, std::max(0.0, double(width) - ScreenWidthUnits));
    candidate.cameraY = std::clamp(document.cameraY, 0.0, std::max(0.0, double(height) - ScreenHeightUnits));
    if (const auto problem = validateScene(candidate); !problem.isEmpty()) {
        setError(problem);
        return false;
    }
    const auto before = document.items;
    for (auto& item : candidate.items) {
        if (item.screenFixed) continue;
        item.x = std::round(std::clamp(item.x, 0.0, std::max(0.0, double(width) - drawnWidth(item))));
        item.y = std::round(std::clamp(item.y, 0.0, std::max(0.0, double(height) - drawnHeight(item))));
    }
    if (!commit(candidate)) return false;
    for (int row = 0; row < document.items.size(); ++row) {
        if (document.items[row].x != before[row].x || document.items[row].y != before[row].y) notifyItem(row);
    }
    emit worldChanged();
    return true;
}

void SceneDocument::setCameraStart(double x, double y) {
    if (!loaded || !std::isfinite(x) || !std::isfinite(y)) return;
    const auto clampedX = std::clamp(x, 0.0, std::max(0.0, double(document.worldWidth) - ScreenWidthUnits));
    const auto clampedY = std::clamp(y, 0.0, std::max(0.0, double(document.worldHeight) - ScreenHeightUnits));
    if (clampedX == document.cameraX && clampedY == document.cameraY) return;
    if (changing) {
        document.cameraX = clampedX;
        document.cameraY = clampedY;
        changedDuringChange = true;
        emit worldChanged();
        return;
    }
    auto candidate = document;
    candidate.cameraX = clampedX;
    candidate.cameraY = clampedY;
    if (commit(candidate)) emit worldChanged();
}

int SceneDocument::insertItem(SceneItem item) {
    placeInsideStage(item);
    auto candidate = document;
    candidate.items.append(item);
    const auto row = static_cast<int>(document.items.size());
    const auto problem = validateScene(candidate);
    if (!problem.isEmpty()) {
        setError(problem);
        return -1;
    }
    beginInsertRows({}, row, row);
    commit(candidate);
    endInsertRows();
    emit countChanged();
    selection = row;
    emit selectionChanged();
    return row;
}

void SceneDocument::beginChange() {
    if (!loaded || changing) return;
    setNotice({}, false);
    changing = true;
    changedDuringChange = false;
    pushUndo();
}

void SceneDocument::endChange() {
    if (!changing) return;
    changing = false;
    if (!changedDuringChange) {
        history.removeLast();
        emit undoChanged();
        return;
    }
    QString failure;
    setError(saveScene(path, document, failure) ? QString() : failure);
}

void SceneDocument::moveItem(int index, double x, double y) {
    if (!isValidIndex(index)) return;
    auto item = document.items[index];
    item.x = x;
    item.y = y;
    placeInsideStage(item);
    if (changing) {
        document.items[index] = item;
        changedDuringChange = true;
    } else {
        auto candidate = document;
        candidate.items[index] = item;
        if (!commit(candidate)) return;
    }
    notifyItem(index);
}

void SceneDocument::nudgeItem(int index, double deltaX, double deltaY) {
    if (isValidIndex(index)) moveItem(index, document.items[index].x + deltaX, document.items[index].y + deltaY);
}

void SceneDocument::resizeItem(int index, double width, double height, bool keepAspect) {
    if (!isValidIndex(index)) return;
    auto item = document.items[index];
    if (item.type != "rectangle" && item.type != "image" && item.type != "particles") return;
    if (keepAspect) {
        // During a drag the shape the drag started with, since the current size is already rounded.
        const auto& start = changing && index < history.last().items.size() ? history.last().items[index] : item;
        const auto bounds = itemBounds(item);
        const auto largest = std::min((bounds.width() - item.x) / start.width, (bounds.height() - item.y) / start.height);
        const auto smallest = std::max(MinimumSize / start.width, MinimumSize / start.height);
        const auto scale = std::clamp(std::max(width / start.width, height / start.height), smallest, std::max(smallest, largest));
        item.width = std::round(start.width * scale);
        item.height = std::round(start.height * scale);
    } else {
        const auto bounds = itemBounds(item);
        item.width = std::round(std::clamp(width, MinimumSize, std::max(MinimumSize, bounds.width() - item.x)));
        item.height = std::round(std::clamp(height, MinimumSize, std::max(MinimumSize, bounds.height() - item.y)));
    }
    if (changing) {
        document.items[index] = item;
        changedDuringChange = true;
    } else {
        auto candidate = document;
        candidate.items[index] = item;
        if (!commit(candidate)) return;
    }
    notifyItem(index);
}

bool SceneDocument::setItemValue(int index, const QString& property, const QVariant& value) {
    if (!isValidIndex(index) || changing) return false;
    auto item = document.items[index];

    static const QStringList sheetSettings{"frameWidth", "frameHeight", "animation"};
    static const QStringList tilemapSettings{"tileWidth", "tileHeight", "tileScale", "columns", "rows"};
    if ((sheetSettings.contains(property) || property == "flipX" || property == "pixelated") && item.type != "image") return false;
    if (tilemapSettings.contains(property) && item.type != "tilemap") return false;

    if (sheetSettings.contains(property) || tilemapSettings.contains(property)) {
        if (property == "animation") {
            item.animation = value.toString();
        } else {
            bool isNumber = false;
            const auto number = value.toDouble(&isNumber);
            if (!isNumber || !std::isfinite(number) || number != std::floor(number)) {
                setError(QString("%1 must be a whole number.").arg(property));
                return false;
            }
            const auto whole = static_cast<int>(std::clamp(number, -1e8, 1e8));
            if (property == "frameWidth" || property == "frameHeight") {
                // A square sheet is the usual case, and a sheet of frames is almost always pixel art.
                if (property == "frameWidth") item.frameWidth = whole;
                else item.frameHeight = whole;
                if (whole == 0) {
                    item.frameWidth = item.frameHeight = 0;
                    item.animations.clear();
                    item.animation.clear();
                } else {
                    if (item.frameWidth == 0) item.frameWidth = whole;
                    if (item.frameHeight == 0) item.frameHeight = whole;
                    item.pixelated = true;
                }
            } else if (property == "tileWidth" || property == "tileHeight" || property == "tileScale") {
                (property == "tileWidth" ? item.tileWidth : property == "tileHeight" ? item.tileHeight : item.tileScale) = whole;
                updateTilemapSize(item);
            } else {
                resizeTilemap(item, property == "columns" ? whole : item.columns, property == "rows" ? whole : item.rows);
            }
        }
        return commitItem(index, item);
    }
    if (property == "flipX" || property == "pixelated" || property == "screenFixed") {
        (property == "flipX" ? item.flipX : property == "pixelated" ? item.pixelated : item.screenFixed) = value.toBool();
        return commitItem(index, item);
    }

    static const QStringList particleSettings{"preset", "palette", "looping", "amount", "spread", "speed", "gravity", "pixelSize"};
    if (particleSettings.contains(property) && item.type != "particles") return false;

    if (property == "preset") {
        // A preset brings a whole look with it. The area is the preset's own, centered where the item was.
        const auto centerX = item.x + item.width / 2;
        const auto centerY = item.y + item.height / 2;
        applyParticlePreset(item, value.toString());
        item.x = centerX - item.width / 2;
        item.y = centerY - item.height / 2;
    } else if (property == "palette") item.palette = value.toString();
    else if (property == "looping") item.looping = value.toBool();
    else if (property == "amount" || property == "spread" || property == "speed" || property == "gravity" || property == "pixelSize") {
        bool isNumber = false;
        const auto number = value.toDouble(&isNumber);
        if (!isNumber || !std::isfinite(number) || number != std::floor(number)) {
            setError(QString("%1 must be a whole number.").arg(property));
            return false;
        }
        const auto whole = static_cast<int>(std::clamp(number, -1e8, 1e8));
        if (property == "amount") item.amount = whole;
        else if (property == "spread") item.spread = whole;
        else if (property == "speed") item.speed = whole;
        else if (property == "gravity") item.gravity = whole;
        else item.pixelSize = whole;
    } else if (property == "name") item.name = value.toString();
    else if (property == "text") item.text = value.toString();
    else if (property == "color") item.color = value.toString();
    else if (property == "controllable") item.controllable = value.toBool();
    else if (property == "x" || property == "y" || property == "width" || property == "height" || property == "scale") {
        const auto number = value.toDouble();
        if (!std::isfinite(number)) {
            setError(QString("%1 must be a number.").arg(property));
            return false;
        }
        if (property == "x") item.x = number;
        else if (property == "y") item.y = number;
        else if (property == "width") item.width = number;
        else if (property == "height") item.height = number;
        else item.scale = static_cast<int>(number);
    } else {
        return false;
    }

    auto candidate = document;
    candidate.items[index] = item;
    if (const auto problem = validateScene(candidate); !problem.isEmpty()) {
        setError(problem);
        return false;
    }
    placeInsideStage(item);
    candidate.items[index] = item;
    if (!commit(candidate)) return false;
    notifyItem(index);
    return true;
}

void SceneDocument::setVariables(const QList<LogicVariable>& newVariables) {
    QStringList before;
    for (const auto& item : document.items) before << displayedText(item.text, variables);
    const auto namesChanged = variableNames() != [&] {
        QStringList names;
        for (const auto& variable : newVariables) names << variable.name;
        return names;
    }();
    variables = newVariables;
    if (namesChanged) emit variablesChanged();
    for (int row = 0; row < document.items.size(); ++row) {
        if (document.items[row].type == "text" && before[row] != displayedText(document.items[row].text, variables)) notifyItem(row);
    }
}

void SceneDocument::renameVariable(const QString& name, const QString& newName) {
    const auto renameIn = [&](Scene& scene) {
        for (auto& item : scene.items) {
            if (item.type == "text") item.text = renameVariableInText(item.text, name, newName);
        }
    };
    const auto before = document.items;
    renameIn(document);
    for (auto& snapshot : history) renameIn(snapshot);
    QList<int> changed;
    for (int row = 0; row < document.items.size(); ++row) {
        if (document.items[row].text != before[row].text) changed.append(row);
    }
    if (changed.isEmpty()) return;
    if (loaded) {
        QString failure;
        setError(saveScene(path, document, failure) ? QString() : failure);
    }
    for (const auto row : changed) notifyItem(row);
}

void SceneDocument::setBackground(const QString& color) {
    if (!loaded || changing) return;
    auto candidate = document;
    candidate.background = color;
    if (commit(candidate)) emit backgroundChanged();
}

void SceneDocument::removeItem(int index) {
    if (!isValidIndex(index) || changing) return;
    auto candidate = document;
    candidate.items.removeAt(index);
    beginRemoveRows({}, index, index);
    const auto accepted = commit(candidate);
    endRemoveRows();
    if (!accepted) return;
    emit countChanged();
    selection = -1;
    emit selectionChanged();
}

bool SceneDocument::reorderItem(int index, int newIndex) {
    if (!isValidIndex(index) || !isValidIndex(newIndex) || index == newIndex || changing) return false;
    const auto selectedId = isValidIndex(selection) ? document.items[selection].id : QString();
    auto candidate = document;
    candidate.items.move(index, newIndex);
    if (const auto problem = validateScene(candidate); !problem.isEmpty()) {
        setError(problem);
        return false;
    }
    beginMoveRows({}, index, index, {}, newIndex > index ? newIndex + 1 : newIndex);
    commit(candidate);
    endMoveRows();
    selection = indexOfItem(selectedId);
    emit selectionChanged();
    return true;
}

bool SceneDocument::reorderItemToGap(int index, int gap) {
    if (!isValidIndex(index)) return false;
    const auto insertBefore = count() - std::clamp(gap, 0, count());
    return reorderItem(index, insertBefore > index ? insertBefore - 1 : insertBefore);
}

bool SceneDocument::bringItemForward(int index) { return reorderItem(index, index + 1); }
bool SceneDocument::sendItemBackward(int index) { return reorderItem(index, index - 1); }
bool SceneDocument::bringItemToFront(int index) { return reorderItem(index, count() - 1); }
bool SceneDocument::sendItemToBack(int index) { return reorderItem(index, 0); }

int SceneDocument::duplicateItem(int index) {
    if (!isValidIndex(index) || changing) return -1;
    if (const auto problem = roomProblem(); !problem.isEmpty()) {
        setNotice(problem + " Delete an item to make room for the copy.", true);
        return -1;
    }
    auto copy = document.items[index];
    copy.name = copyName(copy, document.items);
    copy.id = nextItemId();
    const auto bounds = itemBounds(copy);
    copy.x = offsetForCopy(copy.x, drawnWidth(copy), bounds.width());
    copy.y = offsetForCopy(copy.y, drawnHeight(copy), bounds.height());
    placeInsideStage(copy);

    auto candidate = document;
    const auto row = index + 1;
    candidate.items.insert(row, copy);
    if (const auto problem = validateScene(candidate); !problem.isEmpty()) {
        setNotice(problem, true);
        return -1;
    }
    beginInsertRows({}, row, row);
    commit(candidate);
    endInsertRows();
    emit countChanged();
    selection = row;
    emit selectionChanged();
    return row;
}

void SceneDocument::setItemHidden(int index, bool hidden) {
    if (!isValidIndex(index) || changing || hidden == isItemHidden(index)) return;
    if (hidden) hiddenItems.insert(document.items[index].id);
    else hiddenItems.remove(document.items[index].id);
    notifyItem(index);
}

bool SceneDocument::isItemHidden(int index) const {
    return isValidIndex(index) && hiddenItems.contains(document.items[index].id);
}

void SceneDocument::undo() {
    if (changing || history.isEmpty()) return;
    setNotice({}, false);
    const auto selectedId = isValidIndex(selection) ? document.items[selection].id : QString();
    beginResetModel();
    document = history.takeLast();
    selection = indexOfItem(selectedId);
    refreshImageProblems();
    endResetModel();
    QString failure;
    setError(saveScene(path, document, failure) ? QString() : failure);
    emit undoChanged();
    emit countChanged();
    emit backgroundChanged();
    emit worldChanged();
    emit selectionChanged();
}

}
