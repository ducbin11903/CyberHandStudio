#include "CanBus.h"

#include <QUrl>
#include <QtMath>

using CanV1::Frame;

// ---------------------------------------------------------------------------
// worker (CAN thread)

CanWorker::CanWorker() = default;

CanWorker::~CanWorker()
{
    if (m_be)
        m_be->close();
}

void CanWorker::start(const CanConfig &cfg)
{
    stop();
    if (!m_poll) {
        // created here, in the CAN thread
        m_poll = new QTimer(this);
        m_poll->setTimerType(Qt::PreciseTimer);
        m_poll->setInterval(1);
        connect(m_poll, &QTimer::timeout, this, &CanWorker::poll);
        m_hb = new QTimer(this);
        m_hb->setTimerType(Qt::PreciseTimer);
        m_hb->setInterval(100);
        connect(m_hb, &QTimer::timeout, this, &CanWorker::heartbeat);
        m_state = new QTimer(this);
        m_state->setInterval(500);
        connect(m_state, &QTimer::timeout, this, [this]() {
            CanBusState s;
            if (m_be && m_be->state(&s))
                emit busState(s);
        });
    }
    m_be = createCanBackend(cfg.backend);
    QString err;
    if (!m_be->open(cfg, &err)) {
        m_be.reset();
        emit opened(false, err, QString());
        return;
    }
    m_clock.start();
    m_hbSeq = 0;
    m_lastError.clear();
    m_poll->start();
    m_hb->start();
    m_state->start();
    heartbeat(); // the MCB sees a host right away
    flushSent();
    emit opened(true, QString(), m_be->description());
}

void CanWorker::stop()
{
    if (m_poll) {
        m_poll->stop();
        m_hb->stop();
        m_state->stop();
    }
    if (!m_be)
        return;
    flushSent();
    m_be->close();
    m_be.reset();
    emit closed();
}

void CanWorker::send(const Frame &f)
{
    if (!m_be)
        return;
    Frame s = f;
    s.tx = true;
    s.t = m_clock.nsecsElapsed() / 1e9;
    QString err;
    if (!m_be->write(s, &err)) {
        if (err != m_lastError) {
            m_lastError = err;
            emit failed(err);
        }
        return;
    }
    m_lastError.clear();
    m_sentBatch << s;
}

void CanWorker::poll()
{
    if (!m_be)
        return;
    QVector<Frame> in;
    m_be->read(in);
    if (!in.isEmpty()) {
        const double t = m_clock.nsecsElapsed() / 1e9;
        for (Frame &f : in)
            f.t = t;
        emit received(in);
    }
    flushSent();
}

void CanWorker::heartbeat()
{
    Frame hb;
    hb.id = CanV1::IdHostHb;
    hb.data = QByteArray(1, char(m_hbSeq++));
    send(hb);
}

void CanWorker::flushSent()
{
    if (m_sentBatch.isEmpty())
        return;
    emit sent(m_sentBatch);
    m_sentBatch.clear();
}

// ---------------------------------------------------------------------------
// node link

CanNodeLink::CanNodeLink(CanBus *bus, int node)
    : Transport(bus), m_bus(bus), m_node(node)
{
    m_statsTimer.setInterval(500);
    connect(&m_statsTimer, &QTimer::timeout, this, &CanNodeLink::updateStats);
    m_statsTimer.start();
}

bool CanNodeLink::connected() const
{
    return m_bus->connected();
}

QString CanNodeLink::errorString() const
{
    return QString(); // the bus error is shown once, on the CAN line
}

int CanNodeLink::lastLineAgeMs() const
{
    if (!m_lastLine.isValid())
        return -1;
    return int(qMin<qint64>(m_lastLine.elapsed(), 99999999));
}

bool CanNodeLink::open()
{
    return m_bus->open();
}

void CanNodeLink::close()
{
    m_bus->close();
}

bool CanNodeLink::sendLine(const QString &text, bool quiet)
{
    if (!m_bus->sendText(m_node, text))
        return false;
    noteSent(text, quiet, text.toUtf8().size());
    return true;
}

void CanNodeLink::deliver(const QString &line, double t, int bytes)
{
    m_rxBytes += bytes;
    ++m_lines;
    m_lastLine.start();
    emit lineReceived(line, t);
}

void CanNodeLink::noteSent(const QString &line, bool quiet, int bytes)
{
    m_txBytes += bytes;
    emit lineSent(line, quiet);
}

void CanNodeLink::updateStats()
{
    const double dt = m_statsTimer.interval() / 1000.0;
    m_rxRate = m_rxBytes / dt;
    m_txRate = m_txBytes / dt;
    m_lineRate = m_lines / dt;
    m_rxBytes = m_txBytes = m_lines = 0;
    emit statsChanged();
}

