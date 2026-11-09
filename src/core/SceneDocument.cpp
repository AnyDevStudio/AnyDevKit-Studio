#include "SceneDocument.h"

#include <QFile>
#include <QFileInfo>
#include <QUrl>

#include <algorithm>
#include <cmath>

namespace Studio {

namespace {

constexpr double StageWidth = 1920;
constexpr double StageHeight = 1080;
constexpr double MinimumSize = 8;
constexpr int MaximumItems = 256;
constexpr int MaximumUndoSteps = 200;
// A new picture never covers more than this, so the rest of the scene stays reachable.
constexpr double LargestNewImageWidth = StageWidth / 2;
constexpr double LargestNewImageHeight = StageHeight / 2;

double drawnHeight(const SceneItem& item) {
    return item.type == "text" ? 7.0 * item.scale : item.height;
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
    case ImageUrlRole: return item.type == "image" && !imageProblems.contains(item.source) ? QUrl::fromLocalFile(projectRoot() + '/' + item.source) : QUrl();
    case ImageProblemRole: return imageProblems.value(item.source);
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
    endResetModel();
    setError({});
    setNotice({}, false);
    emit loadedChanged();
    emit countChanged();
    emit backgroundChanged();
    emit selectionChanged();
    emit undoChanged();
    emit imagesChanged();
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
    item.x = std::round(std::clamp(item.x, 0.0, std::max(0.0, StageWidth - drawnWidth(item))));
    item.y = std::round(std::clamp(item.y, 0.0, std::max(0.0, StageHeight - drawnHeight(item))));
}

QString SceneDocument::nextItemId() const {
    for (int number = 1;; ++number) {
        const auto id = QString("item-%1").arg(number);
        if (std::ranges::none_of(document.items, [&](const SceneItem& item) { return item.id == id; })) return id;
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
        const auto largest = std::min((StageWidth - item.x) / start.width, (StageHeight - item.y) / start.height);
        const auto smallest = std::max(MinimumSize / start.width, MinimumSize / start.height);
        const auto scale = std::clamp(std::max(width / start.width, height / start.height), smallest, std::max(smallest, largest));
        item.width = std::round(start.width * scale);
        item.height = std::round(start.height * scale);
    } else {
        item.width = std::round(std::clamp(width, MinimumSize, std::max(MinimumSize, StageWidth - item.x)));
        item.height = std::round(std::clamp(height, MinimumSize, std::max(MinimumSize, StageHeight - item.y)));
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

void SceneDocument::undo() {
    if (changing || history.isEmpty()) return;
    setNotice({}, false);
    beginResetModel();
    document = history.takeLast();
    refreshImageProblems();
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
