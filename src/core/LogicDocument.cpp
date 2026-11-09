#include "LogicDocument.h"

#include <QFile>
#include <QFileInfo>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace Studio {

namespace {

constexpr int MaximumUndoSteps = 200;
constexpr auto NewBlockPlaceholder = "new-block-placeholder";

struct Located {
    QList<Block>* list = nullptr;
    int index = -1;
    Block* parent = nullptr;
};

bool locate(QList<Block>& blocks, const QString& id, Block* parent, int depth, Located& out) {
    for (qsizetype index = 0; index < blocks.size(); ++index) {
        auto& block = blocks[index];
        if (block.id == id) {
            out = {&blocks, static_cast<int>(index), parent};
            return true;
        }
        if (depth < MaximumLogicDepth && locate(block.children, id, &block, depth + 1, out)) return true;
    }
    return false;
}

void collectIds(const QList<Block>& blocks, int depth, QStringList& ids) {
    for (const auto& block : blocks) {
        ids.append(block.id);
        if (depth < MaximumLogicDepth) collectIds(block.children, depth + 1, ids);
    }
}

QStringList readingOrder(const LogicProgram& program) {
    QStringList ids;
    collectIds(program.events, 1, ids);
    return ids;
}

const BlockInfo* findInfo(const QString& type) {
    for (const auto& info : blockCatalog()) {
        if (info.jsonName == type) return &info;
    }
    return nullptr;
}

// How many levels this block and its contents take up.
int depthOf(const QList<Block>& blocks) {
    int deepest = 0;
    for (const auto& block : blocks) deepest = std::max(deepest, 1 + depthOf(block.children));
    return deepest;
}

bool sameShape(const QList<Block>& left, const QList<Block>& right) {
    if (left.size() != right.size()) return false;
    for (qsizetype index = 0; index < left.size(); ++index) {
        if (left[index].id != right[index].id || !sameShape(left[index].children, right[index].children)) return false;
    }
    return true;
}

const Block* findBlock(const QList<Block>& blocks, const QString& id, int depth) {
    for (const auto& block : blocks) {
        if (block.id == id) return &block;
        if (depth < MaximumLogicDepth) {
            if (const auto* found = findBlock(block.children, id, depth + 1)) return found;
        }
    }
    return nullptr;
}

QString plainMessage(const LogicProblem& problem, const LogicProgram& program) {
    // Messages say which block they mean; on the block itself that part would only be noise.
    if (const auto* block = findBlock(program.events, problem.blockId, 1)) {
        const auto prefix = QString("The \"%1\" block (%2)").arg(blockInfo(block->type).title, block->id);
        if (problem.message.startsWith(prefix)) return "This block" + problem.message.mid(prefix.size());
    }
    return problem.message;
}

QVariantList fieldsVariant(const BlockInfo& info) {
    QVariantList list;
    const auto fields = blockFields(info);
    for (const auto& field : fields) list.append(QVariantMap{{"key", field.key}, {"kind", field.kind}, {"label", field.label}});
    return list;
}

}

LogicDocument::LogicDocument(QObject* parent) : QAbstractListModel(parent) {
    layout = layoutLogic(document, viewportWidth);
}

int LogicDocument::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(rows.size());
}

