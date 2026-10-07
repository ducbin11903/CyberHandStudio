#include "AppCore.h"

#include "LineParser.h"
#include "PlotItem.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QQmlEngine>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QStandardPaths>

AppCore::AppCore(const QString &configDir, QObject *parent)
    : QObject(parent)
{
    m_link = new SerialLink(this);
    m_link2 = new SerialLink(this);
    m_link2->setAutoSelect(false);
    m_console = new ConsoleModel(this);
    m_hand = new HandModel(this);
    m_telemetry = new Telemetry(m_hand, this);
    m_ctrl = new HandController(m_link, m_console, m_hand, this);
    m_ctrl->addLink(m_link2);
    m_live = new LiveSeries(this);
    m_streamSeries = new LiveSeries(this);
    m_streamSeries->setCapacity(6000);
    m_streamClock.start();
    m_textTraj = new Trajectory(m_hand, this);
    m_timelineTraj = new Trajectory(m_hand, this);
    m_timeline = new TimelineModel(m_hand, this);
    m_player = new Player(m_hand, m_ctrl, this);
    m_stream = new StreamServer(m_hand, this);
    m_gamepad = new GamepadInput(m_hand, this);
    m_poses = new PoseLibrary(m_hand, this);
    m_llm = new LlmClient(m_hand, this);
    m_macros = new MacroRunner(m_ctrl, m_console, m_poses, this);
    m_calib = new Calibration(m_hand, m_ctrl, m_telemetry, m_link, m_console, this);
    m_cad = new CadModel(this);
    m_board = new BoardMonitor(m_ctrl, this);
    m_events = new EventLog(this);
    m_recorder = new Recorder(m_hand, m_telemetry, m_board, this);

    QString dir = configDir;
    if (dir.isEmpty())
        dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/configs");
    m_config = new ConfigStore(dir, this);
    m_config->setCollector([this]() { return collect(); });
    m_config->setApplier([this](const QJsonObject &o) { apply(o); });

    // serial -> parser -> telemetry / console (port 1 and port 2)
    for (int i = 0; i < 2; ++i) {
        SerialLink *l = i ? m_link2 : m_link;
        connect(l, &SerialLink::lineReceived, this, [this, i](const QString &line, double) { onLine(i, line); });
        connect(l, &SerialLink::lineSent, this, [this, i](const QString &line, bool quiet) {
            m_console->onSent(line, quiet, txTag(i));
            // Enable acknowledges a "safe again" banner (the firmware refuses while still unsafe)
            if (line == QLatin1String("e") || line.startsWith(QLatin1String("e ")))
                m_board->onEnableSent();
        });
        connect(l, &SerialLink::connectedChanged, this, [this, i]() { onPortConnection(i); });
        connect(l, &SerialLink::errorStringChanged, this, [this, l]() {
            if (!l->errorString().isEmpty())
                m_console->error(m_ctrl->openPorts() > 0 || m_ctrl->dualPort()
                                     ? QStringLiteral("%1: %2").arg(l->portName(), l->errorString())
                                     : l->errorString());
        });
        m_idTimer[i].setSingleShot(true);
        m_idTimer[i].setInterval(500);
        connect(&m_idTimer[i], &QTimer::timeout, this, [this, i]() {
            // ask the firmware who it is (cnt90, joints, keys)
            if (m_ctrl->portOpen(i) && !m_gotId[i])
                m_ctrl->sendTo(i, QStringLiteral("i"));
        });
    }
    // port 2 follows the serial settings of port 1
    connect(m_link, &SerialLink::baudRateChanged, this, [this]() { m_link2->setBaudRate(m_link->baudRate()); });
    connect(m_link, &SerialLink::lineEndingChanged, this, [this]() { m_link2->setLineEnding(m_link->lineEnding()); });
    connect(m_ctrl, &HandController::dualPortChanged, this, [this]() {
        // 2-port mode: suggest a free port for port 2
        if (m_ctrl->dualPort() && m_link2->portName().isEmpty()) {
            m_link2->refreshPorts();
            for (const QString &n : m_link2->ports())
                if (n != m_link->portName()) {
                    m_link2->setPortName(n);
                    break;
                }
        }
    });
    connect(m_telemetry, &Telemetry::updated, this, &AppCore::runStateChanged);
    connect(m_player, &Player::playingChanged, this, &AppCore::runStateChanged);
    connect(m_link, &SerialLink::errorStringChanged, this, [this]() {
        if (!m_link->errorString().isEmpty())
            m_console->error(m_link->errorString());
    });
    connect(m_telemetry, &Telemetry::sample, m_live, &LiveSeries::addSample);
    connect(m_telemetry, &Telemetry::overCurrent, this, &AppCore::onOverCurrent);
    // event log: over-current reactions, LINK, OTEMP, VBAT
    connect(m_telemetry, &Telemetry::eventNoted, m_events, &EventLog::add);
    connect(m_board, &BoardMonitor::event, this, [this](const QString &code, const QString &text, bool alarm) {
        if (alarm)
            m_console->error(text);
        else
            m_console->info(text);
        m_telemetry->noteEvent(code, text, alarm);
    });
    connect(m_recorder, &Recorder::message, this, [this](const QString &text, bool error) {
        if (error)
            m_console->error(text);
        else
            m_console->info(text);
    });
    connect(m_telemetry, &Telemetry::configFromFirmware, this, &AppCore::onFirmwareId);
    // G474: the main joint picks the motor (and port) of the single-motor commands
    connect(m_hand, &HandModel::benchJointChanged, this, [this]() {
        if (m_ctrl->connected() && m_ctrl->layoutKnown())
            m_ctrl->selectBenchMotor();
    });

    // stream / gamepad / poses -> hand
    connect(m_stream, &StreamServer::preview, this, [this](const QVector<double> &deg) {
        if (m_mode == QLatin1String("stream") && !m_stream->mirror())
            m_hand->setCommands(deg);
    });
    connect(m_stream, &StreamServer::processedChanged, this, [this]() {
        const QVariantList out = m_stream->processed();
        QVector<double> v;
        for (const QVariant &x : out)
            v << (x.isValid() ? x.toDouble() : qQNaN());
        m_streamSeries->addSample(m_streamClock.nsecsElapsed() / 1e9, m_hand->keys(), v);
    });
    connect(m_stream, &StreamServer::forward, this, [this](const QVector<double> &deg) {
        if (m_mode == QLatin1String("stream"))
            m_ctrl->sendJoints(deg, false, true);
    });
    connect(m_gamepad, &GamepadInput::forward, this, [this](const QVector<double> &deg) {
        if (m_gamepad->output())
            m_ctrl->sendJoints(deg, false, true);
        else
            m_hand->setCommands(deg);
    });
    connect(m_gamepad, &GamepadInput::poseRequested, m_poses, &PoseLibrary::applyByName);
    connect(m_gamepad, &GamepadInput::commandRequested, m_ctrl, &HandController::send);
    connect(m_poses, &PoseLibrary::applyRequested, this, [this](const QVariantList &values, const QString &name) {
        m_console->info(tr("Pose %1").arg(name));
        m_player->pause();
        m_ctrl->applyPose(values);
        if (m_gamepad->enabled())
            m_gamepad->syncFromHand();
    });
    connect(m_llm, &LlmClient::trajectoryReady, this, [this](const QString &csv, const QString &prompt) {
        if (m_textTraj->fromCsvText(csv)) {
            QString n = prompt.left(32).simplified().toLower();
            n.replace(QLatin1Char(' '), QLatin1Char('_'));
            m_textTraj->setName(QStringLiteral("ai_") + n);
            m_console->info(tr("AI tạo quỹ đạo %1 s, %2 điểm").arg(LineParser::num(m_textTraj->duration(), 1)).arg(m_textTraj->sampleCount()));
        } else {
            m_console->error(tr("AI trả CSV không hợp lệ: %1").arg(m_textTraj->lastError()));
        }
    });

    // timeline edits recompile into its trajectory
    m_compileTimer.setSingleShot(true);
    m_compileTimer.setInterval(40);
    connect(&m_compileTimer, &QTimer::timeout, this, [this]() { m_timeline->compileInto(m_timelineTraj, 50); });
    connect(m_timeline, &TimelineModel::changed, &m_compileTimer, qOverload<>(&QTimer::start));
    connect(m_timeline, &TimelineModel::nameChanged, this, [this]() { m_timelineTraj->setName(m_timeline->name()); });

    installExamples();
    m_textTraj->makeDemo(0);
    m_timeline->makeDemo();
    m_timeline->compileInto(m_timelineTraj, 50);
    m_player->setTrajectory(m_textTraj);

    m_config->loadLastOrDefault();
    loadCad(m_ui.value(QStringLiteral("cadFolder")).toString());
    connect(qApp, &QGuiApplication::aboutToQuit, this, [this]() { m_config->save(); });
}

