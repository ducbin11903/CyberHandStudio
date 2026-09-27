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
    for (const QString &f : files)
        QFontDatabase::addApplicationFont(f);

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
    QCommandLineOption baudOpt(QStringLiteral("baud"), QStringLiteral("Baud rate"), QStringLiteral("rate"), QStringLiteral("115200"));
    QCommandLineOption configDirOpt(QStringLiteral("config-dir"), QStringLiteral("Folder for config profiles"), QStringLiteral("dir"));
    QCommandLineOption modeOpt(QStringLiteral("mode"), QStringLiteral("Start in this mode (live, text, timeline, controller, stream, console)"), QStringLiteral("mode"));
    QCommandLineOption shotOpt(QStringLiteral("screenshot"), QStringLiteral("Save a screenshot to file and quit"), QStringLiteral("file"));
    QCommandLineOption shotDelayOpt(QStringLiteral("screenshot-delay"), QStringLiteral("Delay before screenshot (ms)"), QStringLiteral("ms"), QStringLiteral("2500"));
    QCommandLineOption scriptOpt(QStringLiteral("run-macro"), QStringLiteral("Run this macro text at start (lines separated by ;)"), QStringLiteral("text"));
    QCommandLineOption playOpt(QStringLiteral("play"), QStringLiteral("Load a trajectory CSV and play it"), QStringLiteral("file"));
    QCommandLineOption evalOpt(QStringLiteral("eval"), QStringLiteral("Evaluate a QML/JS expression after start (repeatable), e.g. \"Core.stream.start()\""), QStringLiteral("js"));
    args.addOptions({portOpt, baudOpt, configDirOpt, modeOpt, shotOpt, shotDelayOpt, scriptOpt, playOpt, evalOpt});
    args.process(app);

    AppCore core(args.value(configDirOpt));
    core.registerQmlTypes();

    QQmlApplicationEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/"));
    engine.rootContext()->setContextProperty(QStringLiteral("StartMode"), args.value(modeOpt));
    engine.load(QUrl(QStringLiteral("qrc:/CyberHand/qml/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return 1;

    if (args.isSet(portOpt)) {
        core.openPortAtStart(args.value(portOpt), args.value(baudOpt).toInt());
    }
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
            QGuiApplication::quit();
        });
    }

    return app.exec();
}