QVariant LogicDocument::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= rows.size()) return {};
    const auto& id = rows[index.row()];
    const auto* block = blocksById.value(id);
    if (!block) return {};
    const auto& info = blockInfo(block->type);
    const auto* place = layout.find(id);

    const auto nameOf = [&](const QString& itemId) -> QString {
        if (itemId.isEmpty()) return {};
        for (const auto& item : scene.items) {
            if (item.id == itemId) return item.name.isEmpty() ? item.id : item.name;
        }
        return "Missing item";
    };

    switch (role) {
    case IdRole: return id;
    case TypeRole: return info.jsonName;
    case TitleRole: return info.title;
    case CategoryRole: return blockCategory(info);
    case IsEventRole: return info.isEvent;
    case HoldsActionsRole: return info.holdsActions;
    case FieldsRole: return fieldsVariant(info);
    case RunOrderRole: return place ? place->runOrder : 0;
    case DepthRole: return place ? place->depth : 1;
    case XRole: return place ? place->x : 0.0;
    case YRole: return place ? place->y : 0.0;
    case WidthRole: return place ? place->width : LogicMetrics::BlockWidth;
    case HeightRole: return place ? place->height : LogicMetrics::RowHeight;
    case HeaderHeightRole: return place ? place->headerHeight : LogicMetrics::RowHeight;
    case CavityHeightRole: return place ? place->cavityHeight : 0.0;
    case FooterHeightRole: return place ? place->footerHeight : 0.0;
    case TwoRowsRole: return place && place->twoRows;
    case ItemRole: return block->item;
    case OtherItemRole: return block->otherItem;
    case ItemNameRole: return nameOf(block->item);
    case OtherItemNameRole: return nameOf(block->otherItem);
    case ButtonRole: {
        for (const auto& candidate : buttonCatalog()) {
            if (candidate.button == block->button) return candidate.jsonName;
        }
        return {};
    }
    case PointXRole: return block->x;
    case PointYRole: return block->y;
    case ColorRole: return block->color;
    case ValueRole: return block->value;
    case ComparisonRole: {
        for (const auto& candidate : comparisonCatalog()) {
            if (candidate.comparison == block->comparison) return candidate.jsonName;
        }
        return {};
    }
    case ProblemRole: return problems.value(id);
    case DraggedRole: return carried.contains(id);
    case EmptyCavityRole: return info.holdsActions && !info.isEvent && block->children.isEmpty();
    case ProblemHeightRole: return place ? place->problemHeight : 0.0;
    case VariableRole: return block->variable;
    default: return {};
    }
}

QHash<int, QByteArray> LogicDocument::roleNames() const {
    return {
        {IdRole, "blockId"},
        {TypeRole, "blockType"},
        {TitleRole, "title"},
        {CategoryRole, "category"},
        {IsEventRole, "isEvent"},
        {HoldsActionsRole, "holdsActions"},
        {FieldsRole, "fields"},
        {RunOrderRole, "runOrder"},
        {DepthRole, "depth"},
        {XRole, "blockX"},
        {YRole, "blockY"},
        {WidthRole, "blockWidth"},
        {HeightRole, "blockHeight"},
        {HeaderHeightRole, "headerHeight"},
        {CavityHeightRole, "cavityHeight"},
        {FooterHeightRole, "footerHeight"},
        {TwoRowsRole, "twoRows"},
        {ItemRole, "item"},
        {OtherItemRole, "otherItem"},
        {ItemNameRole, "itemName"},
        {OtherItemNameRole, "otherItemName"},
        {ButtonRole, "button"},
        {PointXRole, "pointX"},
        {PointYRole, "pointY"},
        {ColorRole, "blockColor"},
        {ValueRole, "value"},
        {ComparisonRole, "comparison"},
        {ProblemRole, "problem"},
        {DraggedRole, "dragged"},
        {EmptyCavityRole, "emptyCavity"},
        {ProblemHeightRole, "problemHeight"},
        {VariableRole, "variable"},
    };
}

bool LogicDocument::open(const QString& newPath) {
    close();
    path = newPath;
    const QFileInfo info(newPath);
    if (info.exists() || info.isSymLink()) {
        const auto result = loadLogic(newPath);
        if (!result.program) {
            failure = result.error;
            emit loadedChanged();
            return false;
        }
        document = *result.program;
    }
    loaded = true;
    failure.clear();
    afterChange();
    emit loadedChanged();
    return true;
}

void LogicDocument::close() {
    if (drag) finishDrag(false);
    beginResetModel();
    path.clear();
    document = {};
    history.clear();
    rows.clear();
    blocksById.clear();
    problems.clear();
    wholeProgramProblem.clear();
    firstProblem.clear();
    selection.clear();
    loaded = false;
    failure.clear();
    layout = layoutLogic(document, viewportWidth);
    endResetModel();
    setError({});
    emit loadedChanged();
    emit programChanged();
    emit undoChanged();
    emit problemsChanged();
    emit selectionChanged();
    emit layoutChanged();
}