AppCore::~AppCore() = default;

void AppCore::registerQmlTypes()
{
    const char *uri = "CyberHand.Core";
    qmlRegisterSingletonInstance(uri, 1, 0, "Core", this);
    qmlRegisterType<PlotItem>(uri, 1, 0, "PlotItem");
    const QString why = QStringLiteral("owned by Core");
    qmlRegisterUncreatableType<SerialLink>(uri, 1, 0, "SerialLink", why);
    qmlRegisterUncreatableType<ConsoleModel>(uri, 1, 0, "ConsoleModel", why);
    qmlRegisterUncreatableType<HandModel>(uri, 1, 0, "HandModel", why);
    qmlRegisterUncreatableType<Joint>(uri, 1, 0, "Joint", why);
    qmlRegisterUncreatableType<Telemetry>(uri, 1, 0, "Telemetry", why);
    qmlRegisterUncreatableType<HandController>(uri, 1, 0, "HandController", why);
    qmlRegisterUncreatableType<PlotSource>(uri, 1, 0, "PlotSource", why);
    qmlRegisterUncreatableType<LiveSeries>(uri, 1, 0, "LiveSeries", why);
    qmlRegisterUncreatableType<Trajectory>(uri, 1, 0, "Trajectory", why);
    qmlRegisterUncreatableType<TimelineModel>(uri, 1, 0, "TimelineModel", why);
    qmlRegisterUncreatableType<Player>(uri, 1, 0, "Player", why);
    qmlRegisterUncreatableType<StreamServer>(uri, 1, 0, "StreamServer", why);
    qmlRegisterUncreatableType<GamepadInput>(uri, 1, 0, "GamepadInput", why);
    qmlRegisterUncreatableType<PoseLibrary>(uri, 1, 0, "PoseLibrary", why);
    qmlRegisterUncreatableType<LlmClient>(uri, 1, 0, "LlmClient", why);
    qmlRegisterUncreatableType<MacroRunner>(uri, 1, 0, "MacroRunner", why);
    qmlRegisterUncreatableType<ConfigStore>(uri, 1, 0, "ConfigStore", why);
    qmlRegisterUncreatableType<Calibration>(uri, 1, 0, "Calibration", why);
    qmlRegisterUncreatableType<CadModel>(uri, 1, 0, "CadModel", why);
    qmlRegisterUncreatableType<BoardMonitor>(uri, 1, 0, "BoardMonitor", why);
    qmlRegisterUncreatableType<EventLog>(uri, 1, 0, "EventLog", why);
    qmlRegisterUncreatableType<Recorder>(uri, 1, 0, "Recorder", why);
    qmlRegisterType<CadPose>(uri, 1, 0, "CadPose");
    qmlRegisterType<CadGeometry>(uri, 1, 0, "CadGeometry");
}

