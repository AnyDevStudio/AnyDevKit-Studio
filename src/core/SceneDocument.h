#pragma once

#include "Scene.h"

#include <QAbstractListModel>
#include <QHash>
#include <QUrl>
#include <QSet>
#include <QVariantList>
#include <QVariantMap>

namespace Studio {

// The scene being edited: a list model for the stage, with undo and autosave.
class SceneDocument : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(bool loaded READ isLoaded NOTIFY loadedChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(QString background READ background NOTIFY backgroundChanged)
    Q_PROPERTY(int selectedIndex READ selectedIndex WRITE setSelectedIndex NOTIFY selectionChanged)
    Q_PROPERTY(QVariantMap selected READ selected NOTIFY selectionChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QString notice READ notice NOTIFY noticeChanged)
    Q_PROPERTY(bool noticeIsProblem READ noticeIsProblem NOTIFY noticeChanged)
    Q_PROPERTY(QString imageWarning READ imageWarning NOTIFY imagesChanged)
    Q_PROPERTY(QStringList variableNames READ variableNames NOTIFY variablesChanged)
    Q_PROPERTY(int worldWidth READ worldWidth NOTIFY worldChanged)
    Q_PROPERTY(int worldHeight READ worldHeight NOTIFY worldChanged)
    Q_PROPERTY(double cameraX READ cameraX NOTIFY worldChanged)
    Q_PROPERTY(double cameraY READ cameraY NOTIFY worldChanged)
    Q_PROPERTY(QUrl selectedTilesetUrl READ selectedTilesetUrl NOTIFY selectionChanged)
    Q_PROPERTY(QVariantList sceneList READ sceneList NOTIFY scenesChanged)
    Q_PROPERTY(int currentSceneIndex READ currentSceneIndex NOTIFY scenesChanged)
    Q_PROPERTY(int startSceneIndex READ startSceneIndex NOTIFY scenesChanged)
    Q_PROPERTY(QString currentSceneName READ currentSceneName NOTIFY scenesChanged)

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        TypeRole,
        NameRole,
        XRole,
        YRole,
        WidthRole,
        HeightRole,
        ColorRole,
        TextRole,
        ScaleRole,
        ControllableRole,
        ParticlesRole, // a map with every particles setting, for the stage preview
        SourceRole,
        ImageUrlRole,
        ImageProblemRole,
        HiddenRole,
        ScreenFixedRole,
        SpriteRole,  // a map for the stage preview: frame size, flip, pixelated and the frames to play
        TilemapRole, // a map for the stage: tile size, scale, columns and rows
    };

