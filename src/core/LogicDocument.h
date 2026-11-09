#pragma once

#include "Blocks.h"
#include "LogicLayout.h"
#include "Scene.h"

#include <QAbstractListModel>
#include <QVariantMap>

#include <optional>

namespace Studio {

// The logic being edited. Rows are blocks in a stable order, so an interface delegate lives as long
// as its block does and can animate when the block moves; the tree order is in the x and y roles.
class LogicDocument : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(bool loaded READ isLoaded NOTIFY loadedChanged)
    Q_PROPERTY(bool empty READ isEmpty NOTIFY programChanged)
    Q_PROPERTY(int count READ count NOTIFY programChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QString loadError READ loadError NOTIFY loadedChanged)
    Q_PROPERTY(int problemCount READ problemCount NOTIFY problemsChanged)
    Q_PROPERTY(QString programProblem READ programProblem NOTIFY problemsChanged)
    Q_PROPERTY(QString firstProblemId READ firstProblemId NOTIFY problemsChanged)
    Q_PROPERTY(QString selectedId READ selectedId WRITE setSelectedId NOTIFY selectionChanged)
    Q_PROPERTY(QVariantList palette READ palette CONSTANT)
    Q_PROPERTY(QVariantList buttons READ buttons CONSTANT)
    Q_PROPERTY(QVariantList comparisons READ comparisons CONSTANT)
    Q_PROPERTY(QVariantList variables READ variables NOTIFY programChanged)
    Q_PROPERTY(QVariantList sounds READ sounds NOTIFY programChanged)
    Q_PROPERTY(QVariantList sceneItems READ sceneItems NOTIFY sceneItemsChanged)
    Q_PROPERTY(QVariantList scenes READ scenes NOTIFY sceneListChanged)
    Q_PROPERTY(double canvasWidth READ canvasWidth WRITE setCanvasWidth NOTIFY layoutChanged)
    Q_PROPERTY(double contentWidth READ contentWidth NOTIFY layoutChanged)
    Q_PROPERTY(double contentHeight READ contentHeight NOTIFY layoutChanged)
    Q_PROPERTY(bool dragging READ isDragging NOTIFY dragChanged)
    Q_PROPERTY(QVariantMap landing READ landing NOTIFY dragChanged)

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        TypeRole,
        TitleRole,
        CategoryRole,
        IsEventRole,
        HoldsActionsRole,
        FieldsRole,
        RunOrderRole,
        DepthRole,
        XRole,
        YRole,
        WidthRole,
        HeightRole,
        HeaderHeightRole,
        CavityHeightRole,
        FooterHeightRole,
        TwoRowsRole,
        ItemRole,
        OtherItemRole,
        ItemNameRole,
        OtherItemNameRole,
        ButtonRole,
        PointXRole,
        PointYRole,
        ColorRole,
        ValueRole,
        ComparisonRole,
        ProblemRole,
        DraggedRole,
        EmptyCavityRole,
        ProblemHeightRole,
        VariableRole,
        SoundRole,
        AnimationRole,
        FlippedRole,
        SceneRole,
    };

    explicit LogicDocument(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Opens logic.json, or starts empty when the file does not exist; nothing is written until the
    // first change. A file that cannot be read is reported and never saved over.
    bool open(const QString& path);
    void close();
    void setScene(const Scene& scene);
    // The whole project and the scene being edited. Logic belongs per scene, so changing scene shows that scene's blocks.
    void setSceneSet(const SceneSet& set, int currentIndex);
    QVariantList scenes() const; // [{id, name}], for the Go to scene block
    // Copies the blocks of one scene under a new scene's id, with new block ids, for a duplicated scene.
    bool duplicateSceneLogic(const QString& sceneId, const QString& copyId);
    const LogicProgram& program() const;

    bool isLoaded() const;
    bool isEmpty() const;
    int count() const;
    bool canUndo() const;
    QString error() const;
    QString loadError() const;
    int problemCount() const;
    QString programProblem() const;
    QString firstProblemId() const;
    QString selectedId() const;
    void setSelectedId(const QString& id);
    QVariantList palette() const;
    QVariantList buttons() const;
    QVariantList comparisons() const;
    QVariantList variables() const; // [{name, initial, id}], id repeating the name for pickers
    QVariantList sounds() const; // [{name, source, id}], id repeating the name for pickers
    QVariantList sceneItems() const;
    double canvasWidth() const;
    void setCanvasWidth(double width);
    double contentWidth() const;
    double contentHeight() const;
    bool isDragging() const;
    QVariantMap landing() const;

    // Reopens a file that could not be read, once the person has fixed it.
    Q_INVOKABLE void reopen();

    // The keyboard path and a click on the palette: an event goes at the end of the program, an action
    // after the selected block, or last in the last event. Returns the new block's id, or nothing.
    Q_INVOKABLE QString addFromPalette(const QString& type);
    Q_INVOKABLE QString addBlock(const QString& type, const QString& parentId, int index);
    Q_INVOKABLE void removeBlock(const QString& id);
    Q_INVOKABLE bool setField(const QString& id, const QString& key, const QVariant& value);
    Q_INVOKABLE void undo();

    // The Variables area. Each refuses a bad name or value with a message in error.
    Q_INVOKABLE bool addVariable(const QString& name);
    Q_INVOKABLE bool renameVariable(const QString& name, const QString& newName); // updates every block
    Q_INVOKABLE bool setVariableInitial(const QString& name, const QVariant& value);
    // Blocks using it become problems to fix, so this is for after the person has been told.
    Q_INVOKABLE void removeVariable(const QString& name);
    // Everything that shows or changes the variable: [{kind: block|text, title, where}].
    Q_INVOKABLE QVariantList variableUses(const QString& name) const;

    // The Sounds area. The file itself stays in assets/sounds, so undo can bring a removed sound back.
    bool addSound(const QString& name, const QString& source);
    Q_INVOKABLE void removeSound(const QString& name);
    Q_INVOKABLE QVariantList soundUses(const QString& name) const; // [{kind: block, title, where}]
    // Shows a message from outside the document, such as a failed sound import.
    Q_INVOKABLE void reportProblem(const QString& message);

    // The block after and before this one in reading order, for moving with the keyboard.
    Q_INVOKABLE QString nextBlockId(const QString& id) const;
    Q_INVOKABLE QString previousBlockId(const QString& id) const;
    Q_INVOKABLE QVariantMap positionOf(const QString& id) const;

    // Dragging. x and y are the carried block's top left corner on the canvas. Every other block
    // moves aside to show where it would land, but the program does not change until the drop.
    Q_INVOKABLE void startMoving(const QString& id);
    Q_INVOKABLE void startAdding(const QString& type);
    Q_INVOKABLE void dragTo(double x, double y);
    Q_INVOKABLE QString drop(); // returns the first dropped block's id, or nothing when nothing landed
    Q_INVOKABLE void dropOnPalette();
    Q_INVOKABLE void cancelDrag();

signals:
    void loadedChanged();
    void programChanged();
    void undoChanged();
    void errorChanged();
    void problemsChanged();
    void selectionChanged();
    void sceneItemsChanged();
    void sceneListChanged();
    void layoutChanged();
    void dragChanged();
    // A variable's name changed, also by undoing; text in the scene follows it.
    void variableRenamed(const QString& name, const QString& newName);

private:
    struct DragState {
        bool isNew = false;
        QString id;        // the moved block, or a placeholder when adding
        QStringList ids;   // every block being carried
        QList<Block> stack;
        LogicProgram remainder;
        LogicLayout remainderLayout;
        QList<LogicLanding> spots;
        std::optional<LogicLanding> spot;
    };

    Block makeBlock(const BlockInfo& info, const QString& id) const;
    LogicProgram withInserted(const LogicProgram& base, const QList<Block>& blocks, const QString& parentId, int index) const;
    QString insertInto(const QString& type, const QString& parentId, int index);
    bool commit(const LogicProgram& candidate);
    void afterChange();
    void showLogicOfScene(const QString& sceneId);
    QStringList projectSceneIds() const;
    void refreshProblems();
    void applyLayout(const LogicLayout& next);
    void syncRows();
    LogicLayout computeLayout(const LogicProgram& program) const;
    void setError(const QString& text);
    void finishDrag(bool restoreLayout);
    QString variableNameProblem(const QString& name, const QString& ignoring = {}) const;

    QString path;
    LogicProgram document;
    QList<LogicProgram> history;
    Scene scene;
    bool sceneKnown = false; // setScene has said which scene is showing
    SceneSet sceneChoices;
    bool loaded = false;
    QString message;
    QString failure;

    QStringList rows; // block ids, one per model row
    QHash<QString, const Block*> blocksById;
    QHash<QString, QString> problems;
    QString wholeProgramProblem;
    QString firstProblem;
    QString selection;

    double viewportWidth = LogicMetrics::BlockWidth + 2 * LogicMetrics::CanvasPadding;
    LogicLayout layout;
    std::optional<DragState> drag;
    QStringList carried;
};

}
