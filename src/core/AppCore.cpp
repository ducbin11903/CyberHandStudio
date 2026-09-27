#include "AppCore.h"

#include "LineParser.h"
#include "PlotItem.h"

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

    QString dir = configDir;
    if (dir.isEmpty())
        dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/configs");
    m_config = new ConfigStore(dir, this);
    m_config->setCollector([this]() { return collect(); });
    m_config->setApplier([this](const QJsonObject &o) { apply(o); });

    // serial -> parser -> telemetry / console
    connect(m_link, &SerialLink::lineReceived, this, &AppCore::onLine);
    connect(m_link, &SerialLink::lineSent, m_console, &ConsoleModel::onSent);
    connect(m_link, &SerialLink::connectedChanged, this, [this]() {
        m_telemetry->reset();
        if (m_link->connected()) {
            m_live->clear();
            m_console->info(tr("Đã kết nối %1 @ %2").arg(m_link->portName()).arg(m_link->baudRate()));
        } else {
            m_console->info(tr("Đã ngắt kết nối"));
        }
    });
    connect(m_link, &SerialLink::errorStringChanged, this, [this]() {
        if (!m_link->errorString().isEmpty())
            m_console->error(m_link->errorString());
    });
    connect(m_telemetry, &Telemetry::sample, m_live, &LiveSeries::addSample);
    connect(m_telemetry, &Telemetry::overCurrent, this, [this](double ma) {
        m_console->error(tr("Dòng %1 mA vượt ngưỡng %2 mA").arg(LineParser::num(ma, 0), LineParser::num(m_telemetry->currentLimitMa(), 0)));
        if (m_telemetry->autoDisable()) {
            m_player->pause();
            m_ctrl->disable();
        }
    });
    connect(m_telemetry, &Telemetry::configFromFirmware, this, [this](int joints, double cnt90) {
        if (cnt90 > 0) {
            if (Joint *j = m_hand->joint(m_hand->benchJoint()))
                j->setCountsPer90(cnt90);
        }
        m_console->info(tr("Firmware: %1 khớp, %2 count/90°").arg(joints).arg(cnt90));
    });

    // stream / gamepad / poses -> hand
    connect(m_stream, &StreamServer::preview, this, [this](const QVector<double> &deg) {
        if (m_mode == QLatin1String("stream") && !m_stream->mirror())
            m_hand->setCommands(deg);
    });
    connect(m_stream, &StreamServer::frameChanged, this, [this]() {
        const QVariantList out = m_stream->output();
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
        {QStringLiteral("currentLimitMa"), m_telemetry->currentLimitMa()},
        {QStringLiteral("autoDisable"), m_telemetry->autoDisable()}});
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
        m_telemetry->setCurrentLimitMa(c.value(QStringLiteral("currentLimitMa")).toDouble(m_telemetry->currentLimitMa()));
        m_telemetry->setAutoDisable(c.value(QStringLiteral("autoDisable")).toBool(m_telemetry->autoDisable()));
    }
    m_hand->fromJson(o.value(QStringLiteral("joints")).toArray());
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
    m_macros->fromJson(o.value(QStringLiteral("macros")).toArray());
    if (o.contains(QStringLiteral("llm")))
        m_llm->fromJson(o.value(QStringLiteral("llm")).toObject());
    if (o.contains(QStringLiteral("ui")))
        setUi(o.value(QStringLiteral("ui")).toObject().toVariantMap());
}