void LogicDocument::reopen() {
    if (path.isEmpty()) return;
    const auto current = path;
    open(current);
}

void LogicDocument::setScene(const Scene& newScene) {
    scene = newScene;
    emit sceneItemsChanged();
    const auto beforeProblems = problems;
    refreshProblems();
    if (problems.size() != beforeProblems.size() || problems.keys() != beforeProblems.keys()) {
        if (!drag) applyLayout(computeLayout(document));
    }
    if (!rows.isEmpty()) emit dataChanged(index(0), index(static_cast<int>(rows.size()) - 1), {ProblemRole, ItemNameRole, OtherItemNameRole});
}

const LogicProgram& LogicDocument::program() const { return document; }
bool LogicDocument::isLoaded() const { return loaded; }
bool LogicDocument::isEmpty() const { return document.events.isEmpty(); }
int LogicDocument::count() const { return rowCount(); }
bool LogicDocument::canUndo() const { return !history.isEmpty(); }
QString LogicDocument::error() const { return message; }
QString LogicDocument::loadError() const { return failure; }
int LogicDocument::problemCount() const { return static_cast<int>(problems.size()) + (wholeProgramProblem.isEmpty() ? 0 : 1); }
QString LogicDocument::programProblem() const { return wholeProgramProblem; }
QString LogicDocument::firstProblemId() const { return firstProblem; }
QString LogicDocument::selectedId() const { return selection; }
double LogicDocument::canvasWidth() const { return viewportWidth; }
double LogicDocument::contentWidth() const { return layout.contentWidth; }
double LogicDocument::contentHeight() const { return layout.contentHeight; }
bool LogicDocument::isDragging() const { return drag.has_value(); }

void LogicDocument::setSelectedId(const QString& id) {
    const auto next = blocksById.contains(id) ? id : QString();
    if (next == selection) return;
    selection = next;
    emit selectionChanged();
}

QVariantList LogicDocument::palette() const {
    QVariantList groups;
    for (const auto* key : {"events", "movement", "looks", "variables"}) {
        const QString category(key);
        QVariantList blocks;
        for (const auto& info : blockCatalog()) {
            if (blockCategory(info) != category) continue;
            blocks.append(QVariantMap{{"type", info.jsonName}, {"title", info.title}, {"isEvent", info.isEvent}, {"holdsActions", info.holdsActions}, {"category", category}});
        }
        if (!blocks.isEmpty()) groups.append(QVariantMap{{"key", category}, {"title", blockCategoryTitle(category)}, {"blocks", blocks}});
    }
    return groups;
}

QVariantList LogicDocument::buttons() const {
    QVariantList list;
    for (const auto& candidate : buttonCatalog()) list.append(QVariantMap{{"id", candidate.jsonName}, {"name", candidate.title}});
    return list;
}

QVariantList LogicDocument::comparisons() const {
    QVariantList list;
    for (const auto& candidate : comparisonCatalog()) list.append(QVariantMap{{"id", candidate.jsonName}, {"name", candidate.title}});
    return list;
}

QVariantList LogicDocument::variables() const {
    QVariantList list;
    for (const auto& variable : document.variables) list.append(QVariantMap{{"id", variable.name}, {"name", variable.name}, {"initial", variable.initial}});
    return list;
}

QVariantList LogicDocument::sceneItems() const {
    QVariantList list;
    for (const auto& item : scene.items) list.append(QVariantMap{{"id", item.id}, {"name", item.name.isEmpty() ? item.id : item.name}});
    return list;
}

void LogicDocument::setCanvasWidth(double width) {
    if (width <= 0 || width == viewportWidth) return;
    viewportWidth = width;
    if (!drag) applyLayout(computeLayout(document));
}