QString AppCore::rxTag(int port) const
{
    // tag lines only when two MCUs talk: 2 ports, or a port bridging the other MCU
    return (m_ctrl->openPorts() > 1 || m_ctrl->portHasPeer(port)) ? m_ctrl->portLabel(port) : QString();
}

QString AppCore::txTag(int port) const
{
    return m_ctrl->openPorts() > 1 ? m_ctrl->portLabel(port) : QString();
}

void AppCore::onPortConnection(int port)
{
    SerialLink *l = port ? m_link2 : m_link;
    m_gotId[port] = false;
    m_synced[port].clear();
    m_portLog[port] = PortLog();
    if (l->connected()) {
        if (m_ctrl->openPorts() == 1) {
            // first port of a session: fresh state, common time base for both ports
            m_telemetry->reset();
            m_board->reset();
            m_live->clear();
            m_rxClock.start();
        }
        m_console->info(tr("Đã kết nối %1 @ %2").arg(l->portName()).arg(l->baudRate()));
        m_idTimer[port].start();
    } else {
        m_idTimer[port].stop();
        m_console->info(tr("Đã ngắt %1").arg(l->portName()));
        m_boardSeen[port].invalidate();
        if (!m_ctrl->connected()) {
            m_recorder->stop();
            m_telemetry->reset();
            m_board->reset();
        } else {
            clearUnreachableFeedback();
        }
    }
    if (!m_ctrl->connected() && m_fwConfigurable) {
        m_fwConfigurable = false;
        emit firmwareConfigurableChanged();
    }
    emit runStateChanged();
}

bool AppCore::toggleConnection()
{
    if (m_ctrl->connected()) {
        m_link->close();
        m_link2->close();
        return false;
    }
    bool ok = m_link->open();
    if (m_ctrl->dualPort()) {
        if (m_link2->portName().isEmpty()) {
            m_console->error(tr("Chế độ 2 cổng: chưa chọn cổng 2"));
            ok = false;
        } else if (m_link2->portName() == m_link->portName()) {
            m_console->error(tr("Cổng 2 trùng cổng 1 (%1)").arg(m_link->portName()));
            ok = false;
        } else {
            ok = m_link2->open() && ok;
        }
    }
    return ok;
}

void AppCore::clearUnreachableFeedback()
{
    if (!m_ctrl->layoutKnown())
        return;
    for (int i = 0; i < m_hand->count(); ++i)
        if (m_ctrl->portOfJoint(i) < 0)
            m_hand->joint(i)->clearFeedback();
}

void AppCore::onLine(int port, const QString &line)
{
    const double t = m_rxClock.isValid() ? m_rxClock.nsecsElapsed() / 1e9 : 0.0;
    QString text = line;
    QString src = rxTag(port);
    // fw 0.8 bridge: text of the other MCU arrives as "[U7] ..."
    static const QRegularExpression rePeer(QStringLiteral("^\\[(U\\d+)\\]\\s?"));
    const auto mp = rePeer.match(line);
    const bool peerLine = mp.hasMatch();
    if (peerLine) {
        src = mp.captured(1);
        text = line.mid(mp.capturedLength());
    }
    const ParsedLine p = LineParser::parse(text);
    if (p.telemetry) {
        if (peerLine) {
            // status line of the other MCU (@U7 s): shown, not mixed into the hand state
            m_console->onRx(text, src);
            return;
        }
        onTelemetryLine(port, t, p.names, p.values);
        m_console->onTelemetryLine(text, src);
        return;
    }
    m_console->onRx(text, src);
    // board sensors: OK sense / OTEMP: / VBAT: / ERR board ... (from either MCU, duplicates merged)
    if (m_board->processText(text))
        return;
    if (peerLine && text.startsWith(QLatin1String("ID:"), Qt::CaseInsensitive))
        return; // the other MCU's own ID does not describe this port
    static const QRegularExpression reLink(QStringLiteral("^LINK:\\s*(\\w+)\\s*(U\\d+)?"), QRegularExpression::CaseInsensitiveOption);
    const auto ml = reLink.match(text);
    if (ml.hasMatch() && !peerLine) {
        onLinkLine(port, ml.captured(1).toLower(), ml.captured(2).toUpper(), text);
        return;
    }
    m_rxPort = port;
    m_telemetry->processText(text);
}

void AppCore::onLinkLine(int port, const QString &state, const QString &peer, const QString &text)
{
    const bool up = state == QLatin1String("up") || state == QLatin1String("ok") || state == QLatin1String("on");
    if (m_ctrl->setPeerUp(port, up, peer))
        reportPeer(port, up, text);
}

void AppCore::reportPeer(int port, bool up, const QString &text)
{
    const QString other = m_ctrl->ports().value(port).toMap().value(QStringLiteral("peer")).toString();
    if (up) {
        m_console->info(tr("Link %1-%2: up — cổng %1 điều khiển cả %2").arg(m_ctrl->portLabel(port), other));
        return;
    }
    m_console->error(tr("Link %1-%2 mất — khớp của %2 tạm không điều khiển được").arg(m_ctrl->portLabel(port), other));
    m_telemetry->noteEvent(QStringLiteral("LINK"), text);
    clearUnreachableFeedback();
}

