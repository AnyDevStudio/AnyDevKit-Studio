#include "ExportFormat.h"
#include "ReducedMotion.h"
#include "StudioController.h"

#include <QCommandLineParser>
#include <QFile>
#include <QGuiApplication>
#include <QIcon>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSettings>
#include <QStringListModel>
#include <QTextStream>
#include <QTimer>

int main(int argc, char** argv) {
    QGuiApplication application(argc, argv);
    QGuiApplication::setOrganizationName("AnyDevStudio");
    QGuiApplication::setApplicationName("AnyDevKit Studio");
    QGuiApplication::setApplicationVersion(QStringLiteral(PROJECT_VERSION_TEXT));
    QGuiApplication::setWindowIcon(QIcon(":/qt/qml/AnyDevKitStudio/assets/icon/app-icon.png"));
    QQuickStyle::setStyle("Basic");

    // Lets automated runs use their own settings and leave the person's preferences alone.
    if (const auto settingsDirectory = qEnvironmentVariable("ANYDEVKIT_STUDIO_SETTINGS_DIR"); !settingsDirectory.isEmpty()) {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory);
    }

    QCommandLineParser parser;
    parser.setApplicationDescription("Desktop studio for AnyDevKit projects.");
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption openOption("open", "Open a project folder.", "folder");
    const QCommandLineOption buildOption("build", "Build the opened project for PS5.");
    const QCommandLineOption screenshotOption("screenshot", "Save the window as an image, then exit.", "file");
    const QCommandLineOption delayOption("screenshot-delay", "Take the screenshot this many milliseconds after start, even during a build.", "ms");
    const QCommandLineOption newGameOption("new-game", "Start on the new game form.");
    const QCommandLineOption formatOption("export-format", "Export format for the opened project: ffpfsc, zip or pkg.", "format");
    const QCommandLineOption logOption("build-log", "Write the build log to this file when Studio exits.", "file");
    const QCommandLineOption runSetupOption("run-setup", "Run the guided PS5 tools setup without waiting for its button, for scripted testing.");
    const QCommandLineOption setupLogOption("setup-log", "Write the guided setup log to this file when Studio exits.", "file");
    const QCommandLineOption windowSizeOption("window-size", "Open the window at this size, for example 980x620.", "size");
    const QCommandLineOption tabOption("tab", "Open the project on this tab: scene, logic, game or build.", "tab", "scene");
    const QCommandLineOption sceneOption("scene", "Show this scene of the opened project, counting from 1.", "number");
    parser.addOptions({openOption, buildOption, screenshotOption, delayOption, newGameOption, formatOption, logOption, runSetupOption, setupLogOption, windowSizeOption, tabOption, sceneOption});
    parser.process(application);

    if (parser.isSet(formatOption) && !Studio::exportFormatFromId(parser.value(formatOption))) {
        QTextStream(stderr) << "Unknown export format: " << parser.value(formatOption) << "\n";
        return 2;
    }

    Studio::StudioController studio;
    if (parser.isSet(openOption)) studio.openPath(parser.value(openOption), parser.value(tabOption));
    if (parser.isSet(sceneOption)) studio.scene()->selectScene(parser.value(sceneOption).toInt() - 1);
    if (parser.isSet(formatOption)) studio.setExportFormat(parser.value(formatOption));

    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &application, [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.setInitialProperties({{"studio", QVariant::fromValue(&studio)}, {"reducedMotion", Studio::prefersReducedMotion()}, {"startOnNewGame", parser.isSet(newGameOption)}});
    engine.loadFromModule("AnyDevKitStudio", "Main");

    if (parser.isSet(windowSizeOption)) {
        const auto parts = parser.value(windowSizeOption).split('x');
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().value(0));
        if (window && parts.size() == 2) window->resize(parts[0].toInt(), parts[1].toInt());
    }

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

    auto setupStarted = false;
    if (parser.isSet(runSetupOption)) {
        auto* setup = studio.setup();
        QObject::connect(setup, &Studio::SetupFlow::changed, &application, [&, setup] {
            const auto phase = setup->phase();
            if (phase == "ready" && !setupStarted) {
                setupStarted = true;
                setup->start();
            } else if (phase == "done" || phase == "failed" || phase == "cancelled" || phase == "needsTools") {
                QObject::disconnect(setup, &Studio::SetupFlow::changed, &application, nullptr);
                buildSucceeded = phase == "done";
                if (parser.isSet(screenshotOption) && !parser.isSet(delayOption)) screenshot();
                else if (!parser.isSet(delayOption)) QCoreApplication::exit(buildSucceeded ? 0 : 1);
            }
        });
        if (parser.isSet(delayOption)) screenshot();
    }

    if (parser.isSet(buildOption)) {
        QObject::connect(&studio, &Studio::StudioController::buildFinished, &application, [&](bool success) {
            buildSucceeded = success;
            if (parser.isSet(screenshotOption) && !parser.isSet(delayOption)) screenshot();
        });
        QTimer::singleShot(0, &studio, &Studio::StudioController::build);
        if (parser.isSet(delayOption)) screenshot();
    } else if (parser.isSet(screenshotOption) && !parser.isSet(runSetupOption)) {
        screenshot();
    }

    const auto exitCode = application.exec();
    if (parser.isSet(logOption)) {
        QFile logFile(parser.value(logOption));
        if (logFile.open(QIODevice::WriteOnly | QIODevice::Text)) logFile.write(studio.log()->stringList().join('\n').toUtf8() + '\n');
    }
    if (parser.isSet(setupLogOption)) {
        QFile logFile(parser.value(setupLogOption));
        if (logFile.open(QIODevice::WriteOnly | QIODevice::Text)) logFile.write(studio.setup()->log()->stringList().join('\n').toUtf8() + '\n');
    }
    return exitCode;
}
