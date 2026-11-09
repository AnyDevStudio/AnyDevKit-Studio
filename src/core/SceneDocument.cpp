#include "SceneDocument.h"

#include <QFile>

#include <algorithm>
#include <cmath>

namespace Studio {

namespace {

constexpr double StageWidth = 1920;
constexpr double StageHeight = 1080;
constexpr double MinimumSize = 8;
constexpr int MaximumItems = 256;
constexpr int MaximumUndoSteps = 200;

double drawnWidth(const SceneItem& item) {
    return item.type == "text" ? static_cast<double>(item.text.size() * 6 - 1) * item.scale : item.width;
}

double drawnHeight(const SceneItem& item) {
    return item.type == "text" ? 7.0 * item.scale : item.height;
}

}

SceneDocument::SceneDocument(QObject* parent) : QAbstractListModel(parent) {}

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
    case TextRole: return item.text;
    case ScaleRole: return item.scale;
    case ControllableRole: return item.controllable;
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
    endResetModel();
    setError({});
    emit loadedChanged();
    emit countChanged();
    emit backgroundChanged();
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
    endResetModel();
    setError({});
    emit loadedChanged();
    emit countChanged();
    emit backgroundChanged();
    emit selectionChanged();
    emit undoChanged();
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

void SceneDocument::setSelectedIndex(int index) {
    const auto next = isValidIndex(index) ? index : -1;
    if (next == selection) return;
    selection = next;
    emit selectionChanged();
}

QVariantMap SceneDocument::selected() const {
    if (!isValidIndex(selection)) return {};
    const auto& item = document.items[selection];
    return {
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
    };
}

bool SceneDocument::isValidIndex(int index) const {
    return index >= 0 && index < document.items.size();
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
    pushUndo();
    document = candidate;
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
    item.x = std::round(std::clamp(item.x, 0.0, std::max(0.0, StageWidth - drawnWidth(item))));
    item.y = std::round(std::clamp(item.y, 0.0, std::max(0.0, StageHeight - drawnHeight(item))));
}

QString SceneDocument::nextItemId() const {
    for (int number = 1;; ++number) {
        const auto id = QString("item-%1").arg(number);
        if (std::ranges::none_of(document.items, [&](const SceneItem& item) { return item.id == id; })) return id;
    }
}

int SceneDocument::addItem(const QString& type, double centerX, double centerY) {
    if (!loaded || changing) return -1;
    if (document.items.size() >= MaximumItems) {
        setError(QString("A scene can hold at most %1 items.").arg(MaximumItems));
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
    } else {
        item.name = "Shape " + number;
        item.width = 200;
        item.height = 200;
    }
    item.x = centerX - drawnWidth(item) / 2;
    item.y = centerY - drawnHeight(item) / 2;
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

void SceneDocument::resizeItem(int index, double width, double height) {
    if (!isValidIndex(index) || document.items[index].type != "rectangle") return;
    auto item = document.items[index];
    item.width = std::round(std::clamp(width, MinimumSize, std::max(MinimumSize, StageWidth - item.x)));
    item.height = std::round(std::clamp(height, MinimumSize, std::max(MinimumSize, StageHeight - item.y)));
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

    if (property == "name") item.name = value.toString();
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

void SceneDocument::undo() {
    if (changing || history.isEmpty()) return;
    beginResetModel();
    document = history.takeLast();
    endResetModel();
    if (!isValidIndex(selection)) selection = -1;
    QString failure;
    setError(saveScene(path, document, failure) ? QString() : failure);
    emit undoChanged();
    emit countChanged();
    emit backgroundChanged();
    emit selectionChanged();
}

}