void AppCore::onTelemetryLine(int port, double t, const QStringList &names, const QVector<double> &values)
{
    // fw 0.8 bridge: lk = UART link to the other MCU up
    const int lk = names.indexOf(QLatin1String("lk"));
    if (lk >= 0 && lk < values.size() && m_ctrl->portHasPeer(port)) {
        const bool up = values[lk] != 0;
        if (m_ctrl->setPeerUp(port, up))
            reportPeer(port, up, QStringLiteral("lk:%1").arg(up ? 1 : 0));
    }
    // board sensors (on U7, every port reports them): one source, the port of U7 first
    auto isBoardField = [](const QString &n) {
        return n == QLatin1String("temp") || n == QLatin1String("rh") || n == QLatin1String("vbat");
    };
    bool hasBoard = false;
    for (const QString &n : names)
        hasBoard = hasBoard || isBoardField(n);
    if (hasBoard)
        m_boardSeen[port].start();
    const QString label = m_ctrl->portLabel(port);
    const int u7 = m_ctrl->portOfMcu(QStringLiteral("U7"));
    const bool u7Fresh = u7 >= 0 && u7 != port && m_boardSeen[u7].isValid() && m_boardSeen[u7].elapsed() < 3000;
    if (!u7Fresh)
        m_board->updateFromLog(names, values, label == QLatin1String("U7") || label.isEmpty()
                                                  ? QStringLiteral("U7")
                                                  : QStringLiteral("U7 qua %1").arg(label));

    if (m_ctrl->openPorts() < 2) {
        m_telemetry->processTelemetry(t, names, values);
        m_board->updateFault(m_telemetry->flt());
        return;
    }
    // 2 ports: keep each port's latest line; the port of the main joint publishes one
    // merged sample (its 10 channels + qN/tN/iN of both, flt of both, the other port's
    // channels as "U7.out" ..., temp / rh / vbat once)
    PortLog &own = m_portLog[port];
    own.names = names;
    own.values = values;
    own.age.start();
    auto fresh = [this](int p) { return m_portLog[p].age.isValid() && m_portLog[p].age.elapsed() < 1500; };
    int clock = m_ctrl->portOfJoint(m_hand->benchJoint());
    if (clock < 0 || !fresh(clock))
        clock = port; // main port silent: any port drives the samples
    if (port != clock)
        return;
    QStringList n;
    QVector<double> v;
    for (int k = 0; k < names.size() && k < values.size(); ++k) {
        if (isBoardField(names[k]))
            continue;
        n << names[k];
        v << values[k];
    }
    const int other = 1 - port;
    if (fresh(other)) {
        const PortLog &o = m_portLog[other];
        const QString pre = m_ctrl->portLabel(other) + QLatin1Char('.');
        for (int k = 0; k < o.names.size() && k < o.values.size(); ++k) {
            const QString &name = o.names[k];
            const bool jointField = name.size() == 2 && QStringLiteral("qti").contains(name[0]) && name[1].isDigit();
            if (jointField) {
                if (!n.contains(name)) {
                    n << name;
                    v << o.values[k];
                }
                continue;
            }
            if (name == QLatin1String("flt")) {
                // a fault on either MCU is a hand fault
                const int f = n.indexOf(QLatin1String("flt"));
                if (f >= 0)
                    v[f] = double(int(v[f]) | int(o.values[k]));
            }
            if (name != QLatin1String("lk") && !isBoardField(name)) {
                n << pre + name;
                v << o.values[k];
            }
        }
    }
    if (!m_board->stale()) {
        if (m_board->hasTemp()) {
            n << QStringLiteral("temp");
            v << m_board->temp();
        }
        if (m_board->hasRh()) {
            n << QStringLiteral("rh");
            v << m_board->rh();
        }
        if (m_board->hasVbat()) {
            n << QStringLiteral("vbat");
            v << m_board->vbat();
        }
    }
    m_telemetry->processTelemetry(t, n, v);
    m_board->updateFault(m_telemetry->flt());
}

void AppCore::setActiveMode(const QString &m)
{
    if (m == m_mode)
        return;
    m_mode = m;
    if (m != QLatin1String("text") && m != QLatin1String("timeline"))
        m_player->pause();
    if (m == QLatin1String("text"))
        m_player->setTrajectory(m_textTraj);
    else if (m == QLatin1String("timeline"))
        m_player->setTrajectory(m_timelineTraj);
    m_gamepad->setEnabled(m == QLatin1String("controller"));
    emit activeModeChanged();
}

QString AppCore::defaultCadFolder() const
{
    const QStringList candidates = {
        QCoreApplication::applicationDirPath() + QStringLiteral("/models/opengrasp_lite"),
        QStringLiteral(CH_SOURCE_DIR) + QStringLiteral("/assets/models/opengrasp_lite"),
    };
    for (const QString &c : candidates)
        if (QFileInfo::exists(c + QStringLiteral("/rig.json")))
            return c;
    return candidates.first();
}

bool AppCore::loadCad(const QString &folder)
{
    const QString f = folder.isEmpty() ? defaultCadFolder() : folder;
    if (m_cad->load(f)) {
        m_console->info(tr("Mô hình 3D: %1 (%2 tam giác)").arg(m_cad->name()).arg(m_cad->triangles()));
        return true;
    }
    if (!folder.isEmpty())
        m_console->error(tr("Mô hình 3D: %1").arg(m_cad->lastError()));
    return false;
}

