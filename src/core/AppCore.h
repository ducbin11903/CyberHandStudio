#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>

#include "ConfigStore.h"
#include "ConsoleModel.h"
#include "GamepadInput.h"
#include "HandController.h"
#include "HandModel.h"
#include "LiveSeries.h"
#include "LlmClient.h"
#include "MacroRunner.h"
#include "Player.h"
#include "PoseLibrary.h"
#include "SerialLink.h"
#include "StreamServer.h"
#include "Telemetry.h"
#include "TimelineModel.h"
#include "Trajectory.h"

// Owns every backend object and wires them together. Exposed to QML as the `Core` singleton.
class AppCore : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(SerialLink *link READ link CONSTANT)
    Q_PROPERTY(ConsoleModel *console READ console CONSTANT)
    Q_PROPERTY(HandModel *hand READ hand CONSTANT)
    Q_PROPERTY(Telemetry *telemetry READ telemetry CONSTANT)
    Q_PROPERTY(HandController *ctrl READ ctrl CONSTANT)
    Q_PROPERTY(LiveSeries *live READ live CONSTANT)
    Q_PROPERTY(LiveSeries *streamSeries READ streamSeries CONSTANT)
    Q_PROPERTY(Trajectory *textTrajectory READ textTrajectory CONSTANT)
    Q_PROPERTY(Trajectory *timelineTrajectory READ timelineTrajectory CONSTANT)
    Q_PROPERTY(TimelineModel *timeline READ timeline CONSTANT)
    Q_PROPERTY(Player *player READ player CONSTANT)
    Q_PROPERTY(StreamServer *stream READ stream CONSTANT)
    Q_PROPERTY(GamepadInput *gamepad READ gamepad CONSTANT)
    Q_PROPERTY(PoseLibrary *poses READ poses CONSTANT)
    Q_PROPERTY(LlmClient *llm READ llm CONSTANT)
    Q_PROPERTY(MacroRunner *macros READ macros CONSTANT)
    Q_PROPERTY(ConfigStore *config READ config CONSTANT)
    Q_PROPERTY(QString activeMode READ activeMode WRITE setActiveMode NOTIFY activeModeChanged)
    Q_PROPERTY(QVariantMap ui READ ui WRITE setUi NOTIFY uiChanged)
    Q_PROPERTY(QUrl documentsFolder READ documentsFolder CONSTANT)

public:
    explicit AppCore(const QString &configDir = QString(), QObject *parent = nullptr);
    ~AppCore() override;

    void registerQmlTypes();
    void openPortAtStart(const QString &port, int baud);
    void runMacroText(const QString &text);
    void playFile(const QString &file);

    QString version() const { return QStringLiteral(CH_VERSION); }
    SerialLink *link() const { return m_link; }
    ConsoleModel *console() const { return m_console; }
    HandModel *hand() const { return m_hand; }
    Telemetry *telemetry() const { return m_telemetry; }
    HandController *ctrl() const { return m_ctrl; }
    LiveSeries *live() const { return m_live; }
    LiveSeries *streamSeries() const { return m_streamSeries; }
    Trajectory *textTrajectory() const { return m_textTraj; }
    Trajectory *timelineTrajectory() const { return m_timelineTraj; }
    TimelineModel *timeline() const { return m_timeline; }
    Player *player() const { return m_player; }
    StreamServer *stream() const { return m_stream; }
    GamepadInput *gamepad() const { return m_gamepad; }
    PoseLibrary *poses() const { return m_poses; }
    LlmClient *llm() const { return m_llm; }
    MacroRunner *macros() const { return m_macros; }
    ConfigStore *config() const { return m_config; }
    QString activeMode() const { return m_mode; }
    void setActiveMode(const QString &m);
    QVariantMap ui() const { return m_ui; }
    void setUi(const QVariantMap &u);
    QUrl documentsFolder() const;

    Q_INVOKABLE void setUiValue(const QString &key, const QVariant &value);
    Q_INVOKABLE QVariant uiValue(const QString &key, const QVariant &fallback = QVariant()) const;
    Q_INVOKABLE QString fileName(const QString &pathOrUrl) const;
    Q_INVOKABLE void saveNow() { m_config->save(); }

signals:
    void activeModeChanged();
    void uiChanged();

private:
    void onLine(const QString &line, double t);
    QJsonObject collect() const;
    void apply(const QJsonObject &o);
    void installExamples();

    SerialLink *m_link;
    ConsoleModel *m_console;
    HandModel *m_hand;
    Telemetry *m_telemetry;
    HandController *m_ctrl;
    LiveSeries *m_live;
    LiveSeries *m_streamSeries;
    QElapsedTimer m_streamClock;
    Trajectory *m_textTraj;
    Trajectory *m_timelineTraj;
    TimelineModel *m_timeline;
    Player *m_player;
    StreamServer *m_stream;
    GamepadInput *m_gamepad;
    PoseLibrary *m_poses;
    LlmClient *m_llm;
    MacroRunner *m_macros;
    ConfigStore *m_config;
    QString m_mode = QStringLiteral("text");
    QVariantMap m_ui;
    QTimer m_compileTimer;
};
