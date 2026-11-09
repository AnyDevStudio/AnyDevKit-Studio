#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

#include <optional>

namespace Studio {

struct SceneItem {
    QString id;
    QString type; // "rectangle" or "text"
    QString name;
    double x = 0, y = 0, width = 0, height = 0;
    QString color = "#FFD23F";
    int scale = 8;
    QString text;
    bool controllable = false;
};

struct Scene {
    QString background = "#151C3A";
    QList<SceneItem> items;
};

struct SceneLoadResult {
    std::optional<Scene> scene;
    QString error;
};

SceneLoadResult loadScene(const QString& path);
bool saveScene(const QString& path, const Scene& scene, QString& error);
QString validateScene(const Scene& scene); // empty string when valid
QByteArray generateSceneHeader(const Scene& scene); // empty when the scene is invalid
bool installSceneRuntime(const QString& runtimeDirectory, const QString& projectRoot, const QString& projectName, QString& error);

}