void AppCore::setRefreshHz(double hz)
{
    hz = qBound(1.0, hz, 144.0);
    if (qFuzzyCompare(hz, m_refreshHz))
        return;
    m_refreshHz = hz;
    PlotItem::setRefreshHz(hz);
    emit refreshHzChanged();
}

QString AppCore::runState() const
{
    if (!m_ctrl->connected())
        return QStringLiteral("OFFLINE");
    if (m_telemetry->flt() || m_telemetry->overLimit() || m_telemetry->diagLow())
        return QStringLiteral("FAULT");
    if (!m_telemetry->hasData())
        return QStringLiteral("CONNECTED");
    if (!m_telemetry->en())
        return QStringLiteral("IDLE");
    const bool moving = (m_player->playing() && m_player->output()) || qAbs(m_telemetry->out()) >= 1.0
                        || m_calib->running() || m_macros->running();
    return moving ? QStringLiteral("RUN") : QStringLiteral("READY");
}

void AppCore::onFirmwareId(int joints, double cnt90)
{
    const int port = m_rxPort;
    m_gotId[port] = true;
    m_ctrl->setPortId(port, m_telemetry->firmwareId());
    const QString who = m_ctrl->openPorts() > 1 ? (port ? m_link2 : m_link)->portName() + QStringLiteral(": ") : QString();
    m_console->info(tr("%1Firmware: %2 khớp, %3 count/90°").arg(who).arg(joints).arg(cnt90));
    const bool g474 = m_telemetry->firmwareG474();
    const QStringList keys = m_telemetry->firmwareKeys();
    // fw 0.9: read the board sensors once the ID (and the calibration push) went through;
    // one port is enough (both report the U7 sensor), the port of U7 first
    const bool boardFw = g474 && m_telemetry->firmwareHasBoardSensors();
    if (boardFw)
        m_board->setSupported(true);
    auto senseAfter = qScopeGuard([this, port, boardFw]() {
        const int u7 = m_ctrl->portOfMcu(QStringLiteral("U7"));
        if (boardFw && (u7 < 0 || u7 == port))
            m_ctrl->sendTo(port, QStringLiteral("sense"));
    });
    if (g474) {
        // MCB G474: this port = one MCU with 3 motors (fw 0.8 bridge: both MCUs)
        const QString mcu = m_telemetry->firmwareMcu();
        QString peer = m_telemetry->firmwareLink();
        const bool linkDown = peer == QLatin1String("-");
        if (linkDown) // bridge firmware, link down: the peer is the other MCU of the board
            peer = mcu == QLatin1String("U6") ? QStringLiteral("U7") : (mcu == QLatin1String("U7") ? QStringLiteral("U6") : QString());
        const int before = m_ctrl->peerState(port);
        m_ctrl->setPortInfo(port, mcu, keys, peer);
        const int after = m_ctrl->peerState(port);
        QString msg = tr("Cổng %1: %2").arg(mcu.isEmpty() ? tr("MCU") : mcu, keys.join(QStringLiteral(", ")));
        if (!peer.isEmpty())
            msg += linkDown ? tr(" · link %1 mất").arg(peer) : tr(" · link %1").arg(peer);
        m_console->info(msg);
        if (after >= 0 && before >= 0 && after != before)
            reportPeer(port, after == 1, m_telemetry->firmwareId());
        const int other = m_ctrl->portOfMcu(mcu);
        if (!mcu.isEmpty() && other >= 0 && other != port)
            m_console->error(tr("Hai cổng cùng báo %1 — kiểm tra strap MCU_ID").arg(mcu));
        clearUnreachableFeedback();
        if (m_ctrl->protocol() != HandController::PerJoint) {
            m_ctrl->setProtocol(HandController::PerJoint);
            m_console->info(tr("Firmware G474 → giao thức \"Từng khớp\""));
        }
        // m <main joint> on its port, or "Khớp X nằm ở U7 — ..." once every open port has answered
        const int benchPort = m_ctrl->portOfJoint(m_hand->benchJoint());
        bool allAnswered = true;
        for (int i = 0; i < 2; ++i)
            if (m_ctrl->portOpen(i) && !m_gotId[i])
                allAnswered = false;
        if (benchPort == port || (benchPort < 0 && allAnswered))
            m_ctrl->selectBenchMotor();
    }
    if (cnt90 <= 0)
        return;
    if (!m_fwConfigurable) {
        m_fwConfigurable = true;
        emit firmwareConfigurableChanged();
    }

    if (g474) {
        // the app profile holds the calibration: every motor this port drives gets its j (and I),
        // once per connection; joints that left the ID (bridge link lost: the other MCU may
        // have restarted) are pushed again when they come back
        QStringList kept;
        for (const QString &k : m_synced[port])
            if (keys.contains(k))
                kept << k;
        m_synced[port] = kept;
        QStringList todo;
        for (const QString &k : keys)
            if (!m_synced[port].contains(k))
                todo << k;
        m_synced[port] << todo;
        if (todo.isEmpty() || !m_calib->syncOnConnect())
            return;
        static const int fw[] = {3, 0, 1, 2};
        for (const QString &k : todo) {
            const Joint *j = m_hand->joint(m_hand->indexOfKey(k));
            if (!j)
                continue;
            m_ctrl->sendTo(port, QStringLiteral("m ") + k);
            if (j->countsPer90() >= 1)
                m_ctrl->sendTo(port, QStringLiteral("j ") + LineParser::num(j->countsPer90(), 0));
            else
                m_console->error(tr("%1: cnt/90° = %2 (âm) — không gửi j, sửa chiều motor/encoder trong firmware")
                                     .arg(k, LineParser::num(j->countsPer90(), 0)));
            if (m_telemetry->overSource() == Telemetry::Firmware)
                m_ctrl->sendTo(port, HandController::protectionCommand(j->currentLimitMa(), m_telemetry->tripMs(),
                                                                       fw[qBound(0, m_telemetry->overResponse(), 3)],
                                                                       m_telemetry->retractDeg()));
        }
        if (m_ctrl->portOfJoint(m_hand->benchJoint()) == port)
            m_ctrl->selectBenchMotor(false);
        return;
    }

    // single-joint bench (H562): once per connection
    if (m_synced[port].contains(QStringLiteral("*")))
        return;
    m_synced[port] << QStringLiteral("*");
    Joint *j = m_hand->joint(m_hand->benchJoint());
    if (!j)
        return;
    if (m_calib->syncOnConnect() && m_ctrl->protocol() == HandController::BenchSingle) {
        // single-joint bench: push the profile's calibration when the firmware differs
        if (qAbs(j->countsPer90() - cnt90) >= 1)
            m_ctrl->sendCountsPer90(j->countsPer90());
        if (m_telemetry->overSource() == Telemetry::Firmware)
            pushProtection();
    } else if (!m_calib->syncOnConnect()) {
        j->setCountsPer90(cnt90);
    }
}