QVariantMap LogicDocument::landing() const {
    if (!drag || !drag->spot) return {{"valid", false}};
    const auto* place = layout.find(drag->id);
    return {
        {"valid", true},
        {"parentId", drag->spot->parentId},
        {"index", drag->spot->index},
        {"x", place ? place->x : drag->spot->x},
        {"y", place ? place->y : drag->spot->y},
        {"width", place ? place->width : LogicMetrics::BlockWidth},
        {"height", place ? place->height : LogicMetrics::RowHeight},
        {"isEvent", drag->spot->isEvent},
    };
}

QVariantMap LogicDocument::positionOf(const QString& id) const {
    const auto* place = layout.find(id);
    if (!place) return {};
    return {{"x", place->x}, {"y", place->y}, {"width", place->width}, {"height", place->height}};
}

QString LogicDocument::nextBlockId(const QString& id) const {
    const auto ids = readingOrder(document);
    const auto index = ids.indexOf(id);
    return index >= 0 && index + 1 < ids.size() ? ids[index + 1] : QString();
}

QString LogicDocument::previousBlockId(const QString& id) const {
    const auto ids = readingOrder(document);
    const auto index = ids.indexOf(id);
    return index > 0 ? ids[index - 1] : QString();
}

Block LogicDocument::makeBlock(const BlockInfo& info, const QString& id) const {
    Block block;
    block.id = id;
    block.type = info.type;
    if (info.usesItem && !scene.items.isEmpty()) block.item = scene.items.first().id;
    if (info.usesVariable && !document.variables.isEmpty()) block.variable = document.variables.first().name;
    if (info.usesOtherItem && scene.items.size() > 1) block.otherItem = scene.items[1].id;
    if (info.usesPosition) block.x = 4;
    if (info.usesValue) block.value = 1;
    if (info.usesComparison) block.comparison = VariableComparison::GreaterOrEqual;
    return block;
}

LogicProgram LogicDocument::withInserted(const LogicProgram& base, const QList<Block>& blocks, const QString& parentId, int index) const {
    auto candidate = base;
    QList<Block>* target = &candidate.events;
    if (!parentId.isEmpty()) {
        Located found;
        if (!locate(candidate.events, parentId, nullptr, 1, found)) return candidate;
        target = &(*found.list)[found.index].children;
    }
    index = std::clamp(index, 0, static_cast<int>(target->size()));
    for (qsizetype offset = 0; offset < blocks.size(); ++offset) target->insert(index + offset, blocks[offset]);
    return candidate;
}

bool LogicDocument::commit(const LogicProgram& candidate) {
    if (!loaded) return false;
    const auto structural = validateLogicStructure(candidate);
    if (!structural.isEmpty()) {
        setError(plainMessage(structural.first(), candidate));
        return false;
    }
    history.append(document);
    if (history.size() > MaximumUndoSteps) history.removeFirst();
    document = candidate;
    QString saveProblem;
    setError(saveLogic(path, document, saveProblem) ? QString() : saveProblem);
    afterChange();
    emit undoChanged();
    return true;
}

void LogicDocument::afterChange() {
    blocksById.clear();
    const auto collect = [&](const auto& self, const QList<Block>& blocks, int depth) -> void {
        for (const auto& block : blocks) {
            blocksById.insert(block.id, &block);
            if (depth < MaximumLogicDepth) self(self, block.children, depth + 1);
        }
    };
    collect(collect, std::as_const(document).events, 1);

    if (!selection.isEmpty() && !blocksById.contains(selection)) {
        selection.clear();
        emit selectionChanged();
    }
    refreshProblems();
    layout = computeLayout(document);
    syncRows();
    if (!rows.isEmpty()) {
        // Not the fields role: it never changes for a block, and a new list would rebuild its editors
        // and drop the focus from whichever one the person is typing in.
        emit dataChanged(index(0), index(static_cast<int>(rows.size()) - 1),
            {RunOrderRole, DepthRole, XRole, YRole, WidthRole, HeightRole, HeaderHeightRole, CavityHeightRole, FooterHeightRole, ProblemHeightRole,
             TwoRowsRole, ItemRole, OtherItemRole, ItemNameRole, OtherItemNameRole, ButtonRole, PointXRole, PointYRole, ColorRole, ValueRole,
             ComparisonRole, ProblemRole, DraggedRole, EmptyCavityRole, VariableRole});
    }
    emit programChanged();
    emit layoutChanged();
}

