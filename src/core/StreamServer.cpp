#include "StreamServer.h"

#include "HandModel.h"
#include "LineParser.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkDatagram>
#include <QtMath>

StreamServer::StreamServer(HandModel *hand, QObject *parent)
    : QObject(parent), m_hand(hand)
{
    const int n = hand->count();
    m_raw = QVector<double>(n, qQNaN());
    m_out = QVector<double>(n, qQNaN());
    m_calMin = QVector<double>(n, qQNaN());
    m_calMax = QVector<double>(n, qQNaN());
    m_hist = QVector<QVector<double>>(n);
    m_avg = QVector<double>(n, qQNaN());
    m_lp = QVector<double>(n, qQNaN());
    m_proc = QVector<double>(n, qQNaN());
    m_vel = QVector<double>(n, 0.0);

    connect(&m_sock, &QUdpSocket::readyRead, this, &StreamServer::onReadyRead);
    m_sendTimer.setTimerType(Qt::PreciseTimer);
    m_sendTimer.setInterval(int(1000 / m_rate));
    connect(&m_sendTimer, &QTimer::timeout, this, &StreamServer::onSendTick);
    m_sendTimer.start();
    m_statsTimer.setInterval(500);
    connect(&m_statsTimer, &QTimer::timeout, this, [this]() {
        m_fps = m_framesWindow * 2.0;
        m_framesWindow = 0;
        emit statsChanged();
    });
    m_statsTimer.start();

    m_demoTimer.setInterval(20);
    connect(&m_demoTimer, &QTimer::timeout, this, [this]() {
        const double t = m_demoClock.nsecsElapsed() / 1e9;
        QStringList parts;
        for (int i = 0; i < m_hand->count(); ++i) {
            const double v = 50 - 50 * qCos(2 * M_PI * 0.25 * t - i * 0.6);
            parts << LineParser::num(v, 1);
        }
        const int u = m_unit;
        m_unit = Percent; // the demo source always speaks percent
        injectText(parts.join(QLatin1Char(',')), QStringLiteral("demo"));
        m_unit = u;
    });
}

void StreamServer::setPort(int p)
{
    p = qBound(1, p, 65535);
    if (p == m_port)
        return;
    m_port = p;
    emit portChanged();
    if (listening()) {
        stop();
        start();
    }
}

void StreamServer::setUnit(int u)
{
    u = qBound(0, u, 2);
    if (u == m_unit)
        return;
    m_unit = u;
    emit settingsChanged();
}

void StreamServer::setMirror(bool m)
{
    if (m == m_mirror)
        return;
    m_mirror = m;
    emit settingsChanged();
}

void StreamServer::setSendRate(double r)
{
    r = qBound(1.0, r, 200.0);
    if (qFuzzyCompare(r, m_rate))
        return;
    m_rate = r;
    m_sendTimer.setInterval(qMax(1, int(1000.0 / r)));
    emit settingsChanged();
}

void StreamServer::setDemoSource(bool d)
{
    if (d == m_demo)
        return;
    m_demo = d;
    if (d) {
        m_demoClock.start();
        m_demoTimer.start();
    } else {
        m_demoTimer.stop();
    }
    emit demoSourceChanged();
}

int StreamServer::ageMs() const
{
    return m_lastFrame.isValid() ? int(qMin<qint64>(m_lastFrame.elapsed(), 9999999)) : -1;
}

static QVariantList toList(const QVector<double> &v)
{
    QVariantList l;
    for (double x : v)
        l << (qIsNaN(x) ? QVariant() : QVariant(x));
    return l;
}

QVariantList StreamServer::raw() const { return toList(m_raw); }
QVariantList StreamServer::processed() const { return toList(m_proc); }

QStringList StreamServer::handedNames() const
{
    return {tr("Bất kỳ"), tr("Phải"), tr("Trái")};
}

void StreamServer::setHanded(int h)
{
    h = qBound(0, h, 2);
    if (h == m_handed)
        return;
    m_handed = h;
    emit processingChanged();
}

void StreamServer::setAverageN(int n)
{
    n = qBound(1, n, 100);
    if (n == m_avgN)
        return;
    m_avgN = n;
    emit processingChanged();
}

