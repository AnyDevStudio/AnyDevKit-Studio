#pragma once

#include "BuildRunner.h"
#include "Project.h"

#include <QObject>
#include <QStringListModel>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <optional>

namespace Studio {

class StudioController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the application")
    Q_PROPERTY(bool hasProject READ hasProject NOTIFY projectChanged)
    Q_PROPERTY(QString projectName READ projectName NOTIFY projectChanged)
    Q_PROPERTY(QString titleName READ titleName NOTIFY projectChanged)
    Q_PROPERTY(QString titleId READ titleId NOTIFY projectChanged)
    Q_PROPERTY(QString contentVersion READ contentVersion NOTIFY projectChanged)
    Q_PROPERTY(QVariantList files READ files NOTIFY projectChanged)
    Q_PROPERTY(QString openError READ openError NOTIFY openErrorChanged)
    Q_PROPERTY(bool building READ building NOTIFY buildChanged)
    Q_PROPERTY(QString buildState READ buildState NOTIFY buildChanged)
    Q_PROPERTY(QString buildMessage READ buildMessage NOTIFY buildChanged)
    Q_PROPERTY(QVariantList stages READ stages NOTIFY buildChanged)
    Q_PROPERTY(QString outputName READ outputName NOTIFY buildChanged)
    Q_PROPERTY(QString outputSize READ outputSize NOTIFY buildChanged)
    Q_PROPERTY(QStringListModel* log READ log CONSTANT)

public:
    explicit StudioController(QObject* parent = nullptr);

    bool hasProject() const;
    QString projectName() const;
    QString titleName() const;
    QString titleId() const;
    QString contentVersion() const;
    QVariantList files() const;
    QString openError() const;
    bool building() const;
    QString buildState() const;
    QString buildMessage() const;
    QVariantList stages() const;
    QString outputName() const;
    QString outputSize() const;
    QStringListModel* log();

    Q_INVOKABLE bool openFolder(const QUrl& folder);
    Q_INVOKABLE void closeProject();
    Q_INVOKABLE void build();
    Q_INVOKABLE void cancelBuild();
    Q_INVOKABLE void revealOutput() const;
    Q_INVOKABLE void openFile(const QString& relativePath) const;
    Q_INVOKABLE void copyLog() const;

    bool openPath(const QString& path);

signals:
    void projectChanged();
    void openErrorChanged();
    void buildChanged();
    void buildFinished(bool success);

private:
    void resetBuild();
    void setStageState(int index, const QString& state);
    void appendLog(const QStringList& lines);

    std::optional<Project> project;
    QVariantList fileEntries;
    QString lastOpenError;
    BuildRunner runner;
    QStringListModel logModel;
    QVariantList stageEntries;
    QString state = "idle";
    QString message;
    QString size;
};

}