void LogicDocument::refreshProblems() {
    const auto list = validateLogic(document, scene);
    QHash<QString, QString> next;
    QString whole;
    QString first;
    for (const auto& problem : list) {
        if (problem.blockId.isEmpty()) {
            if (whole.isEmpty()) whole = problem.message;
            continue;
        }
        if (first.isEmpty()) first = problem.blockId;
        if (!next.contains(problem.blockId)) next.insert(problem.blockId, plainMessage(problem, document));
    }
    if (next == problems && whole == wholeProgramProblem && first == firstProblem) return;
    problems = next;
    wholeProgramProblem = whole;
    firstProblem = first;
    emit problemsChanged();
}

LogicLayout LogicDocument::computeLayout(const LogicProgram& program) const {
    const auto keys = problems.keys();
    const QSet<QString> ids(keys.begin(), keys.end());
    return layoutLogic(program, viewportWidth, &ids);
}

void LogicDocument::applyLayout(const LogicLayout& next) {
    QList<int> changed;
    for (int row = 0; row < rows.size(); ++row) {
        const auto* before = layout.find(rows[row]);
        const auto* after = next.find(rows[row]);
        if (!after) continue;
        if (!before || before->x != after->x || before->y != after->y || before->width != after->width || before->height != after->height || before->cavityHeight != after->cavityHeight) changed.append(row);
    }
    layout = next;
    for (const auto row : changed) {
        const auto changedIndex = index(row);
        emit dataChanged(changedIndex, changedIndex, {XRole, YRole, WidthRole, HeightRole, HeaderHeightRole, CavityHeightRole, FooterHeightRole, RunOrderRole, DepthRole, TwoRowsRole, ProblemHeightRole});
    }
    emit layoutChanged();
}

void LogicDocument::syncRows() {
    const auto desired = readingOrder(document);
    const QSet<QString> keep(desired.begin(), desired.end());
    for (auto row = static_cast<int>(rows.size()) - 1; row >= 0; --row) {
        if (keep.contains(rows[row])) continue;
        beginRemoveRows({}, row, row);
        rows.removeAt(row);
        endRemoveRows();
    }
    const QSet<QString> present(rows.begin(), rows.end());
    for (const auto& id : desired) {
        if (present.contains(id)) continue;
        const auto row = static_cast<int>(rows.size());
        beginInsertRows({}, row, row);
        rows.append(id);
        endInsertRows();
    }
}

void LogicDocument::setError(const QString& text) {
    if (text == message) return;
    message = text;
    emit errorChanged();
}

QString LogicDocument::insertInto(const QString& type, const QString& parentId, int position) {
    const auto* info = findInfo(type);
    if (!info || !loaded) return {};
    const auto id = newBlockId(document);
    const auto candidate = withInserted(document, {makeBlock(*info, id)}, parentId, position);
    if (!commit(candidate)) return {};
    selection = id;
    emit selectionChanged();
    return id;
}

QString LogicDocument::addBlock(const QString& type, const QString& parentId, int position) {
    return insertInto(type, parentId, position);
}

QString LogicDocument::addFromPalette(const QString& type) {
    const auto* info = findInfo(type);
    if (!info || !loaded) return {};
    if (info->isEvent) return insertInto(type, {}, static_cast<int>(document.events.size()));

    if (document.events.isEmpty()) {
        QString firstEvent;
        for (const auto& candidate : blockCatalog()) {
            if (candidate.isEvent) {
                firstEvent = candidate.title;
                break;
            }
        }
        setError(QString("Actions run when an event starts them. Add an event first, such as \"%1\".").arg(firstEvent));
        return {};
    }

    QString parentId = document.events.last().id;
    auto position = static_cast<int>(document.events.last().children.size());
    if (!selection.isEmpty()) {
        auto copy = document;
        Located found;
        if (locate(copy.events, selection, nullptr, 1, found)) {
            const auto& selected = (*found.list)[found.index];
            if (blockInfo(selected.type).holdsActions) {
                parentId = selected.id;
                position = static_cast<int>(selected.children.size());
            } else if (found.parent) {
                parentId = found.parent->id;
                position = found.index + 1;
            }
        }
    }
    return insertInto(type, parentId, position);
}

