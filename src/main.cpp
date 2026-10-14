#include <QCommandLineParser>
#include <QDir>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlExpression>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QFileInfo>
#include <QTimer>
#include <QDebug>

#include "core/AppCore.h"

static void loadFonts()
{
    const QStringList files = {
        QStringLiteral(":/assets/fonts/NotoSans-Regular.ttf"),
        QStringLiteral(":/assets/fonts/NotoSans-Bold.ttf"),
        QStringLiteral(":/assets/fonts/NotoSansMono-Regular.ttf"),
    };
    for (const QString &f : files) {
        if (QFontDatabase::addApplicationFont(f) < 0)
            qWarning() << "font not loaded:" << f;
    }

    QFont font(QStringLiteral("Noto Sans"));
    font.setPixelSize(12);
    font.setHintingPreference(QFont::PreferNoHinting);
    QGuiApplication::setFont(font);
}

int main(int argc, char *argv[])
{
    QGuiApplication::setApplicationName(QStringLiteral("CyberHandStudio"));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("CyberHand Studio"));
    QGuiApplication::setOrganizationName(QStringLiteral("CyberHand"));
    QGuiApplication::setApplicationVersion(QStringLiteral(CH_VERSION));
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    QGuiApplication app(argc, argv);
    app.setWindowIcon(QIcon(QStringLiteral(":/assets/icon.png")));
    loadFonts();

    QCommandLineParser args;
    args.setApplicationDescription(QStringLiteral("CyberHand Studio"));
    args.addHelpOption();
    args.addVersionOption();
    QCommandLineOption portOpt(QStringLiteral("port"), QStringLiteral("Open this serial port at start"), QStringLiteral("name"));
    QCommandLineOption port2Opt(QStringLiteral("port2"), QStringLiteral("2-port mode: open this serial port as port 2"), QStringLiteral("name"));
    QCommandLineOption canOpt(QStringLiteral("can"), QStringLiteral("CAN mode: open udp[:port] (fw_sim --can-udp) or waveshare[:device] at start"), QStringLiteral("spec"));
    QCommandLineOption baudOpt(QStringLiteral("baud"), QStringLiteral("Baud rate"), QStringLiteral("rate"), QStringLiteral("115200"));
    QCommandLineOption configDirOpt(QStringLiteral("config-dir"), QStringLiteral("Folder for config profiles"), QStringLiteral("dir"));
    QCommandLineOption modeOpt(QStringLiteral("mode"), QStringLiteral("Start in this mode (live, text, timeline, controller, stream, console, canmon)"), QStringLiteral("mode"));
    QCommandLineOption shotOpt(QStringLiteral("screenshot"), QStringLiteral("Save a screenshot to file and quit"), QStringLiteral("file"));
    QCommandLineOption shotDelayOpt(QStringLiteral("screenshot-delay"), QStringLiteral("Delay before screenshot (ms)"), QStringLiteral("ms"), QStringLiteral("2500"));
    QCommandLineOption scriptOpt(QStringLiteral("run-macro"), QStringLiteral("Run this macro text at start (lines separated by ;)"), QStringLiteral("text"));
    QCommandLineOption playOpt(QStringLiteral("play"), QStringLiteral("Load a trajectory CSV and play it"), QStringLiteral("file"));
    QCommandLineOption evalOpt(QStringLiteral("eval"), QStringLiteral("Evaluate a QML/JS expression after start (repeatable), e.g. \"Core.stream.start()\""), QStringLiteral("js"));
    args.addOptions({portOpt, port2Opt, canOpt, baudOpt, configDirOpt, modeOpt, shotOpt, shotDelayOpt, scriptOpt, playOpt, evalOpt});
    args.process(app);

    AppCore core(args.value(configDirOpt));
    core.registerQmlTypes();

    QQmlApplicationEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/"));
    // deployed layout (windeployqt): <app>/qml next to the exe
    engine.addImportPath(QCoreApplication::applicationDirPath() + QStringLiteral("/qml"));
    engine.rootContext()->setContextProperty(QStringLiteral("StartMode"), args.value(modeOpt));
    engine.load(QUrl(QStringLiteral("qrc:/CyberHand/qml/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return 1;

    if (args.isSet(portOpt)) {
        core.openPortAtStart(args.value(portOpt), args.value(baudOpt).toInt());
    }
    if (args.isSet(port2Opt))
        core.openPort2AtStart(args.value(port2Opt));
    if (args.isSet(canOpt))
        core.openCanAtStart(args.value(canOpt));
    if (args.isSet(scriptOpt)) {
        QString text = args.value(scriptOpt);
        text.replace(QLatin1Char(';'), QLatin1Char('\n'));
        QTimer::singleShot(600, &core, [&core, text]() { core.runMacroText(text); });
    }
    if (args.isSet(playOpt)) {
        const QString file = args.value(playOpt);
        QTimer::singleShot(400, &core, [&core, file]() { core.playFile(file); });
    }

    if (args.isSet(evalOpt)) {
        QObject *rootObj = engine.rootObjects().first();
        const QStringList exprs = args.values(evalOpt);
        QTimer::singleShot(500, rootObj, [rootObj, exprs]() {
            for (const QString &e : exprs) {
                QQmlExpression expr(qmlContext(rootObj), rootObj, e);
                bool undef = false;
                expr.evaluate(&undef);
                if (expr.hasError())
                    qWarning() << "eval error:" << expr.error().toString();
            }
        });
    }

    if (args.isSet(shotOpt)) {
        const QString file = args.value(shotOpt);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QTimer::singleShot(args.value(shotDelayOpt).toInt(), &app, [window, file]() {
            if (window) {
                const QImage img = window->grabWindow();
                img.save(file);
            }
            // extra tool windows (Manual control, Calibration, Data monitor): file_<objectName>.png
            const QFileInfo fi(file);
            for (QWindow *w : QGuiApplication::topLevelWindows()) {
                auto *qw = qobject_cast<QQuickWindow *>(w);
                if (!qw || qw == window || !qw->isVisible() || qw->objectName().isEmpty())
                    continue;
                qw->grabWindow().save(fi.path() + QLatin1Char('/') + fi.completeBaseName() + QLatin1Char('_')
                                      + qw->objectName() + QLatin1Char('.') + fi.suffix());
            }
            QGuiApplication::quit();
        });
    }

    return app.exec();
}