bool AppCore::pushProtection()
{
    const Joint *j = m_hand->joint(m_hand->benchJoint());
    if (!j)
        return false;
    // app Response -> firmware code: 0 trip, 1 retract, 2 hold, 3 warn
    static const int fw[] = {3, 0, 1, 2};
    return m_ctrl->sendProtection(j->currentLimitMa(), m_telemetry->tripMs(),
                                  fw[qBound(0, m_telemetry->overResponse(), 3)], m_telemetry->retractDeg());
}

void AppCore::onOverCurrent(int joint, double ma)
{
    Joint *j = m_hand->joint(joint);
    if (!j)
        return;
    m_console->error(tr("%1: dòng %2 mA vượt ngưỡng %3 mA")
                         .arg(j->name(), LineParser::num(ma, 0), LineParser::num(j->currentLimitMa(), 0)));
    // the firmware reacts itself and reports "OVERI: ..."
    if (m_telemetry->overSource() == Telemetry::Firmware)
        return;
    const int r = m_telemetry->overResponse();
    if (r == Telemetry::Warn) {
        m_telemetry->noteEvent(QStringLiteral("WARN"), tr("%1 %2 mA").arg(j->key(), LineParser::num(ma, 0)));
        return;
    }
    // stop every source that would push the joint again
    m_player->pause();
    if (m_mode == QLatin1String("stream") && m_stream->mirror()) {
        m_stream->setMirror(false);
        m_console->info(tr("Tắt gửi Stream do quá dòng"));
    }
    if (m_gamepad->enabled() && m_gamepad->output()) {
        m_gamepad->setOutput(false);
        m_console->info(tr("Tắt gửi Controller do quá dòng"));
    }
    if (r == Telemetry::Disable) {
        m_ctrl->disable();
        m_telemetry->noteEvent(QStringLiteral("TRIP"), tr("%1 %2 mA → tắt driver").arg(j->key(), LineParser::num(ma, 0)));
    } else if (r == Telemetry::Retract) {
        const double from = j->hasFeedback() ? j->measuredDeg() : j->commandDeg();
        const double to = j->clamp(from - m_telemetry->retractDeg());
        m_ctrl->moveJoint(joint, to);
        m_telemetry->noteEvent(QStringLiteral("RETR"), tr("%1 %2 mA → lùi về %3°").arg(j->key(), LineParser::num(ma, 0), LineParser::num(to, 1)));
    } else if (r == Telemetry::Hold) {
        const double at = j->hasFeedback() ? j->measuredDeg() : j->commandDeg();
        m_ctrl->moveJoint(joint, at);
        m_telemetry->noteEvent(QStringLiteral("HOLD"), tr("%1 %2 mA → giữ tại %3°").arg(j->key(), LineParser::num(ma, 0), LineParser::num(at, 1)));
    }
}

void AppCore::setUi(const QVariantMap &u)
{
    m_ui = u;
    emit uiChanged();
}

void AppCore::setUiValue(const QString &key, const QVariant &value)
{
    if (m_ui.value(key) == value)
        return;
    m_ui.insert(key, value);
    emit uiChanged();
}

QVariant AppCore::uiValue(const QString &key, const QVariant &fallback) const
{
    return m_ui.value(key, fallback);
}

QString AppCore::fileName(const QString &pathOrUrl) const
{
    return QFileInfo(Trajectory::localPath(pathOrUrl)).fileName();
}

QUrl AppCore::documentsFolder() const
{
    const QString d = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QStringLiteral("/CyberHandStudio");
    return QUrl::fromLocalFile(d);
}

void AppCore::installExamples()
{
    const QString dst = documentsFolder().toLocalFile();
    QDir().mkpath(dst);
    const QStringList files = QDir(QStringLiteral(":/assets/examples")).entryList(QDir::Files);
    for (const QString &f : files) {
        const QString target = QDir(dst).filePath(f);
        if (!QFile::exists(target)) {
            QFile::copy(QStringLiteral(":/assets/examples/") + f, target);
            QFile::setPermissions(target, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadUser | QFile::WriteUser);
        }
    }
}