void LogicDocument::removeBlock(const QString& id) {
    if (drag) return;
    auto candidate = document;
    Located found;
    if (!locate(candidate.events, id, nullptr, 1, found)) return;
    found.list->removeAt(found.index);
    commit(candidate);
}

bool LogicDocument::setField(const QString& id, const QString& key, const QVariant& value) {
    if (drag) return false;
    auto candidate = document;
    Located found;
    if (!locate(candidate.events, id, nullptr, 1, found)) return false;
    auto& block = (*found.list)[found.index];
    const auto before = block;

    if (key == "item") {
        block.item = value.toString();
    } else if (key == "otherItem") {
        block.otherItem = value.toString();
    } else if (key == "x" || key == "y") {
        bool numeric = false;
        const auto number = value.toDouble(&numeric);
        if (!numeric || !std::isfinite(number)) {
            setError(QString("Type a number for %1.").arg(key));
            return false;
        }
        (key == "x" ? block.x : block.y) = number;
    } else if (key == "value") {
        bool numeric = false;
        const auto number = value.toDouble(&numeric);
        if (!numeric || number != std::floor(number)) {
            setError("Type a whole number here, for example 10.");
            return false;
        }
        if (std::abs(number) > MaximumVariableValue) {
            setError(QString("This needs a whole number from -%1 to %1.").arg(MaximumVariableValue));
            return false;
        }
        block.value = static_cast<int>(number);
    } else if (key == "color") {
        block.color = value.toString();
    } else if (key == "variable") {
        block.variable = value.toString();
    } else if (key == "button") {
        const auto name = value.toString();
        const auto found2 = std::ranges::find(buttonCatalog(), name, &ButtonInfo::jsonName);
        if (found2 == buttonCatalog().end()) return false;
        block.button = found2->button;
    } else if (key == "comparison") {
        const auto name = value.toString();
        const auto found2 = std::ranges::find(comparisonCatalog(), name, &ComparisonInfo::jsonName);
        if (found2 == comparisonCatalog().end()) return false;
        block.comparison = found2->comparison;
    } else {
        return false;
    }

    const auto unchanged = block.item == before.item && block.otherItem == before.otherItem && block.x == before.x && block.y == before.y
        && block.value == before.value && block.color == before.color && block.button == before.button && block.comparison == before.comparison
        && block.variable == before.variable;
    if (unchanged) {
        setError({});
        return true;
    }
    return commit(candidate);
}

void LogicDocument::undo() {
    if (drag || history.isEmpty()) return;
    const auto before = document.variables;
    document = history.takeLast();
    QString saveProblem;
    setError(saveLogic(path, document, saveProblem) ? QString() : saveProblem);
    afterChange();
    emit undoChanged();

    // A rename changed one name in place, and text in the scene has to go back with it.
    if (before.size() != document.variables.size()) return;
    for (qsizetype index = 0; index < before.size(); ++index) {
        if (before[index].name != document.variables[index].name) {
            emit variableRenamed(before[index].name, document.variables[index].name);
            return;
        }
    }
}

QString LogicDocument::variableNameProblem(const QString& name, const QString& ignoring) const {
    if (name.isEmpty()) return "Type a name for the variable first.";
    if (!isVariableName(name)) return QString("A variable name can use letters, digits and _, up to %1 characters, and cannot start with a digit. Try score or lives.").arg(MaximumVariableNameLength);
    const auto folded = name.toCaseFolded();
    for (const auto& variable : document.variables) {
        if (variable.name != ignoring && variable.name.toCaseFolded() == folded) return QString("There is already a variable named %1. Choose another name.").arg(variable.name);
    }
    return {};
}

