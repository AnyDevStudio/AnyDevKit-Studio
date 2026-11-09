#pragma once

#include <QColor>
#include <QQuickPaintedItem>
#include <QtQml/qqmlregistration.h>

namespace Studio {

class PixelText : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString text READ text WRITE setText NOTIFY textChanged)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY colorChanged)

public:
    explicit PixelText(QQuickItem* parent = nullptr);

    QString text() const;
    QColor color() const;
    void setText(const QString& text);
    void setColor(const QColor& color);
    void paint(QPainter* painter) override;

signals:
    void textChanged();
    void colorChanged();

private:
    QString value;
    QColor ink = Qt::white;
};

}
