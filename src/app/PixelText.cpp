#include "PixelText.h"

#include "PixelFont.h"

#include <QPainter>

namespace Studio {

PixelText::PixelText(QQuickItem* parent) : QQuickPaintedItem(parent) {
    setAntialiasing(false);
}

QString PixelText::text() const { return value; }
QColor PixelText::color() const { return ink; }

void PixelText::setText(const QString& text) {
    if (text == value) return;
    value = text;
    emit textChanged();
    update();
}

void PixelText::setColor(const QColor& color) {
    if (color == ink) return;
    ink = color;
    emit colorChanged();
    update();
}

void PixelText::paint(QPainter* painter) {
    if (value.isEmpty()) return;
    const auto unit = width() / (value.size() * 6 - 1);
    for (qsizetype index = 0; index < value.size(); ++index) {
        const auto rows = pixelGlyph(value[index].toLatin1());
        for (int row = 0; row < 7; ++row) {
            for (int bit = 0; bit < 5; ++bit) {
                if (rows[row] & (1u << (4 - bit))) painter->fillRect(QRectF((index * 6 + bit) * unit, row * unit, unit, unit), ink);
            }
        }
    }
}

}