bool LogicDocument::addVariable(const QString& name) {
    if (!loaded || drag) return false;
    if (const auto problem = variableNameProblem(name); !problem.isEmpty()) {
        setError(problem);
        return false;
    }
    auto candidate = document;
    candidate.variables.append({name, 0});
    return commit(candidate);
}

bool LogicDocument::renameVariable(const QString& name, const QString& newName) {
    if (!loaded || drag) return false;
    if (name == newName) {
        setError({});
        return true;
    }
    if (const auto problem = variableNameProblem(newName, name); !problem.isEmpty()) {
        setError(problem);
        return false;
    }
    // The new name can be longer, and a text that shows it still has to fit its 64 characters.
    auto renamedScene = scene;
    for (auto& item : renamedScene.items) {
        if (item.type == "text") item.text = renameVariableInText(item.text, name, newName);
    }
    if (const auto textProblem = validateScene(renamedScene); !textProblem.isEmpty()) {
        setError(QString("Renaming to %1 would make a text item too long. %2 Choose a shorter name, or shorten the text in the Scene tab.").arg(newName, textProblem));
        return false;
    }

    auto candidate = document;
    const auto entry = std::ranges::find(candidate.variables, name, &LogicVariable::name);
    if (entry == candidate.variables.end()) return false;
    entry->name = newName;
    const auto renameIn = [&](const auto& self, QList<Block>& blocks) -> void {
        for (auto& block : blocks) {
            if (blockInfo(block.type).usesVariable && block.variable == name) block.variable = newName;
            self(self, block.children);
        }
    };
    renameIn(renameIn, candidate.events);
    if (!commit(candidate)) return false;
    emit variableRenamed(name, newName);
    return true;
}

bool LogicDocument::setVariableInitial(const QString& name, const QVariant& value) {
    if (!loaded || drag) return false;
    auto candidate = document;
    const auto entry = std::ranges::find(candidate.variables, name, &LogicVariable::name);
    if (entry == candidate.variables.end()) return false;
    bool numeric = false;
    const auto number = value.toDouble(&numeric);
    if (!numeric || !std::isfinite(number) || number != std::floor(number)) {
        setError("A variable starts at a whole number. Type one, for example 0.");
        return false;
    }
    if (std::abs(number) > MaximumVariableValue) {
        setError(QString("A variable can start at a whole number from -%1 to %1.").arg(MaximumVariableValue));
        return false;
    }
    if (entry->initial == static_cast<int>(number)) {
        setError({});
        return true;
    }
    entry->initial = static_cast<int>(number);
    return commit(candidate);
}

void LogicDocument::removeVariable(const QString& name) {
    if (!loaded || drag) return;
    auto candidate = document;
    if (candidate.variables.removeIf([&](const LogicVariable& variable) { return variable.name == name; }) == 0) return;
    commit(candidate);
}

QVariantList LogicDocument::variableUses(const QString& name) const {
    QVariantList uses;
    const auto walk = [&](const auto& self, const QList<Block>& blocks, const QString& eventTitle, int depth) -> void {
        for (const auto& block : blocks) {
            const auto& info = blockInfo(block.type);
            const auto where = eventTitle.isEmpty() ? info.title : eventTitle;
            if (info.usesVariable && block.variable == name) uses.append(QVariantMap{{"kind", "block"}, {"title", info.title}, {"where", QString("Inside \"%1\"").arg(where)}});
            if (depth < MaximumLogicDepth) self(self, block.children, where, depth + 1);
        }
    };
    walk(walk, document.events, {}, 1);
    for (const auto& item : scene.items) {
        if (item.type == "text" && textVariables(item.text).contains(name)) {
            uses.append(QVariantMap{{"kind", "text"}, {"title", item.name.isEmpty() ? item.id : item.name}, {"where", "Shown on the stage"}});
        }
    }
    return uses;
}

