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
#include <QStandardPaths>

AppCore::AppCore(const QString &configDir, QObject *parent)
    : QObject(parent)
{
    m_link = new SerialLink(this);
    m_console = new ConsoleModel(this);
    m_hand = new HandModel(this);
    m_telemetry = new Telemetry(m_hand, this);
    m_ctrl = new HandController(m_link, m_console, m_hand, this);
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

    QString dir = configDir;
    if (dir.isEmpty())
        dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/configs");
    m_config = new ConfigStore(dir, this);
    m_config->setCollector([this]() { return collect(); });
    m_config->setApplier([this](const QJsonObject &o) { apply(o); });

    // serial -> parser -> telemetry / console
    connect(m_link, &SerialLink::lineReceived, this, &AppCore::onLine);
    connect(m_link, &SerialLink::lineSent, m_console, &ConsoleModel::onSent);
    m_idTimer.setSingleShot(true);
    m_idTimer.setInterval(500);
    connect(&m_idTimer, &QTimer::timeout, this, [this]() {
        if (m_link->connected() && m_telemetry->firmwareId().isEmpty())
            m_ctrl->requestId();
    });
    connect(m_link, &SerialLink::connectedChanged, this, [this]() {
        m_telemetry->reset();
        if (m_fwConfigurable) {
            m_fwConfigurable = false;
            emit firmwareConfigurableChanged();
        }
        m_fwSynced = false;
        if (m_link->connected()) {
            m_live->clear();
            m_console->info(tr("Đã kết nối %1 @ %2").arg(m_link->portName()).arg(m_link->baudRate()));
            m_idTimer.start(); // ask the firmware who it is (cnt90, joints)
        } else {
            m_console->info(tr("Đã ngắt kết nối"));
        }
        emit runStateChanged();
    });
    connect(m_telemetry, &Telemetry::updated, this, &AppCore::runStateChanged);
    connect(m_player, &Player::playingChanged, this, &AppCore::runStateChanged);
    connect(m_link, &SerialLink::errorStringChanged, this, [this]() {
        if (!m_link->errorString().isEmpty())
            m_console->error(m_link->errorString());
    });
    connect(m_telemetry, &Telemetry::sample, m_live, &LiveSeries::addSample);
    connect(m_telemetry, &Telemetry::overCurrent, this, &AppCore::onOverCurrent);
    connect(m_telemetry, &Telemetry::configFromFirmware, this, &AppCore::onFirmwareId);
    // G474: the main joint picks the motor of the single-motor commands
    connect(m_hand, &HandModel::benchJointChanged, this, [this]() {
        if (m_link->connected() && !m_ctrl->portKeys().isEmpty())
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
    qmlRegisterType<CadPose>(uri, 1, 0, "CadPose");
    qmlRegisterType<CadGeometry>(uri, 1, 0, "CadGeometry");
}

void AppCore::onLine(const QString &line, double t)
{
    const ParsedLine p = LineParser::parse(line);
    if (p.telemetry) {
        m_telemetry->processTelemetry(t, p.names, p.values);
        m_console->onTelemetryLine(line);
    } else {
        m_console->onRx(line);
        m_telemetry->processText(line);
    }
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
    if (!m_link->connected())
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
    m_console->info(tr("Firmware: %1 khớp, %2 count/90°").arg(joints).arg(cnt90));
    const bool g474 = m_telemetry->firmwareG474();
    const QStringList keys = m_telemetry->firmwareKeys();
    if (g474) {
        // MCB G474: this port = one MCU with 3 motors
        m_ctrl->setPortInfo(m_telemetry->firmwareMcu(), keys);
        m_console->info(tr("Cổng %1: %2").arg(m_telemetry->firmwareMcu().isEmpty() ? tr("MCU") : m_telemetry->firmwareMcu(),
                                             keys.join(QStringLiteral(", "))));
        if (m_ctrl->protocol() != HandController::PerJoint) {
            m_ctrl->setProtocol(HandController::PerJoint);
            m_console->info(tr("Firmware G474 → giao thức \"Từng khớp\""));
        }
        m_ctrl->selectBenchMotor(); // m <main joint>, or "Khớp X nằm ở U7 — cắm cổng U7"
    }
    if (cnt90 <= 0)
        return;
    if (!m_fwConfigurable) {
        m_fwConfigurable = true;
        emit firmwareConfigurableChanged();
    }
    if (m_fwSynced) // an `i` typed later does not push everything again
        return;
    m_fwSynced = true;

    if (g474) {
        if (!m_calib->syncOnConnect())
            return;
        // the app profile holds the calibration: every motor of this MCU gets its j (and I)
        static const int fw[] = {3, 0, 1, 2};
        for (const QString &k : keys) {
            const Joint *j = m_hand->joint(m_hand->indexOfKey(k));
            if (!j)
                continue;
            m_ctrl->send(QStringLiteral("m ") + k);
            if (j->countsPer90() >= 1)
                m_ctrl->send(QStringLiteral("j ") + LineParser::num(j->countsPer90(), 0));
            else
                m_console->error(tr("%1: cnt/90° = %2 (âm) — không gửi j, sửa chiều motor/encoder trong firmware")
                                     .arg(k, LineParser::num(j->countsPer90(), 0)));
            if (m_telemetry->overSource() == Telemetry::Firmware)
                m_ctrl->send(HandController::protectionCommand(j->currentLimitMa(), m_telemetry->tripMs(),
                                                               fw[qBound(0, m_telemetry->overResponse(), 3)],
                                                               m_telemetry->retractDeg()));
        }
        m_ctrl->selectBenchMotor(false);
        return;
    }

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
                                                  {QStringLiteral("lineEnding"), m_link->lineEnding()}});
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
    if (!link.isEmpty() && !m_link->connected()) {
        const QString port = link.value(QStringLiteral("port")).toString();
        if (!port.isEmpty())
            m_link->setPortName(port);
        m_link->setBaudRate(link.value(QStringLiteral("baud")).toInt(115200));
        m_link->setLineEnding(link.value(QStringLiteral("lineEnding")).toInt(0));
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
