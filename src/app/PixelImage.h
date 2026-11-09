#pragma once

#include <QImage>
#include <QQuickPaintedItem>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

namespace Studio {

// One frame of a picture, drawn pixel for pixel: nearest-neighbour scaling and a flip, as the game
// draws it. Painted, so it also shows up in the software renderer.
class PixelImage : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QUrl source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(int frameWidth READ frameWidth WRITE setFrameWidth NOTIFY frameChanged)
    Q_PROPERTY(int frameHeight READ frameHeight WRITE setFrameHeight NOTIFY frameChanged)
    Q_PROPERTY(int frame READ frame WRITE setFrame NOTIFY frameChanged)
    Q_PROPERTY(bool flipped READ flipped WRITE setFlipped NOTIFY frameChanged)
    Q_PROPERTY(int sourceWidth READ sourceWidth NOTIFY sourceChanged)
    Q_PROPERTY(int sourceHeight READ sourceHeight NOTIFY sourceChanged)

public:
    explicit PixelImage(QQuickItem* parent = nullptr);

    QUrl source() const;
    void setSource(const QUrl& source);
    int frameWidth() const;
    void setFrameWidth(int value);
    int frameHeight() const;
    void setFrameHeight(int value);
    int frame() const;
    void setFrame(int value);
    bool flipped() const;
    void setFlipped(bool value);
    int sourceWidth() const;
    int sourceHeight() const;
    void paint(QPainter* painter) override;

signals:
    void sourceChanged();
    void frameChanged();

private:
    QUrl url;
    QImage image;
    int frameW = 0;
    int frameH = 0;
    int index = 0;
    bool flip = false;
};

}