// ---------------------------------------------------------------------------
// monitor model

CanFrameLog::CanFrameLog(QObject *parent)
    : QAbstractListModel(parent)
{
}

int CanFrameLog::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

QVariant CanFrameLog::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size())
        return {};
    const Frame &f = m_all[m_rows[index.row()]];
    switch (role) {
    case TimeRole: return QString::number(f.t, 'f', 3);
    case DirRole: return f.tx ? QStringLiteral("Tx") : QStringLiteral("Rx");
    case IdRole: return QStringLiteral("%1").arg(f.id, f.ext ? 8 : 3, 16, QLatin1Char('0')).toUpper();
    case LenRole: return f.data.size();
    case FlagsRole: return f.fd ? (f.brs ? QStringLiteral("FD BRS") : QStringLiteral("FD")) : QStringLiteral("CAN");
    case DataRole: return CanV1::hex(f.data);
    case InfoRole: return describe(f);
    }
    return {};
}

QHash<int, QByteArray> CanFrameLog::roleNames() const
{
    return {{TimeRole, "time"}, {DirRole, "dir"}, {IdRole, "frameId"}, {LenRole, "len"},
            {FlagsRole, "flags"}, {DataRole, "hexData"}, {InfoRole, "info"}};
}

void CanFrameLog::setFilter(const QString &f)
{
    if (f == m_filterText)
        return;
    m_filterText = f;
    m_filter = CanV1::IdFilter(f);
    beginResetModel();
    rebuild();
    endResetModel();
    emit filterChanged();
    emit countChanged();
}

void CanFrameLog::setPaused(bool p)
{
    if (p == m_paused)
        return;
    m_paused = p;
    emit pausedChanged();
}

void CanFrameLog::rebuild()
{
    m_rows.clear();
    for (int i = 0; i < m_all.size(); ++i)
        if (m_filter.accepts(m_all[i].id))
            m_rows << i;
}

void CanFrameLog::add(const QVector<Frame> &frames)
{
    if (m_paused || frames.isEmpty())
        return;
    // drop the oldest frames first (rows that show them go too)
    const int over = m_all.size() + frames.size() - m_max;
    if (over > 0) {
        const int drop = qMin(m_all.size(), over + m_max / 10);
        int rows = 0;
        while (rows < m_rows.size() && m_rows[rows] < drop)
            ++rows;
        if (rows > 0) {
            beginRemoveRows(QModelIndex(), 0, rows - 1);
            m_rows.remove(0, rows);
            endRemoveRows();
        }
        m_all.remove(0, drop);
        for (int &r : m_rows)
            r -= drop;
    }
    QVector<int> added;
    for (const Frame &f : frames) {
        m_all << f;
        if (m_filter.accepts(f.id))
            added << m_all.size() - 1;
    }
    if (!added.isEmpty()) {
        beginInsertRows(QModelIndex(), m_rows.size(), m_rows.size() + added.size() - 1);
        m_rows << added;
        endInsertRows();
        emit countChanged();
    }
}

void CanFrameLog::clear()
{
    beginResetModel();
    m_all.clear();
    m_rows.clear();
    endResetModel();
    emit countChanged();
}

