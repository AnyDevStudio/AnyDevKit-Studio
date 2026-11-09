#pragma once

#include "SceneDocument.h"

#include <QImage>
#include <QPointer>
#include <QQuickPaintedItem>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

namespace Studio {

// Paints the part of a tilemap the stage can see. Painted rather than built from scene graph nodes,
// so the same view also draws in the software renderer. Only visible cells are looked at.
class TilemapView : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(Studio::SceneDocument* document READ document WRITE setDocument NOTIFY documentChanged)
    Q_PROPERTY(int itemIndex READ itemIndex WRITE setItemIndex NOTIFY itemIndexChanged)
    Q_PROPERTY(QUrl source READ source WRITE setSource NOTIFY sourceChanged)
    // Where the map's top left corner is, in stage pixels, and how big one world unit is on the stage.
    Q_PROPERTY(qreal mapX READ mapX WRITE setMapX NOTIFY viewChanged)
    Q_PROPERTY(qreal mapY READ mapY WRITE setMapY NOTIFY viewChanged)
    Q_PROPERTY(qreal unit READ unit WRITE setUnit NOTIFY viewChanged)
    Q_PROPERTY(bool grid READ grid WRITE setGrid NOTIFY viewChanged)
    Q_PROPERTY(bool showSolid READ showSolid WRITE setShowSolid NOTIFY viewChanged)

public:
    explicit TilemapView(QQuickItem* parent = nullptr);

    SceneDocument* document() const;
    void setDocument(SceneDocument* document);
    int itemIndex() const;
    void setItemIndex(int index);
    QUrl source() const;
    void setSource(const QUrl& source);
    qreal mapX() const;
    void setMapX(qreal value);
    qreal mapY() const;
    void setMapY(qreal value);
    qreal unit() const;
    void setUnit(qreal value);
    bool grid() const;
    void setGrid(bool value);
    bool showSolid() const;
    void setShowSolid(bool value);

    void paint(QPainter* painter) override;

signals:
    void documentChanged();
    void itemIndexChanged();
    void sourceChanged();
    void viewChanged();

private:
    void changed(const QModelIndex& first, const QModelIndex& last);

    QPointer<SceneDocument> scene;
    int index = -1;
    QUrl url;
    QImage tileset;
    qreal originX = 0;
    qreal originY = 0;
    qreal pixelUnit = 1;
    bool showGrid = false;
    bool solidMarks = false;
};

}
