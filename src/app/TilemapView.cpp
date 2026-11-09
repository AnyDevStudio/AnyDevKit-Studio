#include "TilemapView.h"

#include <QColor>
#include <QPainter>
#include <QPen>

#include <algorithm>
#include <cmath>

namespace Studio {

TilemapView::TilemapView(QQuickItem* parent) : QQuickPaintedItem(parent) {
    setRenderTarget(QQuickPaintedItem::Image);
    setAntialiasing(false);
    setSmooth(false);
    setMipmap(false);
}

SceneDocument* TilemapView::document() const { return scene; }

void TilemapView::setDocument(SceneDocument* document) {
    if (document == scene) return;
    if (scene) disconnect(scene, nullptr, this, nullptr);
    scene = document;
    if (scene) connect(scene, &QAbstractItemModel::dataChanged, this, [this](const QModelIndex& first, const QModelIndex& last) { changed(first, last); });
    if (scene) connect(scene, &QAbstractItemModel::modelReset, this, [this] { update(); });
    emit documentChanged();
    update();
}

int TilemapView::itemIndex() const { return index; }

void TilemapView::setItemIndex(int value) {
    if (value == index) return;
    index = value;
    emit itemIndexChanged();
    update();
}

QUrl TilemapView::source() const { return url; }

void TilemapView::setSource(const QUrl& value) {
    if (value == url) return;
    url = value;
    tileset = url.isValid() && !url.isEmpty() ? QImage(url.toLocalFile()) : QImage();
    emit sourceChanged();
    update();
}

qreal TilemapView::mapX() const { return originX; }
qreal TilemapView::mapY() const { return originY; }
qreal TilemapView::unit() const { return pixelUnit; }
bool TilemapView::grid() const { return showGrid; }
bool TilemapView::showSolid() const { return solidMarks; }

void TilemapView::setMapX(qreal value) {
    if (qFuzzyCompare(value, originX)) return;
    originX = value;
    emit viewChanged();
    update();
}

void TilemapView::setMapY(qreal value) {
    if (qFuzzyCompare(value, originY)) return;
    originY = value;
    emit viewChanged();
    update();
}

void TilemapView::setUnit(qreal value) {
    if (qFuzzyCompare(value, pixelUnit)) return;
    pixelUnit = value;
    emit viewChanged();
    update();
}

void TilemapView::setGrid(bool value) {
    if (value == showGrid) return;
    showGrid = value;
    emit viewChanged();
    update();
}

void TilemapView::setShowSolid(bool value) {
    if (value == solidMarks) return;
    solidMarks = value;
    emit viewChanged();
    update();
}

void TilemapView::changed(const QModelIndex& first, const QModelIndex& last) {
    if (index >= first.row() && index <= last.row()) update();
}

void TilemapView::paint(QPainter* painter) {
    if (!scene || index < 0 || index >= scene->count()) return;
    const auto& item = scene->scene().items[index];
    if (item.type != "tilemap" || tileset.isNull() || item.tiles.size() != qint64(item.columns) * item.rows) return;
    const auto cellWidth = item.tileWidth * item.tileScale * pixelUnit;
    const auto cellHeight = item.tileHeight * item.tileScale * pixelUnit;
    if (cellWidth <= 0 || cellHeight <= 0) return;
    const auto columns = tileset.width() / item.tileWidth;
    const auto rows = tileset.height() / item.tileHeight;
    if (columns <= 0 || rows <= 0) return;

    // Only the cells inside the view are looked at, so painting stays cheap on big maps.
    const auto firstColumn = std::max(0, static_cast<int>(std::floor(-originX / cellWidth)));
    const auto lastColumn = std::min(item.columns - 1, static_cast<int>(std::floor((width() - originX) / cellWidth)));
    const auto firstRow = std::max(0, static_cast<int>(std::floor(-originY / cellHeight)));
    const auto lastRow = std::min(item.rows - 1, static_cast<int>(std::floor((height() - originY) / cellHeight)));

    painter->setRenderHint(QPainter::SmoothPixmapTransform, false);
    for (int row = firstRow; row <= lastRow; ++row) {
        for (int column = firstColumn; column <= lastColumn; ++column) {
            const auto tile = item.tiles[row * item.columns + column];
            if (tile < 0 || tile / columns >= rows) continue;
            const QRectF target(originX + column * cellWidth, originY + row * cellHeight, cellWidth, cellHeight);
            const QRectF source((tile % columns) * item.tileWidth, (tile / columns) * item.tileHeight, item.tileWidth, item.tileHeight);
            painter->drawImage(target, tileset, source);
            if (solidMarks && item.solidTiles.contains(tile)) painter->fillRect(target, QColor(255, 211, 63, 90));
        }
    }

    if (!showGrid || cellWidth < 8 || cellHeight < 8) return;
    painter->setPen(QPen(QColor(255, 255, 255, 40), 1));
    for (int column = firstColumn; column <= lastColumn + 1; ++column) {
        const auto x = originX + column * cellWidth;
        painter->drawLine(QPointF(x, originY + firstRow * cellHeight), QPointF(x, originY + (lastRow + 1) * cellHeight));
    }
    for (int row = firstRow; row <= lastRow + 1; ++row) {
        const auto y = originY + row * cellHeight;
        painter->drawLine(QPointF(originX + firstColumn * cellWidth, y), QPointF(originX + (lastColumn + 1) * cellWidth, y));
    }
}

}