void AppCore::openPortAtStart(const QString &port, int baud)
{
    m_link->setPortName(port);
    if (baud > 0)
        m_link->setBaudRate(baud);
    m_link->open();
}

void AppCore::openPort2AtStart(const QString &port)
{
    m_ctrl->setDualPort(true);
    m_link2->setPortName(port);
    m_link2->setBaudRate(m_link->baudRate());
    m_link2->open();
}

void AppCore::runMacroText(const QString &text)
{
    m_macros->runText(text, QStringLiteral("cli"));
}

void AppCore::playFile(const QString &file)
{
    if (!m_textTraj->loadCsv(file)) {
        m_console->error(m_textTraj->lastError());
        return;
    }
    setActiveMode(QStringLiteral("text"));
    m_player->setTrajectory(m_textTraj);
    m_player->play();
}

// ---------------------------------------------------------------------------
// config profiles

QJsonObject AppCore::collect() const
{
    QJsonObject o;
    o.insert(QStringLiteral("link"), QJsonObject{{QStringLiteral("port"), m_link->portName()},
                                                  {QStringLiteral("baud"), m_link->baudRate()},
                                                  {QStringLiteral("lineEnding"), m_link->lineEnding()},
                                                  {QStringLiteral("dual"), m_ctrl->dualPort()},
                                                  {QStringLiteral("port2"), m_link2->portName()}});
    o.insert(QStringLiteral("control"), QJsonObject{
        {QStringLiteral("protocol"), m_ctrl->protocol()},
        {QStringLiteral("deadband"), m_ctrl->deadband()},
        {QStringLiteral("kp"), m_ctrl->kp()}, {QStringLiteral("ki"), m_ctrl->ki()}, {QStringLiteral("kd"), m_ctrl->kd()},
        {QStringLiteral("dutyLimit"), m_ctrl->dutyLimit()},
        {QStringLiteral("logRate"), m_ctrl->logRate()},
        {QStringLiteral("homePct"), m_ctrl->homePct()},
        {QStringLiteral("homeMa"), m_ctrl->homeMa()},
        {QStringLiteral("velocity"), m_ctrl->velocity()},
        {QStringLiteral("benchJoint"), m_hand->benchJoint()},
        {QStringLiteral("overResponse"), m_telemetry->overResponse()},
        {QStringLiteral("overSource"), m_telemetry->overSource()},
        {QStringLiteral("retractDeg"), m_telemetry->retractDeg()},
        {QStringLiteral("tripMs"), m_telemetry->tripMs()},
        {QStringLiteral("powerRate"), m_ctrl->powerRate()}});
    o.insert(QStringLiteral("calibration"), QJsonObject{
        {QStringLiteral("sweepPct"), m_calib->sweepPct()},
        {QStringLiteral("stopDeg"), m_calib->stopDeg()},
        {QStringLiteral("syncOnConnect"), m_calib->syncOnConnect()}});
    o.insert(QStringLiteral("refreshHz"), m_refreshHz);
    o.insert(QStringLiteral("board"), QJsonObject{{QStringLiteral("cells"), m_board->cells()}});
    o.insert(QStringLiteral("joints"), m_hand->toJson());
    o.insert(QStringLiteral("player"), QJsonObject{
        {QStringLiteral("sendRate"), m_player->sendRate()},
        {QStringLiteral("speed"), m_player->speed()},
        {QStringLiteral("loopMode"), m_player->loopMode()},
        {QStringLiteral("output"), m_player->output()},
        {QStringLiteral("softStart"), m_player->softStart()},
        {QStringLiteral("softStop"), m_player->softStop()},
        {QStringLiteral("rampTime"), m_player->rampTime()}});
    o.insert(QStringLiteral("stream"), m_stream->toJson());
    o.insert(QStringLiteral("gamepad"), QJsonObject{
        {QStringLiteral("mapping"), m_gamepad->toJson()},
        {QStringLiteral("sendRate"), m_gamepad->sendRate()},
        {QStringLiteral("deadzone"), m_gamepad->deadzone()},
        {QStringLiteral("output"), m_gamepad->output()}});
    o.insert(QStringLiteral("poses"), m_poses->toJson());
    o.insert(QStringLiteral("timelineCustom"), m_timeline->customToJson());
    o.insert(QStringLiteral("macros"), m_macros->toJson());
    o.insert(QStringLiteral("llm"), m_llm->toJson());
    o.insert(QStringLiteral("ui"), QJsonObject::fromVariantMap(m_ui));
    return o;
}

