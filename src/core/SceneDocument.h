#pragma once

#include "Scene.h"

#include <QAbstractListModel>
#include <QHash>
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
    };

    explicit SceneDocument(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool open(const QString& path);
    void close();
    void setError(const QString& message);
    const Scene& scene() const;

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

    // What the logic's variables are and start at, for text that shows them on the stage.
    void setVariables(const QList<LogicVariable>& variables);
    // Rewrites {name} in every text, including the undo history, so undo cannot bring the old name back.
    void renameVariable(const QString& name, const QString& newName);

    // `source` is a file already inside the project; StudioController imports it first because
    // decoding needs Qt Gui, which this library stays free of.
    int addImage(const QString& source, int pixelWidth, int pixelHeight, const QString& name, double centerX, double centerY);
    bool replaceImage(int index, const QString& source, int pixelWidth, int pixelHeight);
    QString projectRoot() const;

    Q_INVOKABLE int addItem(const QString& type, double centerX, double centerY);
    Q_INVOKABLE void beginChange();
    Q_INVOKABLE void endChange();
    Q_INVOKABLE void moveItem(int index, double x, double y);
    Q_INVOKABLE void nudgeItem(int index, double deltaX, double deltaY);
    Q_INVOKABLE void resizeItem(int index, double width, double height, bool keepAspect = false);
    Q_INVOKABLE bool setItemValue(int index, const QString& property, const QVariant& value);
    Q_INVOKABLE void setBackground(const QString& color);
    Q_INVOKABLE void removeItem(int index);
    Q_INVOKABLE void undo();
    // Looks again at the image files on disk, so the stage follows files that went missing or came back.
    Q_INVOKABLE void recheckImages();
    Q_INVOKABLE void clearNotice();

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

private:
    bool isValidIndex(int index) const;
    double drawnWidth(const SceneItem& item) const;
    void pushUndo();
    bool commit(const Scene& candidate);
    void notifyItem(int index);
    void placeInsideStage(SceneItem& item) const;
    int insertItem(SceneItem item);
    bool refreshImageProblems();
    QString nextItemId() const;

    QString path;
    Scene document;
    QList<Scene> history;
    bool loaded = false;
    bool changing = false;
    bool changedDuringChange = false;
    int selection = -1;
    QString message;
    QString noticeText;
    bool noticeProblem = false;
    QHash<QString, QString> imageProblems; // by source, only images that cannot be used
    QList<LogicVariable> variables;
};

}