QString CanFrameLog::describe(const Frame &f)
{
    auto mcu = [](int n) { return n == 1 ? QStringLiteral("U6") : (n == 2 ? QStringLiteral("U7") : QStringLiteral("node %1").arg(n)); };
    auto text = [](const QByteArray &d) {
        QString s = QString::fromUtf8(d);
        s.remove(QChar(0));
        s.replace(QLatin1Char('\n'), QStringLiteral("⏎"));
        s.replace(QLatin1Char('\r'), QString());
        return s;
    };
    const quint32 id = f.id;
    if (id == CanV1::IdEStop)
        return QStringLiteral("E-STOP");
    if (id == CanV1::IdHostHb)
        return QStringLiteral("HOST_HB %1").arg(f.data.isEmpty() ? -1 : int(uchar(f.data[0])));
    if (id == CanV1::IdPosAll) {
        static const char *keys[] = {"T2", "T1", "I", "M", "R", "P"};
        const QVector<double> q = CanV1::decodePosAll(f.data);
        QStringList l;
        for (int i = 0; i < 6; ++i)
            if (!qIsNaN(q[i]))
                l << QStringLiteral("%1=%2").arg(QLatin1String(keys[i])).arg(q[i], 0, 'f', 2);
        return QStringLiteral("POS_ALL ") + (l.isEmpty() ? QStringLiteral("(giữ cả 6)") : l.join(QLatin1Char(' ')));
    }
    if (id == CanV1::IdCmdText)
        return QStringLiteral("CMD → cả hai: ") + text(f.data);
    if (id > CanV1::IdCmdText && id < CanV1::IdCmdText + 0x80)
        return QStringLiteral("CMD → %1: ").arg(mcu(int(id - CanV1::IdCmdText))) + text(f.data);
    if (id > CanV1::IdText && id < CanV1::IdText + 0x80)
        return QStringLiteral("TEXT %1: ").arg(mcu(int(id - CanV1::IdText))) + text(f.data);
    if (id > CanV1::IdStatus && id < CanV1::IdStatus + 0x80) {
        CanV1::Status s;
        if (CanV1::decodeStatus(f.data, &s))
            return QStringLiteral("STATUS %1 #%2").arg(mcu(int(id - CanV1::IdStatus))).arg(s.seq);
        return QStringLiteral("STATUS %1 (lỗi)").arg(mcu(int(id - CanV1::IdStatus)));
    }
    if (id > CanV1::IdPing && id < CanV1::IdPing + 0x80)
        return QStringLiteral("PING %1").arg(mcu(int(id - CanV1::IdPing)));
    if (id > CanV1::IdPong && id < CanV1::IdPong + 0x80)
        return QStringLiteral("PONG %1").arg(mcu(int(id - CanV1::IdPong)));
    return QString();
}

// ---------------------------------------------------------------------------
// bus

CanBus::CanBus(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<CanV1::Frame>("CanV1::Frame");
    qRegisterMetaType<QVector<CanV1::Frame>>("QVector<CanV1::Frame>");
    qRegisterMetaType<CanConfig>("CanConfig");
    qRegisterMetaType<CanBusState>("CanBusState");
    m_nodes[0] = new CanNodeLink(this, 1);
    m_nodes[1] = new CanNodeLink(this, 2);
    m_monitor = new CanFrameLog(this);

    m_worker = new CanWorker;
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(this, &CanBus::startWorker, m_worker, &CanWorker::start);
    connect(this, &CanBus::stopWorker, m_worker, &CanWorker::stop);
    connect(this, &CanBus::transmit, m_worker, &CanWorker::send);
    connect(m_worker, &CanWorker::opened, this, &CanBus::onOpened);
    connect(m_worker, &CanWorker::closed, this, &CanBus::onClosed);
    connect(m_worker, &CanWorker::received, this, &CanBus::onReceived);
    connect(m_worker, &CanWorker::sent, this, &CanBus::onSent);
    connect(m_worker, &CanWorker::failed, this, &CanBus::setError);
    connect(m_worker, &CanWorker::busState, this, [this](const CanBusState &s) {
        m_state = s;
        emit statsChanged();
    });
    m_thread.setObjectName(QStringLiteral("CAN"));
    m_thread.start(QThread::HighPriority);

    m_posTimer.setSingleShot(true);
    m_posTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_posTimer, &QTimer::timeout, this, &CanBus::flushPos);
    m_pendingPos = QVector<double>(6, qQNaN());

    m_monitorTimer.setInterval(100);
    connect(&m_monitorTimer, &QTimer::timeout, this, [this]() {
        if (m_monitorBatch.isEmpty())
            return;
        m_monitor->add(m_monitorBatch);
        m_monitorBatch.clear();
    });
    m_monitorTimer.start();
    m_statsTimer.setInterval(500);
    connect(&m_statsTimer, &QTimer::timeout, this, &CanBus::updateStats);
    m_statsTimer.start();
}

CanBus::~CanBus()
{
    if (m_worker)
        QMetaObject::invokeMethod(m_worker, &CanWorker::stop, Qt::BlockingQueuedConnection);
    m_thread.quit();
    m_thread.wait();
}

Transport *CanBus::node1() const { return m_nodes[0]; }
Transport *CanBus::node2() const { return m_nodes[1]; }

QStringList CanBus::backendNames() const
{
    return {tr("Waveshare USB-CAN-FD"), tr("Ảo (UDP, fw_sim)")};
}

void CanBus::setConfig(const CanConfig &c)
{
    m_cfg = c;
    emit settingsChanged();
}

void CanBus::setBackend(int b)
{
    b = qBound(0, b, 1);
    if (b == m_cfg.backend)
        return;
    m_cfg.backend = b;
    emit settingsChanged();
}

void CanBus::setDeviceIndex(int d)
{
    d = qBound(0, d, 15);
    if (d == m_cfg.device)
        return;
    m_cfg.device = d;
    emit settingsChanged();
}