    explicit SceneDocument(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool open(const QString& path);
    void close();
    void setError(const QString& message);
    const Scene& scene() const; // the scene being edited
    SceneSet sceneSet() const;  // every scene of the project, as it stands now

    bool isLoaded() const;
    int count() const;
    QString background() const;
    int selectedIndex() const;
    void setSelectedIndex(int index);
    QVariantMap selected() const;
    bool canUndo() const;
    QString error() const;
    QString notice() const;
    bool noticeIsProblem() const;
    QString imageWarning() const;
    QString roomProblem() const;
    void setNotice(const QString& text, bool problem);
    QStringList variableNames() const;
    QVariantList sceneList() const; // [{id, name, isStart}], in list order
    int currentSceneIndex() const;
    int startSceneIndex() const;
    QString currentSceneName() const;
    int worldWidth() const;
    int worldHeight() const;
    double cameraX() const;
    double cameraY() const;
    QUrl selectedTilesetUrl() const;

    // What the logic's variables are and start at, for text that shows them on the stage.
    void setVariables(const QList<LogicVariable>& variables);
    // Rewrites {name} in every text, including the undo history, so undo cannot bring the old name back.
    void renameVariable(const QString& name, const QString& newName);

    // `source` is a file already inside the project; StudioController imports it first because
    // decoding needs Qt Gui, which this library stays free of.
    int addImage(const QString& source, int pixelWidth, int pixelHeight, const QString& name, double centerX, double centerY);
    bool replaceImage(int index, const QString& source, int pixelWidth, int pixelHeight);
    QString projectRoot() const;

    // A tilemap over the whole world, every cell empty. `source` is a file already in the project.
    int addTilemap(const QString& source, int pixelWidth, int pixelHeight, const QString& name, double centerX, double centerY);

    Q_INVOKABLE int addItem(const QString& type, double centerX, double centerY);
    Q_INVOKABLE void beginChange();
    Q_INVOKABLE void endChange();
    Q_INVOKABLE void moveItem(int index, double x, double y);
    Q_INVOKABLE void nudgeItem(int index, double deltaX, double deltaY);
    Q_INVOKABLE void resizeItem(int index, double width, double height, bool keepAspect = false);
    Q_INVOKABLE bool setItemValue(int index, const QString& property, const QVariant& value);
    Q_INVOKABLE void setBackground(const QString& color);
    Q_INVOKABLE void removeItem(int index);
    // Later items are drawn on top. Each of these is one undo step and false means nothing moved.
    Q_INVOKABLE bool reorderItem(int index, int newIndex);
    // `gap` counts in the layers panel, which lists front-most first: 0 is above its top row.
    Q_INVOKABLE bool reorderItemToGap(int index, int gap);
    Q_INVOKABLE bool bringItemForward(int index);
    Q_INVOKABLE bool sendItemBackward(int index);
    Q_INVOKABLE bool bringItemToFront(int index);
    Q_INVOKABLE bool sendItemToBack(int index);
    // Returns the new row, right above the source, or -1 after explaining why not in the notice.
    Q_INVOKABLE int duplicateItem(int index);
    // Editor only: hides an item on the stage. Nothing here reaches scene.json or the undo history.
    Q_INVOKABLE void setItemHidden(int index, bool hidden);
    Q_INVOKABLE bool isItemHidden(int index) const;
    Q_INVOKABLE void undo();
    // Painting a tilemap. Inside beginChange/endChange a drag is one undo step and saves at the end;
    // alone, each call is one undo step. false means the item is not a tilemap or the cell is off the map.
    Q_INVOKABLE bool paintTile(int index, int column, int row, int tile);
    Q_INVOKABLE int tileAt(int index, int column, int row) const; // -1 for empty or off the map
    // Fills the area of connected equal tiles starting at this cell. Returns how many tiles changed.
    Q_INVOKABLE int fillTiles(int index, int column, int row, int tile);
    // Marks a tile index solid, or not, for later collision. Kept per tilemap, sorted.
    Q_INVOKABLE bool setTileSolid(int index, int tile, bool solid);
    Q_INVOKABLE bool isTileSolid(int index, int tile) const;
    Q_INVOKABLE QVariantList solidTiles(int index) const;
    // Named animations of a sprite sheet. Each is one undo step and false means nothing changed.
    Q_INVOKABLE bool addAnimation(int index, const QString& name);
    Q_INVOKABLE bool removeAnimation(int index, const QString& name);
    Q_INVOKABLE bool setAnimationFrames(int index, const QString& name, const QVariantList& frames);
    Q_INVOKABLE bool setAnimationFps(int index, const QString& name, int fps);
    // The world size clamps the camera and the items into the new area. Refused sizes change nothing.
    Q_INVOKABLE bool setWorldSize(int width, int height);
    // Where the camera starts, kept inside the world. Inside beginChange/endChange it is one undo step.
    Q_INVOKABLE void setCameraStart(double x, double y);
    // Looks again at the image files on disk, so the stage follows files that went missing or came back.
    Q_INVOKABLE void recheckImages();
    Q_INVOKABLE void clearNotice();
    // The scene list. Each changes the project and saves it, as one undo step. -1 or false means nothing changed.
    Q_INVOKABLE int addScene();
    Q_INVOKABLE bool renameScene(int index, const QString& name);
    Q_INVOKABLE int duplicateScene(int index);
    Q_INVOKABLE bool deleteScene(int index);
    Q_INVOKABLE bool setStartScene(int index);
    // Which scene the stage shows. Not an edit, so it is not an undo step.
    Q_INVOKABLE bool selectScene(int index);

signals:
    void loadedChanged();
    void countChanged();
    void backgroundChanged();
    void selectionChanged();
    void undoChanged();
    void errorChanged();
    void noticeChanged();
    void imagesChanged();
    void variablesChanged();
    void worldChanged();
    void scenesChanged();
    // A scene's logic follows it, so the logic document copies its blocks under the new id.
    void sceneDuplicated(const QString& scene, const QString& copy);

private:
    bool isValidIndex(int index) const;
    bool isTilemap(int index) const;
    QSize itemBounds(const SceneItem& item) const; // the world, or the screen for a screen-fixed item
    bool changeTilemapCell(SceneItem& item, int column, int row, int tile) const;
    bool commitItem(int index, const SceneItem& item);
    double drawnWidth(const SceneItem& item) const;
    void pushUndo();
    bool commit(const Scene& candidate);
    bool persist();
    void showScene(int index);
    QString newSceneId() const;
    QString newSceneName() const;
    int indexOfSceneId(const QString& id) const;
    void notifyItem(int index);
    void placeInsideStage(SceneItem& item) const;
    int insertItem(SceneItem item);
    bool refreshImageProblems();
    QString nextItemId() const;
    int indexOfItem(const QString& id) const;

    struct UndoStep {
        SceneSet set;
        int current = 0;
    };

    QString path;
    SceneSet scenes;
    int currentIndex = 0;
    Scene document; // the scene being edited; set.scenes[currentIndex] matches it outside of a drag
    QList<UndoStep> history;
    bool loaded = false;
    bool changing = false;
    bool changedDuringChange = false;
    int selection = -1;
    QString message;
    QString noticeText;
    bool noticeProblem = false;
    QHash<QString, QString> imageProblems; // by source, only images that cannot be used
    QSet<QString> hiddenItems; // by id, so it follows an item through reorders and undo
    QList<LogicVariable> variables;
};

}
