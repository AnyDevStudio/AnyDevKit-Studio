#include "PixelImage.h"

#include <QPainter>

#include <algorithm>

namespace Studio {

PixelImage::PixelImage(QQuickItem* parent) : QQuickPaintedItem(parent) {
    setRenderTarget(QQuickPaintedItem::Image); // the software renderer needs a plain image target
    setAntialiasing(false);
    setSmooth(false);
    setMipmap(false);
}

QUrl PixelImage::source() const { return url; }

void PixelImage::setSource(const QUrl& source) {
    if (source == url) return;
    url = source;
    image = source.isValid() && !source.isEmpty() ? QImage(source.toLocalFile()) : QImage();
    emit sourceChanged();
    update();
}

int PixelImage::frameWidth() const { return frameW; }
int PixelImage::frameHeight() const { return frameH; }
int PixelImage::frame() const { return index; }
bool PixelImage::flipped() const { return flip; }
int PixelImage::sourceWidth() const { return image.width(); }
int PixelImage::sourceHeight() const { return image.height(); }

void PixelImage::setFrameWidth(int value) {
    if (value == frameW) return;
    frameW = value;
    emit frameChanged();
    update();
}

void PixelImage::setFrameHeight(int value) {
    if (value == frameH) return;
    frameH = value;
    emit frameChanged();
    update();
}

void PixelImage::setFrame(int value) {
    if (value == index) return;
    index = value;
    emit frameChanged();
    update();
}

void PixelImage::setFlipped(bool value) {
    if (value == flip) return;
    flip = value;
    emit frameChanged();
    update();
}

void PixelImage::paint(QPainter* painter) {
    if (image.isNull() || width() <= 0 || height() <= 0) return;
    QRectF source(0, 0, image.width(), image.height());
    if (frameW > 0 && frameH > 0 && frameW <= image.width() && frameH <= image.height()) {
        const auto columns = std::max(1, image.width() / frameW);
        const auto frameX = (index % columns) * frameW;
        const auto frameY = (index / columns) * frameH;
        if (frameY + frameH <= image.height()) source = QRectF(frameX, frameY, frameW, frameH);
    }
    const auto target = QRectF(0, 0, width(), height());
    painter->setRenderHint(QPainter::SmoothPixmapTransform, false);
    if (flip) {
        painter->save();
        painter->translate(target.right(), 0);
        painter->scale(-1, 1);
        painter->drawImage(QRectF(0, 0, width(), height()), image, source);
        painter->restore();
    } else {
        painter->drawImage(target, image, source);
    }
}

}
