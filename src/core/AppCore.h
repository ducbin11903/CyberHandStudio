#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>

#include "BoardMonitor.h"
#include "CadModel.h"
#include "CanBus.h"
#include "Calibration.h"
#include "ConfigStore.h"
#include "ConsoleModel.h"
#include "EventLog.h"
#include "GamepadInput.h"
#include "HandController.h"
#include "HandModel.h"
#include "LiveSeries.h"
#include "LlmClient.h"
#include "MacroRunner.h"
#include "Player.h"
#include "PoseLibrary.h"
#include "Recorder.h"
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
    // second USB port (2-port mode: one port per MCU)
    Q_PROPERTY(SerialLink *link2 READ link2 CONSTANT)
    // CAN connection (Waveshare USB-CAN-FD / UDP virtual bus), MCB CAN v1
    Q_PROPERTY(CanBus *can READ can CONSTANT)
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
    Q_PROPERTY(Calibration *calibration READ calibration CONSTANT)
    Q_PROPERTY(CadModel *cad READ cad CONSTANT)
    // MCB board sensors (fw 0.9): temperature, humidity, VBAT, protection state
    Q_PROPERTY(BoardMonitor *board READ board CONSTANT)
    Q_PROPERTY(EventLog *events READ events CONSTANT)
    Q_PROPERTY(Recorder *recorder READ recorder CONSTANT)
    Q_PROPERTY(QString defaultCadFolder READ defaultCadFolder CONSTANT)
    Q_PROPERTY(double refreshHz READ refreshHz WRITE setRefreshHz NOTIFY refreshHzChanged)
    Q_PROPERTY(QString runState READ runState NOTIFY runStateChanged)
    // 3D shows the commands instead of the real hand: not connected, or the active source
    // is not sent (Timeline/Text Output off, Controller Output off, Stream without Mirror, Manual Output off)
    Q_PROPERTY(bool simulating READ simulating NOTIFY simulatingChanged)
    Q_PROPERTY(bool firmwareConfigurable READ firmwareConfigurable NOTIFY firmwareConfigurableChanged)
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
    SerialLink *link2() const { return m_link2; }
    CanBus *can() const { return m_can; }
    // "udp", "udp:7470", "udp:127.0.0.1:7470", "waveshare", "waveshare:0" (command line --can)
    void openCanAtStart(const QString &spec);
    // E-STOP: stop playback, CAN frame 0x000 (USB: D / 0 + d on every port)
    Q_INVOKABLE void estop();
    // Connect / Disconnect: port 1, plus port 2 in 2-port mode
    Q_INVOKABLE bool toggleConnection();
    void openPort2AtStart(const QString &port);
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
    Calibration *calibration() const { return m_calib; }
    CadModel *cad() const { return m_cad; }
    BoardMonitor *board() const { return m_board; }
    EventLog *events() const { return m_events; }
    Recorder *recorder() const { return m_recorder; }
    // bundled model: <app>/models/opengrasp_lite (deployed) or the source tree (dev)
    QString defaultCadFolder() const;
    // load the model folder from Visual settings ("" = bundled); false keeps the block hand
    Q_INVOKABLE bool loadCad(const QString &folder);
    // Visual Refresh Rate: plots and the 3D view
    double refreshHz() const { return m_refreshHz; }
    void setRefreshHz(double hz);
    // OFFLINE / IDLE / READY / RUN / FAULT for the big status light
    QString runState() const;
    bool simulating() const;
    // Manual Control window open with Output off
    Q_INVOKABLE void setManualPreview(bool on);
    // firmware answered `i` with cnt90= (supports j / I / a)
    bool firmwareConfigurable() const { return m_fwConfigurable; }
    // push the over-current settings of the main joint to the firmware (`I`)
    Q_INVOKABLE bool pushProtection();
    QString activeMode() const { return m_mode; }
    void setActiveMode(const QString &m);
    QVariantMap ui() const { return m_ui; }
    void setUi(const QVariantMap &u);
    QUrl documentsFolder() const;

    Q_INVOKABLE void setUiValue(const QString &key, const QVariant &value);
    Q_INVOKABLE QVariant uiValue(const QString &key, const QVariant &fallback = QVariant()) const;
    Q_INVOKABLE QString fileName(const QString &pathOrUrl) const;
    Q_INVOKABLE QString localPath(const QString &pathOrUrl) const;
    Q_INVOKABLE void saveNow() { m_config->save(); }

signals:
    void activeModeChanged();
    void uiChanged();
    void refreshHzChanged();
    void runStateChanged();
    void simulatingChanged();
    void firmwareConfigurableChanged();

private:
    void onLine(int port, const QString &line);
    void onPortConnection(int port);
    // 2 ports: each port's latest log, merged into one 6-joint sample
    void onTelemetryLine(int port, double t, const QStringList &names, const QVector<double> &values);
    // fw 0.8: "LINK: up U7" / "LINK: lost U7"
    void onLinkLine(int port, const QString &state, const QString &peer, const QString &text);
    void reportPeer(int port, bool up, const QString &text);
    // joints no open port drives keep no stale feedback
    void clearUnreachableFeedback();
    QString rxTag(int port) const;
    QString txTag(int port) const;
    QJsonObject collect() const;
    void apply(const QJsonObject &o);
    void installExamples();
    void onOverCurrent(int joint, double ma);
    // ID line: G474 port layout (m <main joint>, protocol), then j / I sync once per connection
    void onFirmwareId(int joints, double cnt90);

    SerialLink *m_link;
    SerialLink *m_link2;
    CanBus *m_can;
    // a connection session is open (first port up .. last port down)
    bool m_session = false;
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
    Calibration *m_calib;
    CadModel *m_cad;
    BoardMonitor *m_board;
    EventLog *m_events;
    Recorder *m_recorder;
    // per port: last log line that carried temp / rh / vbat (2 ports: U7 first)
    QElapsedTimer m_boardSeen[2];
    double m_refreshHz = 30;
    bool m_fwConfigurable = false;
    QTimer m_idTimer[2];
    bool m_gotId[2] = {false, false};
    // joints whose j / I went to the firmware on this connection (per port)
    QStringList m_synced[2];
    // port whose text line is being parsed (ID -> onFirmwareId)
    int m_rxPort = 0;
    QElapsedTimer m_rxClock;
    struct PortLog {
        QStringList names;
        QVector<double> values;
        QElapsedTimer age;
    };
    PortLog m_portLog[2];
    QString m_mode = QStringLiteral("text");
    QVariantMap m_ui;
    QTimer m_compileTimer;
    bool m_manualPreview = false;
};