void StreamServer::setFilterHz(double f)
{
    f = qBound(0.0, f, 100.0);
    if (qFuzzyCompare(f + 1, m_filterHz + 1))
        return;
    m_filterHz = f;
    emit processingChanged();
}

void StreamServer::setMaxVel(double v)
{
    v = qBound(0.0, v, 10000.0);
    if (qFuzzyCompare(v + 1, m_maxVel + 1))
        return;
    m_maxVel = v;
    emit processingChanged();
}

void StreamServer::setMaxAccel(double a)
{
    a = qBound(0.0, a, 100000.0);
    if (qFuzzyCompare(a + 1, m_maxAcc + 1))
        return;
    m_maxAcc = a;
    emit processingChanged();
}

void StreamServer::resetProcessing()
{
    for (auto &h : m_hist)
        h.clear();
    m_avg.fill(qQNaN());
    m_lp.fill(qQNaN());
    m_proc.fill(qQNaN());
    m_vel.fill(0.0);
    m_haveFrame = false;
    emit processedChanged();
}

void StreamServer::processStep(double dt)
{
    dt = qMax(1e-4, dt);
    const double alpha = m_filterHz > 0 ? 1.0 - std::exp(-2.0 * M_PI * m_filterHz * dt) : 1.0;
    for (int i = 0; i < m_avg.size(); ++i) {
        const double x = m_avg[i];
        if (qIsNaN(x))
            continue;
        // low-pass
        if (qIsNaN(m_lp[i]))
            m_lp[i] = x;
        else
            m_lp[i] += alpha * (x - m_lp[i]);
        // rate limits
        if (qIsNaN(m_proc[i]) || (m_maxVel <= 0 && m_maxAcc <= 0)) {
            m_vel[i] = qIsNaN(m_proc[i]) ? 0.0 : (m_lp[i] - m_proc[i]) / dt;
            m_proc[i] = m_lp[i];
            continue;
        }
        const double e = m_lp[i] - m_proc[i];
        double vdes = e / dt;
        if (m_maxVel > 0)
            vdes = qBound(-m_maxVel, vdes, m_maxVel);
        if (m_maxAcc > 0) {
            // no overshoot: never faster than what can still stop at the target
            const double vstop = std::sqrt(2.0 * m_maxAcc * qAbs(e));
            vdes = qBound(-vstop, vdes, vstop);
            const double dv = qBound(-m_maxAcc * dt, vdes - m_vel[i], m_maxAcc * dt);
            m_vel[i] += dv;
        } else {
            m_vel[i] = vdes;
        }
        double next = m_proc[i] + m_vel[i] * dt;
        // do not step past the target
        if ((e > 0 && next > m_lp[i]) || (e < 0 && next < m_lp[i])) {
            next = m_lp[i];
            m_vel[i] = 0;
        }
        m_proc[i] = next;
    }
}
QVariantList StreamServer::output() const { return toList(m_out); }
QVariantList StreamServer::calMin() const { return toList(m_calMin); }
QVariantList StreamServer::calMax() const { return toList(m_calMax); }

QStringList StreamServer::unitNames() const
{
    return {tr("Độ"), tr("% hành trình"), tr("Thô + hiệu chuẩn")};
}

void StreamServer::setError(const QString &e)
{
    if (e == m_error)
        return;
    m_error = e;
    emit lastErrorChanged();
}

