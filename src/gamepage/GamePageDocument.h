#pragma once

#include "GamePage.h"

#include <QObject>
#include <QString>
#include <QUrl>

class QThread;

namespace Studio {

// What the Game page edits: the console name and images of the open project.
class GamePageDocument : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool loaded READ isLoaded NOTIFY changed)
    Q_PROPERTY(bool busy READ isBusy NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QUrl iconUrl READ iconUrl NOTIFY changed)
    Q_PROPERTY(QUrl backgroundUrl READ backgroundUrl NOTIFY changed)
    Q_PROPERTY(QUrl splashUrl READ splashUrl NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)

public:
    explicit GamePageDocument(QObject* parent = nullptr);
    ~GamePageDocument() override;

    bool isLoaded() const;
    bool isBusy() const;
    QString title() const;
    QUrl iconUrl() const;
    QUrl backgroundUrl() const;
    QUrl splashUrl() const;
    QString status() const;
    QString error() const;

    void open(const QString& root, const QString& app0Path, const QString& paramPath, const QString& titleName);
    void close();
    void setBuilding(bool building);

    Q_INVOKABLE void setTitle(const QString& text);
    Q_INVOKABLE void chooseImage(const QString& kind, const QUrl& source);
    Q_INVOKABLE void removeSplash();

signals:
    void changed();
    void titleSaved(const QString& title);
    void filesChanged();

private:
    QString systemFolder() const;
    QUrl imageUrl(GameImageKind kind) const;
    void refreshUrls();
    void waitForWorker();
    void report(const QString& newStatus, const QString& newError);

    QString root;
    QString app0;
    QString param;
    QString currentTitle;
    QString currentStatus;
    QString currentError;
    QUrl icon;
    QUrl background;
    QUrl splash;
    QThread* worker = nullptr;
    int revision = 0;
    int generation = 0;
    bool isBuilding = false;
};

}