void AppCore::apply(const QJsonObject &o)
{
    const QJsonObject link = o.value(QStringLiteral("link")).toObject();
    if (!link.isEmpty() && !m_ctrl->connected()) {
        const QString port = link.value(QStringLiteral("port")).toString();
        if (!port.isEmpty())
            m_link->setPortName(port);
        m_link->setBaudRate(link.value(QStringLiteral("baud")).toInt(115200));
        m_link->setLineEnding(link.value(QStringLiteral("lineEnding")).toInt(0));
        const QString port2 = link.value(QStringLiteral("port2")).toString();
        if (!port2.isEmpty())
            m_link2->setPortName(port2);
        m_ctrl->setDualPort(link.value(QStringLiteral("dual")).toBool(false));
    }
    const QJsonObject c = o.value(QStringLiteral("control")).toObject();
    if (!c.isEmpty()) {
        m_ctrl->setProtocol(c.value(QStringLiteral("protocol")).toInt(m_ctrl->protocol()));
        m_ctrl->setDeadband(c.value(QStringLiteral("deadband")).toDouble(m_ctrl->deadband()));
        m_ctrl->setKp(c.value(QStringLiteral("kp")).toDouble(m_ctrl->kp()));
        m_ctrl->setKi(c.value(QStringLiteral("ki")).toDouble(m_ctrl->ki()));
        m_ctrl->setKd(c.value(QStringLiteral("kd")).toDouble(m_ctrl->kd()));
        m_ctrl->setDutyLimit(c.value(QStringLiteral("dutyLimit")).toDouble(m_ctrl->dutyLimit()));
        m_ctrl->setLogRate(c.value(QStringLiteral("logRate")).toDouble(m_ctrl->logRate()));
        m_ctrl->setHomePct(c.value(QStringLiteral("homePct")).toDouble(m_ctrl->homePct()));
        m_ctrl->setHomeMa(c.value(QStringLiteral("homeMa")).toDouble(m_ctrl->homeMa()));
        m_ctrl->setVelocity(c.value(QStringLiteral("velocity")).toDouble(m_ctrl->velocity()));
        m_hand->setBenchJoint(c.value(QStringLiteral("benchJoint")).toInt(m_hand->benchJoint()));
        if (c.contains(QStringLiteral("overResponse")))
            m_telemetry->setOverResponse(c.value(QStringLiteral("overResponse")).toInt());
        else // profiles before 0.2
            m_telemetry->setAutoDisable(c.value(QStringLiteral("autoDisable")).toBool(false));
        m_telemetry->setOverSource(c.value(QStringLiteral("overSource")).toInt(m_telemetry->overSource()));
        m_telemetry->setRetractDeg(c.value(QStringLiteral("retractDeg")).toDouble(m_telemetry->retractDeg()));
        m_telemetry->setTripMs(c.value(QStringLiteral("tripMs")).toDouble(m_telemetry->tripMs()));
        m_ctrl->setPowerRate(c.value(QStringLiteral("powerRate")).toDouble(m_ctrl->powerRate()));
    }
    m_hand->fromJson(o.value(QStringLiteral("joints")).toArray());
    // profiles before 0.2 had one global limit
    if (c.contains(QStringLiteral("currentLimitMa"))) {
        const QJsonArray ja = o.value(QStringLiteral("joints")).toArray();
        const bool perJoint = !ja.isEmpty() && ja.first().toObject().contains(QStringLiteral("currentLimitMa"));
        if (!perJoint)
            for (int i = 0; i < m_hand->count(); ++i)
                m_hand->joint(i)->setCurrentLimitMa(c.value(QStringLiteral("currentLimitMa")).toDouble(300));
    }
    const QJsonObject cal = o.value(QStringLiteral("calibration")).toObject();
    if (!cal.isEmpty()) {
        m_calib->setSweepPct(cal.value(QStringLiteral("sweepPct")).toDouble(m_calib->sweepPct()));
        m_calib->setStopDeg(cal.value(QStringLiteral("stopDeg")).toDouble(m_calib->stopDeg()));
        m_calib->setSyncOnConnect(cal.value(QStringLiteral("syncOnConnect")).toBool(m_calib->syncOnConnect()));
    }
    setRefreshHz(o.value(QStringLiteral("refreshHz")).toDouble(m_refreshHz));
    m_board->setCells(o.value(QStringLiteral("board")).toObject().value(QStringLiteral("cells")).toInt(m_board->cells()));
    const QJsonObject p = o.value(QStringLiteral("player")).toObject();
    if (!p.isEmpty()) {
        m_player->setSendRate(p.value(QStringLiteral("sendRate")).toDouble(m_player->sendRate()));
        m_player->setSpeed(p.value(QStringLiteral("speed")).toDouble(m_player->speed()));
        m_player->setLoopMode(p.value(QStringLiteral("loopMode")).toInt(m_player->loopMode()));
        m_player->setOutput(p.value(QStringLiteral("output")).toBool(m_player->output()));
        m_player->setSoftStart(p.value(QStringLiteral("softStart")).toBool(m_player->softStart()));
        m_player->setSoftStop(p.value(QStringLiteral("softStop")).toBool(m_player->softStop()));
        m_player->setRampTime(p.value(QStringLiteral("rampTime")).toDouble(m_player->rampTime()));
    }
    if (o.contains(QStringLiteral("stream")))
        m_stream->fromJson(o.value(QStringLiteral("stream")).toObject());
    const QJsonObject g = o.value(QStringLiteral("gamepad")).toObject();
    if (!g.isEmpty()) {
        m_gamepad->fromJson(g.value(QStringLiteral("mapping")).toArray());
        m_gamepad->setSendRate(g.value(QStringLiteral("sendRate")).toDouble(m_gamepad->sendRate()));
        m_gamepad->setDeadzone(g.value(QStringLiteral("deadzone")).toDouble(m_gamepad->deadzone()));
        m_gamepad->setOutput(g.value(QStringLiteral("output")).toBool(m_gamepad->output()));
    }
    m_poses->fromJson(o.value(QStringLiteral("poses")).toArray());
    if (o.contains(QStringLiteral("timelineCustom")))
        m_timeline->customFromJson(o.value(QStringLiteral("timelineCustom")).toArray());
    m_macros->fromJson(o.value(QStringLiteral("macros")).toArray());
    if (o.contains(QStringLiteral("llm")))
        m_llm->fromJson(o.value(QStringLiteral("llm")).toObject());
    if (o.contains(QStringLiteral("ui")))
        setUi(o.value(QStringLiteral("ui")).toObject().toVariantMap());
}