bool StreamServer::start()
{
    if (listening())
        return true;
    if (!m_sock.bind(QHostAddress::Any, quint16(m_port), QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        setError(m_sock.errorString());
        emit listeningChanged();
        return false;
    }
    setError(QString());
    emit listeningChanged();
    return true;
}

void StreamServer::stop()
{
    m_sock.close();
    emit listeningChanged();
}

bool StreamServer::toggle()
{
    if (listening()) {
        stop();
        return false;
    }
    return start();
}

void StreamServer::onReadyRead()
{
    while (m_sock.hasPendingDatagrams()) {
        const QNetworkDatagram dg = m_sock.receiveDatagram();
        QVector<double> raw;
        int hand = AnyHand;
        if (!parseFrame(dg.data(), &raw, &hand))
            continue;
        if (m_handed != AnyHand && hand != AnyHand && hand != m_handed) {
            ++m_skipped;
            continue;
        }
        handleFrame(raw, dg.senderAddress().toString() + QLatin1Char(':') + QString::number(dg.senderPort()));
    }
}

bool StreamServer::injectText(const QString &text, const QString &sender)
{
    QVector<double> raw;
    int hand = AnyHand;
    if (!parseFrame(text.toUtf8(), &raw, &hand))
        return false;
    if (m_handed != AnyHand && hand != AnyHand && hand != m_handed) {
        ++m_skipped;
        return true;
    }
    handleFrame(raw, sender);
    return true;
}

bool StreamServer::parseFrame(const QByteArray &data, QVector<double> *out, int *hand) const
{
    const int n = m_hand->count();
    out->fill(qQNaN(), n);
    QByteArray trimmed = data.trimmed();
    if (hand)
        *hand = AnyHand;
    // "L ..." / "R ..." prefix
    if (trimmed.size() > 2 && (trimmed[0] == 'L' || trimmed[0] == 'R') && (trimmed[1] == ' ' || trimmed[1] == ':' || trimmed[1] == ';')) {
        if (hand)
            *hand = trimmed[0] == 'L' ? LeftHand : RightHand;
        trimmed = trimmed.mid(2).trimmed();
    }
    if (trimmed.isEmpty())
        return false;
    bool any = false;
    if (trimmed.startsWith('{') || trimmed.startsWith('[')) {
        const QJsonDocument doc = QJsonDocument::fromJson(trimmed);
        if (doc.isNull())
            return false;
        QJsonArray arr;
        if (doc.isArray()) {
            arr = doc.array();
        } else {
            const QJsonObject o = doc.object();
            const QString side = o.value(QStringLiteral("hand")).toString().toLower();
            if (hand && side.startsWith(QLatin1Char('l')))
                *hand = LeftHand;
            else if (hand && side.startsWith(QLatin1Char('r')))
                *hand = RightHand;
            if (o.contains(QStringLiteral("q")))
                arr = o.value(QStringLiteral("q")).toArray();
            for (auto it = o.begin(); it != o.end(); ++it) {
                const int j = m_hand->indexOfKey(it.key());
                if (j >= 0 && it.value().isDouble()) {
                    (*out)[j] = it.value().toDouble();
                    any = true;
                }
            }
        }
        for (int i = 0; i < arr.size() && i < n; ++i) {
            if (arr[i].isDouble()) {
                (*out)[i] = arr[i].toDouble();
                any = true;
            }
        }
        return any;
    }
    const ParsedLine p = LineParser::parse(QString::fromUtf8(trimmed));
    if (!p.telemetry)
        return false;
    for (int k = 0; k < p.names.size(); ++k) {
        const QString &name = p.names[k];
        int j = m_hand->indexOfKey(name);
        if (j < 0 && name.size() >= 2 && (name[0] == QLatin1Char('q') || name.startsWith(QLatin1String("ch"))))
            j = name.mid(name[0] == QLatin1Char('q') ? 1 : 2).toInt() - 1;
        if (j >= 0 && j < n) {
            (*out)[j] = p.values[k];
            any = true;
        }
    }
    return any;
}

double StreamServer::mapValue(int ch, double v) const
{
    if (qIsNaN(v))
        return v;
    const Joint *j = m_hand->joint(ch);
    const double lo = j->minDeg(), hi = j->maxDeg();
    switch (m_unit) {
    case Degrees:
        return j->clamp(v);
    case Percent:
        return j->clamp(lo + (hi - lo) * v / 100.0);
    default: {
        const double a = m_calMin[ch], b = m_calMax[ch];
        if (qIsNaN(a) || qIsNaN(b) || qFuzzyCompare(a + 1, b + 1))
            return j->clamp(v);
        return j->clamp(lo + (hi - lo) * (v - a) / (b - a));
    }
    }
}

void StreamServer::handleFrame(const QVector<double> &raw, const QString &sender)
{
    for (int i = 0; i < raw.size() && i < m_raw.size(); ++i) {
        if (qIsNaN(raw[i]))
            continue;
        m_raw[i] = raw[i];
        if (m_calibrating) {
            m_calMin[i] = qIsNaN(m_calMin[i]) ? raw[i] : qMin(m_calMin[i], raw[i]);
            m_calMax[i] = qIsNaN(m_calMax[i]) ? raw[i] : qMax(m_calMax[i], raw[i]);
        }
        m_out[i] = mapValue(i, raw[i]);
        QVector<double> &h = m_hist[i];
        h.append(m_out[i]);
        while (h.size() > m_avgN)
            h.removeFirst();
        double sum = 0;
        for (double x : h)
            sum += x;
        m_avg[i] = sum / h.size();
    }
    m_haveFrame = true;
    ++m_frames;
    ++m_framesWindow;
    m_sender = sender;
    m_lastFrame.start();
    m_pending = true;
    emit frameChanged();
    if (m_calibrating)
        emit calibrationChanged();
}

void StreamServer::onSendTick()
{
    if (!m_haveFrame)
        return;
    const QVector<double> before = m_proc;
    processStep(1.0 / m_rate);
    bool moved = m_pending;
    for (int i = 0; i < m_proc.size() && !moved; ++i)
        moved = qIsNaN(before[i]) != qIsNaN(m_proc[i]) || (!qIsNaN(m_proc[i]) && qAbs(before[i] - m_proc[i]) > 1e-3);
    m_pending = false;
    if (!moved)
        return;
    emit processedChanged();
    emit preview(m_proc);
    if (m_mirror)
        emit forward(m_proc);
}

void StreamServer::startCalibration()
{
    m_calMin.fill(qQNaN());
    m_calMax.fill(qQNaN());
    m_calibrating = true;
    emit calibrationChanged();
}

void StreamServer::finishCalibration()
{
    m_calibrating = false;
    m_calAt = QDateTime::currentDateTime().toString(QStringLiteral("dd/MM/yy HH:mm"));
    setUnit(Calibrated);
    emit calibrationChanged();
}

void StreamServer::clearCalibration()
{
    m_calibrating = false;
    m_calMin.fill(qQNaN());
    m_calMax.fill(qQNaN());
    m_calAt.clear();
    emit calibrationChanged();
}

QJsonObject StreamServer::toJson() const
{
    QJsonArray mn, mx;
    for (int i = 0; i < m_calMin.size(); ++i) {
        mn.append(qIsNaN(m_calMin[i]) ? QJsonValue() : QJsonValue(m_calMin[i]));
        mx.append(qIsNaN(m_calMax[i]) ? QJsonValue() : QJsonValue(m_calMax[i]));
    }
    return {{QStringLiteral("port"), m_port}, {QStringLiteral("unit"), m_unit},
            {QStringLiteral("sendRate"), m_rate}, {QStringLiteral("calMin"), mn},
            {QStringLiteral("calMax"), mx}, {QStringLiteral("calibratedAt"), m_calAt},
            {QStringLiteral("handed"), m_handed}, {QStringLiteral("averageN"), m_avgN},
            {QStringLiteral("filterHz"), m_filterHz}, {QStringLiteral("maxVel"), m_maxVel},
            {QStringLiteral("maxAccel"), m_maxAcc}};
}

void StreamServer::fromJson(const QJsonObject &o)
{
    setPort(o.value(QStringLiteral("port")).toInt(m_port));
    setUnit(o.value(QStringLiteral("unit")).toInt(m_unit));
    setSendRate(o.value(QStringLiteral("sendRate")).toDouble(m_rate));
    const QJsonArray mn = o.value(QStringLiteral("calMin")).toArray();
    const QJsonArray mx = o.value(QStringLiteral("calMax")).toArray();
    for (int i = 0; i < m_calMin.size(); ++i) {
        m_calMin[i] = mn.at(i).isDouble() ? mn.at(i).toDouble() : qQNaN();
        m_calMax[i] = mx.at(i).isDouble() ? mx.at(i).toDouble() : qQNaN();
    }
    m_calAt = o.value(QStringLiteral("calibratedAt")).toString();
    setHanded(o.value(QStringLiteral("handed")).toInt(m_handed));
    setAverageN(o.value(QStringLiteral("averageN")).toInt(m_avgN));
    setFilterHz(o.value(QStringLiteral("filterHz")).toDouble(m_filterHz));
    setMaxVel(o.value(QStringLiteral("maxVel")).toDouble(m_maxVel));
    setMaxAccel(o.value(QStringLiteral("maxAccel")).toDouble(m_maxAcc));
    emit calibrationChanged();
}
