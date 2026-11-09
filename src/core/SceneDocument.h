#pragma once

#include "Scene.h"

#include <QAbstractListModel>
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

    Q_INVOKABLE int addItem(const QString& type, double centerX, double centerY);
    Q_INVOKABLE void beginChange();
    Q_INVOKABLE void endChange();
    Q_INVOKABLE void moveItem(int index, double x, double y);
    Q_INVOKABLE void nudgeItem(int index, double deltaX, double deltaY);
    Q_INVOKABLE void resizeItem(int index, double width, double height);
    Q_INVOKABLE bool setItemValue(int index, const QString& property, const QVariant& value);
    Q_INVOKABLE void setBackground(const QString& color);
    Q_INVOKABLE void removeItem(int index);
    Q_INVOKABLE void undo();

signals:
    void loadedChanged();
    void countChanged();
    void backgroundChanged();
    void selectionChanged();
    void undoChanged();
    void errorChanged();

private:
    bool isValidIndex(int index) const;
    void pushUndo();
    bool commit(const Scene& candidate);
    void notifyItem(int index);
    void placeInsideStage(SceneItem& item) const;
    QString nextItemId() const;

    QString path;
    Scene document;
    QList<Scene> history;
    bool loaded = false;
    bool changing = false;
    bool changedDuringChange = false;
    int selection = -1;
    QString message;
};

}
