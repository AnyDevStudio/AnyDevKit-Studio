#pragma once

#include <QByteArray>
#include <QImage>
#include <QList>
#include <QRect>
#include <QSize>
#include <QString>

namespace Studio {

enum class GameImageKind { Icon, Background, Splash };

struct GameImageTarget {
    GameImageKind kind;
    QString id;
    QString label;
    QString fileName;
    QSize size;
};

GameImageTarget gameImageTarget(GameImageKind kind);
QList<GameImageTarget> gameImageTargets();

// The largest centered rectangle of source that has the target's proportions.
QRect centerCropRect(const QSize& source, const QSize& target);

// Crops to the target's proportions, scales to its exact size and flattens transparency onto black.
QImage fitGameImage(const QImage& source, const QSize& target);

// Copies Studio's pictures into a project's sce_sys folder, leaving any picture already there.
// Returns a message for the person using Studio, or an empty string on success.
QString installDefaultGamePictures(const QString& sourceFolder, const QString& targetFolder);

struct TitleEdit {
    QByteArray json;
    QString error;
};

// Returns param.json with the default language's title replaced. Everything else, including
// formatting and field order, stays as it was. On failure, json is empty and error says why.
TitleEdit withDefaultTitle(const QByteArray& json, const QString& title);

// Each returns a message for the person using Studio, or an empty string on success.
QString installGameImage(const QString& sourcePath, const QString& projectRoot, const QString& systemFolder, const GameImageTarget& target);
QString removeGameImage(const QString& projectRoot, const QString& systemFolder, const GameImageTarget& target);

}
