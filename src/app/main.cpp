#include "ReducedMotion.h"
#include "StudioController.h"

#include <QCommandLineParser>
#include <QGuiApplication>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>

int main(int argc, char** argv) {
    QGuiApplication application(argc, argv);
    QGuiApplication::setApplicationName("AnyDevKit Studio");
    QGuiApplication::setApplicationVersion(QStringLiteral(PROJECT_VERSION_TEXT));
    QQuickStyle::setStyle("Basic");

    QCommandLineParser parser;
    parser.setApplicationDescription("Desktop studio for AnyDevKit projects.");
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption openOption("open", "Open a project folder.", "folder");
    const QCommandLineOption buildOption("build", "Build the opened project for PS5.");
    const QCommandLineOption screenshotOption("screenshot", "Save the window as an image, then exit.", "file");
    const QCommandLineOption delayOption("screenshot-delay", "Take the screenshot this many milliseconds after start, even during a build.", "ms");
    parser.addOptions({openOption, buildOption, screenshotOption, delayOption});
    parser.process(application);

    Studio::StudioController studio;
    if (parser.isSet(openOption)) studio.openPath(parser.value(openOption));

    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &application, [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.setInitialProperties({{"studio", QVariant::fromValue(&studio)}, {"reducedMotion", Studio::prefersReducedMotion()}});
    engine.loadFromModule("AnyDevKitStudio", "Main");

    auto buildSucceeded = true;
    const auto screenshot = [&] {
        const auto file = parser.value(screenshotOption);
        const auto delay = parser.isSet(delayOption) ? parser.value(delayOption).toInt() : 1200;
        QTimer::singleShot(delay, &application, [&, file] {
            auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().value(0));
            const auto saved = window && window->grabWindow().save(file);
            QCoreApplication::exit(saved && buildSucceeded ? 0 : 1);
        });
    };

    if (parser.isSet(buildOption)) {
        QObject::connect(&studio, &Studio::StudioController::buildFinished, &application, [&](bool success) {
            buildSucceeded = success;
            if (parser.isSet(screenshotOption) && !parser.isSet(delayOption)) screenshot();
        });
        QTimer::singleShot(0, &studio, &Studio::StudioController::build);
        if (parser.isSet(delayOption)) screenshot();
    } else if (parser.isSet(screenshotOption)) {
        screenshot();
    }

    return application.exec();
}