void LogicDocument::startMoving(const QString& id) {
    if (!loaded || drag) return;
    auto remainder = document;
    Located found;
    if (!locate(remainder.events, id, nullptr, 1, found)) return;

    DragState state;
    state.isNew = false;
    state.id = id;
    if (!found.parent) {
        state.stack = {found.list->takeAt(found.index)};
    } else {
        state.stack = found.list->mid(found.index);
        found.list->remove(found.index, found.list->size() - found.index);
    }
    state.remainder = remainder;
    QStringList ids;
    collectIds(state.stack, 1, ids);
    state.ids = ids;
    state.remainderLayout = computeLayout(state.remainder);
    state.spots = landingSpots(state.remainder, state.remainderLayout, blockInfo(state.stack.first().type).isEvent, depthOf(state.stack));
    drag = state;

    selection = id;
    emit selectionChanged();
    carried = ids;
    for (int row = 0; row < rows.size(); ++row) {
        if (carried.contains(rows[row])) emit dataChanged(index(row), index(row), {DraggedRole});
    }
    applyLayout(drag->remainderLayout);
    emit dragChanged();
}

void LogicDocument::startAdding(const QString& type) {
    if (!loaded || drag) return;
    const auto* info = findInfo(type);
    if (!info) return;
    DragState state;
    state.isNew = true;
    state.id = NewBlockPlaceholder;
    state.stack = {makeBlock(*info, NewBlockPlaceholder)};
    state.remainder = document;
    state.ids = {state.id};
    state.remainderLayout = computeLayout(state.remainder);
    state.spots = landingSpots(state.remainder, state.remainderLayout, info->isEvent, 1);
    drag = state;
    emit dragChanged();
}

void LogicDocument::dragTo(double x, double y) {
    if (!drag) return;
    const auto* next = nearestLanding(drag->spots, x, y);
    const auto same = (!next && !drag->spot) || (next && drag->spot && next->parentId == drag->spot->parentId && next->index == drag->spot->index);
    if (same) return;
    if (next) drag->spot = *next;
    else drag->spot.reset();

    if (next) applyLayout(computeLayout(withInserted(drag->remainder, drag->stack, next->parentId, next->index)));
    else applyLayout(drag->remainderLayout);
    emit dragChanged();
}

void LogicDocument::finishDrag(bool restoreLayout) {
    if (!drag) return;
    const auto ids = carried;
    carried.clear();
    drag.reset();
    for (int row = 0; row < rows.size(); ++row) {
        if (ids.contains(rows[row])) emit dataChanged(index(row), index(row), {DraggedRole});
    }
    if (restoreLayout) applyLayout(computeLayout(document));
    emit dragChanged();
}

QString LogicDocument::drop() {
    if (!drag) return {};
    if (!drag->spot) {
        const auto wasNew = drag->isNew;
        const auto isEvent = blockInfo(drag->stack.first().type).isEvent;
        finishDrag(true);
        if (wasNew && !isEvent) {
            setError(document.events.isEmpty()
                ? QString("Actions run when an event starts them. Add an event first, then snap this block under it.")
                : QString("Drop this block closer to an event or under another block."));
        }
        return {};
    }

    auto spot = *drag->spot;
    auto stack = drag->stack;
    const auto id = drag->isNew ? newBlockId(document) : drag->id;
    if (drag->isNew) {
        stack.first().id = id;
    }
    auto candidate = withInserted(drag->remainder, stack, spot.parentId, spot.index);
    const auto unchanged = sameShape(candidate.events, document.events);

    finishDrag(false);
    if (unchanged) {
        applyLayout(computeLayout(document));
        return id;
    }
    if (!commit(candidate)) return {};
    selection = id;
    emit selectionChanged();
    return id;
}

void LogicDocument::dropOnPalette() {
    if (!drag) return;
    if (drag->isNew) {
        cancelDrag();
        return;
    }
    const auto candidate = drag->remainder;
    finishDrag(false);
    commit(candidate);
}

void LogicDocument::cancelDrag() {
    finishDrag(true);
}

}