void CanBus::setChannel(int c)
{
    c = qBound(0, c, 1);
    if (c == m_cfg.channel)
        return;
    m_cfg.channel = c;
    emit settingsChanged();
}

void CanBus::setArbBaud(int b)
{
    if (b <= 0 || b == m_cfg.arbBaud)
        return;
    m_cfg.arbBaud = b;
    emit settingsChanged();
}

void CanBus::setDataBaud(int b)
{
    if (b <= 0 || b == m_cfg.dataBaud)
        return;
    m_cfg.dataBaud = b;
    emit settingsChanged();
}

void CanBus::setTermination(bool on)
{
    if (on == m_cfg.termination)
        return;
    m_cfg.termination = on;
    emit settingsChanged();
}

void CanBus::setDllPath(const QString &p)
{
    const QUrl u(p);
    const QString path = u.isLocalFile() ? u.toLocalFile() : p;
    if (path == m_cfg.dllPath)
        return;
    m_cfg.dllPath = path;
    emit settingsChanged();
}

void CanBus::setUdpHost(const QString &h)
{
    if (h == m_cfg.udpHost)
        return;
    m_cfg.udpHost = h;
    emit settingsChanged();
}

void CanBus::setUdpPort(int p)
{
    p = qBound(1, p, 65535);
    if (p == m_cfg.udpPort)
        return;
    m_cfg.udpPort = p;
    emit settingsChanged();
}

qint64 CanBus::statusLost() const
{
    return qint64(m_seq[0].lost() + m_seq[1].lost());
}

QString CanBus::statusLostText() const
{
    return QStringLiteral("U6 %1/%2 · U7 %3/%4")
        .arg(m_seq[0].lost()).arg(m_seq[0].received())
        .arg(m_seq[1].lost()).arg(m_seq[1].received());
}

QString CanBus::busStateText() const
{
    if (!m_state.supported)
        return QString(); // UDP bus / adapter without error counters
    const QString cnt = QStringLiteral("TEC %1 · REC %2").arg(m_state.txErrors).arg(m_state.rxErrors);
    if (m_state.busOff)
        return QStringLiteral("BUS-OFF · ") + cnt;
    if (m_state.passive)
        return QStringLiteral("error passive · ") + cnt;
    if (m_state.warning)
        return QStringLiteral("cảnh báo · ") + cnt;
    return QStringLiteral("bus OK · ") + cnt;
}

bool CanBus::open()
{
    if (m_connected || m_opening)
        return true;
    setError(QString());
    m_opening = true;
    m_desc.clear();
    m_state = CanBusState();
    emit connectedChanged();
    emit startWorker(m_cfg);
    return true;
}

void CanBus::close()
{
    if (m_virtual) {
        m_virtual = false;
        setConnected(false);
        return;
    }
    if (!m_connected && !m_opening)
        return;
    m_posTimer.stop();
    m_posPending = false;
    m_opening = false;
    emit stopWorker(); // after the frames already queued (D on Disconnect)
    setConnected(false);
}

void CanBus::openVirtual()
{
    if (m_connected)
        return;
    m_virtual = true;
    m_virtualSent.clear();
    m_desc = QStringLiteral("virtual");
    setConnected(true);
}

void CanBus::injectFrame(const Frame &f)
{
    Frame r = f;
    r.t = now();
    onReceived({r});
}

void CanBus::onOpened(bool ok, const QString &error, const QString &description)
{
    m_opening = false;
    if (!ok) {
        setError(error);
        emit connectedChanged();
        return;
    }
    m_desc = description;
    setConnected(true);
}

void CanBus::onClosed()
{
    m_state = CanBusState();
    emit statsChanged();
}

void CanBus::setConnected(bool c)
{
    if (c == m_connected)
        return;
    m_connected = c;
    if (c) {
        m_clock.start();
        for (int i = 0; i < 2; ++i) {
            m_text[i].clear();
            m_seq[i].reset();
        }
        m_pendingPos = QVector<double>(6, qQNaN());
        m_posPending = false;
        m_lastPos.invalidate();
    }
    emit connectedChanged();
    for (CanNodeLink *n : m_nodes)
        emit n->connectedChanged();
    emit statsChanged();
}

void CanBus::setError(const QString &e)
{
    if (e == m_error)
        return;
    m_error = e;
    emit errorChanged();
    for (CanNodeLink *n : m_nodes)
        emit n->errorStringChanged();
}

bool CanBus::queue(const Frame &f)
{
    if (!m_connected)
        return false;
    if (m_virtual) {
        Frame s = f;
        s.tx = true;
        s.t = now();
        m_virtualSent << s;
        onSent({s});
        return true;
    }
    emit transmit(f);
    return true;
}

bool CanBus::sendText(int node, const QString &line)
{
    if (!m_connected)
        return false;
    // one CMD_TEXT frame = one line for the firmware
    if (line.toUtf8().size() > 64) {
        setError(tr("Lệnh dài hơn 64 byte, không gửi được qua CAN: %1").arg(line.left(40)));
        return false;
    }
    bool ok = true;
    for (const Frame &f : CanV1::encodeText(node, line))
        ok = queue(f) && ok;
    return ok;
}

void CanBus::sendPosAll(const QVector<double> &deg)
{
    if (!m_connected)
        return;
    for (int i = 0; i < 6 && i < deg.size(); ++i)
        if (std::isfinite(deg[i]))
            m_pendingPos[i] = deg[i];
    m_posPending = true;
    // at most 100 frames/s: a faster caller gets its latest values merged into the next frame
    if (m_virtual || !m_lastPos.isValid() || m_lastPos.elapsed() >= 10)
        flushPos();
    else if (!m_posTimer.isActive())
        m_posTimer.start(int(qMax<qint64>(1, 10 - m_lastPos.elapsed())));
}

void CanBus::flushPos()
{
    if (!m_posPending)
        return;
    Frame f;
    f.id = CanV1::IdPosAll;
    f.data = CanV1::encodePosAll(m_pendingPos);
    m_pendingPos = QVector<double>(6, qQNaN());
    m_posPending = false;
    m_lastPos.start();
    queue(f);
}

bool CanBus::estop()
{
    m_posTimer.stop();
    m_posPending = false;
    m_pendingPos = QVector<double>(6, qQNaN());
    Frame f;
    f.id = CanV1::IdEStop;
    return queue(f);
}

bool CanBus::sendRaw(const QString &idHex, const QString &dataHex, bool fd, bool brs)
{
    QString idt = idHex.trimmed();
    if (idt.startsWith(QLatin1String("0x"), Qt::CaseInsensitive))
        idt = idt.mid(2);
    bool okId = false, okData = false;
    const quint32 id = idt.toUInt(&okId, 16);
    QByteArray data = CanV1::parseHex(dataHex, &okData);
    if (!okId || id > 0x1FFFFFFF) {
        setError(tr("ID không hợp lệ: %1").arg(idHex));
        return false;
    }
    if (!okData || data.size() > (fd ? 64 : 8)) {
        setError(tr("Dữ liệu hex không hợp lệ hoặc quá dài (%1 tối đa %2 byte)").arg(fd ? "FD" : "CAN").arg(fd ? 64 : 8));
        return false;
    }
    Frame f;
    f.id = id;
    f.ext = id > 0x7FF;
    f.fd = fd;
    f.brs = fd && brs;
    f.data = fd ? CanV1::padFd(data) : data;
    if (!queue(f)) {
        setError(tr("CAN chưa kết nối"));
        return false;
    }
    return true;
}

void CanBus::onReceived(const QVector<Frame> &frames)
{
    m_rxFrames += frames.size();
    for (const Frame &f : frames)
        dispatch(f);
    if (!m_monitor->paused())
        m_monitorBatch << frames;
}

void CanBus::onSent(const QVector<Frame> &frames)
{
    m_txFrames += frames.size();
    if (!m_monitor->paused())
        m_monitorBatch << frames;
}

void CanBus::dispatch(const Frame &f)
{
    const quint32 id = f.id;
    if (f.ext)
        return;
    if (id == CanV1::IdText + 1 || id == CanV1::IdText + 2) {
        const int n = int(id - CanV1::IdText);
        CanNodeLink *node = m_nodes[n - 1];
        node->noteRxBytes(f.data.size());
        for (const QString &line : m_text[n - 1].feed(f.data))
            node->deliver(line, f.t, 0);
        return;
    }
    if (id == CanV1::IdStatus + 1 || id == CanV1::IdStatus + 2) {
        const int n = int(id - CanV1::IdStatus);
        CanV1::Status s;
        if (!CanV1::decodeStatus(f.data, &s))
            return;
        m_seq[n - 1].feed(s.seq);
        m_nodes[n - 1]->deliver(CanV1::statusToLogLine(s, m_headJoint), f.t, f.data.size());
    }
    // our own HB / POS_ALL / CMD echoes, ping / pong: monitor only
}

void CanBus::updateStats()
{
    const double dt = m_statsTimer.interval() / 1000.0;
    m_rxRate = m_rxFrames / dt;
    m_txRate = m_txFrames / dt;
    m_rxFrames = m_txFrames = 0;
    emit statsChanged();
}
