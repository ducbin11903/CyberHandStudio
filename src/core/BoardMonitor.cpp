#include "BoardMonitor.h"

#include "HandController.h"
#include "LineParser.h"

#include <QRegularExpression>
#include <QtMath>

BoardMonitor::BoardMonitor(HandController *ctrl, QObject *parent)
    : QObject(parent), m_ctrl(ctrl)
{
    m_clock.start();
    m_tickTimer.setInterval(500);
    connect(&m_tickTimer, &QTimer::timeout, this, [this]() {
        if (m_seen)
            emit tick();
    });
    m_tickTimer.start();
}

// ---------------------------------------------------------------------------
// parsing

bool BoardMonitor::parseSense(const QString &line, Sense *out)
{
    const QString s = line.trimmed();
    static const QRegularExpression reHead(QStringLiteral("^OK\\s+sense\\b\\s*(U\\d+)?"), QRegularExpression::CaseInsensitiveOption);
    const auto mh = reHead.match(s);
    if (!mh.hasMatch())
        return false;
    Sense r;
    r.ok = true;
    r.mcu = mh.captured(1).toUpper();
    auto num = [](const QString &v) {
        bool ok = false;
        const double d = v.toDouble(&ok);
        return ok ? d : qQNaN();
    };
    // values: T=25.0 C RH=45.0 % VBAT=7.99 V (n/a when invalid)
    static const QRegularExpression reT(QStringLiteral("\\bT\\s*=\\s*(-?[0-9.]+|n/?a)"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression reRh(QStringLiteral("\\bRH\\s*=\\s*(-?[0-9.]+|n/?a)"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression reV(QStringLiteral("\\bVBAT\\s*=\\s*(-?[0-9.]+|n/?a)"), QRegularExpression::CaseInsensitiveOption);
    // thresholds: off at T>=70.0 C (on <60.0) VBAT<6.40 V (on >=6.80)
    static const QRegularExpression reTh(QStringLiteral("\\bT\\s*>=\\s*(-?[0-9.]+)\\s*C?\\s*\\(\\s*on\\s*<\\s*(-?[0-9.]+)\\s*\\)"),
                                         QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression reVh(QStringLiteral("\\bVBAT\\s*<\\s*([0-9.]+)\\s*V?\\s*\\(\\s*on\\s*>=\\s*([0-9.]+)\\s*\\)"),
                                         QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression reHot(QStringLiteral("\\bhot\\s*=\\s*(\\d)"));
    static const QRegularExpression reLow(QStringLiteral("\\blow\\s*=\\s*(\\d)"));
    static const QRegularExpression reI2c(QStringLiteral("\\bi2c_err\\s*=\\s*(\\d+)"));
    auto m = reT.match(s);
    if (m.hasMatch())
        r.t = num(m.captured(1));
    m = reRh.match(s);
    if (m.hasMatch())
        r.rh = num(m.captured(1));
    m = reV.match(s);
    if (m.hasMatch())
        r.vbat = num(m.captured(1));
    if (s.contains(QLatin1String("T invalid"), Qt::CaseInsensitive)) {
        r.t = qQNaN();
        r.rh = qQNaN();
    }
    if (s.contains(QLatin1String("VBAT not measured"), Qt::CaseInsensitive))
        r.vbat = qQNaN();
    m = reTh.match(s);
    if (m.hasMatch()) {
        r.tOff = num(m.captured(1));
        r.tOn = num(m.captured(2));
    }
    m = reVh.match(s);
    if (m.hasMatch()) {
        r.vOff = num(m.captured(1));
        r.vOn = num(m.captured(2));
    }
    m = reHot.match(s);
    if (m.hasMatch())
        r.hot = m.captured(1).toInt();
    m = reLow.match(s);
    if (m.hasMatch())
        r.low = m.captured(1).toInt();
    m = reI2c.match(s);
    if (m.hasMatch())
        r.i2cErr = m.captured(1).toInt();
    *out = r;
    return true;
}

double BoardMonitor::lipoPercent(double v)
{
    // LiPo resting voltage per cell -> charge; 3.20 V empty (6.4 V for 2S), 4.20 V full
    static const double table[][2] = {
        {3.20, 0}, {3.50, 3}, {3.61, 5}, {3.69, 10}, {3.71, 15}, {3.73, 20}, {3.75, 25}, {3.77, 30},
        {3.79, 35}, {3.80, 40}, {3.82, 45}, {3.84, 50}, {3.85, 55}, {3.87, 60}, {3.91, 65}, {3.95, 70},
        {3.98, 75}, {4.02, 80}, {4.08, 85}, {4.11, 90}, {4.15, 95}, {4.20, 100}};
    const int n = int(sizeof(table) / sizeof(table[0]));
    if (qIsNaN(v))
        return qQNaN();
    if (v <= table[0][0])
        return 0;
    if (v >= table[n - 1][0])
        return 100;
    for (int i = 1; i < n; ++i) {
        if (v <= table[i][0]) {
            const double f = (v - table[i - 1][0]) / (table[i][0] - table[i - 1][0]);
            return table[i - 1][1] + f * (table[i][1] - table[i - 1][1]);
        }
    }
    return 100;
}

// ---------------------------------------------------------------------------
// state

double BoardMonitor::batteryPct() const
{
    if (!batteryPresent() || m_cells < 1)
        return qQNaN();
    return lipoPercent(m_vbat / m_cells);
}

void BoardMonitor::setCells(int c)
{
    c = qBound(1, c, 6);
    if (c == m_cells)
        return;
    m_cells = c;
    emit settingsChanged();
    emit updated();
}

int BoardMonitor::ageMs() const
{
    return m_age.isValid() ? int(qMin<qint64>(m_age.elapsed(), 99999999)) : -1;
}

void BoardMonitor::setSupported(bool s)
{
    if (s == m_supported)
        return;
    m_supported = s;
    emit updated();
}

int BoardMonitor::tempLevel() const
{
    if (m_otemp == 2 || m_hot)
        return 2;
    if (m_hasTemp && !qIsNaN(m_tOff) && m_temp >= m_tOff - 10.0)
        return 1;
    return 0;
}

int BoardMonitor::vbatLevel() const
{
    if (m_vbatSt == 2 || m_low)
        return 2;
    if (batteryPresent() && !qIsNaN(m_vOn) && m_vbat < m_vOn)
        return 1;
    return 0;
}

QString BoardMonitor::otempText() const
{
    if (m_otemp == 1)
        return tr("Đã an toàn — bấm Enable để chạy lại");
    if (m_otemp != 2)
        return QString();
    const double t = !qIsNaN(m_tripTemp) ? m_tripTemp : (m_hasTemp ? m_temp : qQNaN());
    if (qIsNaN(t))
        return tr("Quá nhiệt — đã tắt cả 6 motor");
    return tr("Quá nhiệt %1 °C — đã tắt cả 6 motor").arg(QString::number(t, 'f', 1));
}

QString BoardMonitor::vbatText() const
{
    if (m_vbatSt == 1)
        return tr("Đã an toàn — bấm Enable để chạy lại");
    if (m_vbatSt != 2)
        return QString();
    const double v = !qIsNaN(m_tripVbat) ? m_tripVbat : (m_hasVbat ? m_vbat : qQNaN());
    if (qIsNaN(v))
        return tr("Pin yếu — đã tắt cả 6 motor");
    return tr("Pin yếu %1 V — đã tắt cả 6 motor").arg(QString::number(v, 'f', 2));
}

QString BoardMonitor::blockReason() const
{
    QStringList l;
    if (m_otemp == 2)
        l << (qIsNaN(m_tOn) ? tr("Quá nhiệt: firmware giữ motor tắt cho tới khi nguội")
                            : tr("Quá nhiệt: firmware giữ motor tắt cho tới khi < %1 °C").arg(QString::number(m_tOn, 'f', 1)));
    if (m_vbatSt == 2)
        l << (qIsNaN(m_vOn) ? tr("Pin yếu: firmware giữ motor tắt cho tới khi sạc / thay pin")
                            : tr("Pin yếu: firmware giữ motor tắt cho tới khi VBAT ≥ %1 V").arg(QString::number(m_vOn, 'f', 2)));
    return l.join(QLatin1Char('\n'));
}

double BoardMonitor::historyNow() const
{
    return m_clock.elapsed() / 1000.0;
}

QVariantList BoardMonitor::toList(const QVector<QPointF> &h)
{
    QVariantList l;
    l.reserve(h.size());
    for (const QPointF &p : h)
        l << p;
    return l;
}

void BoardMonitor::addHistory(QVector<QPointF> &h, double v)
{
    const double t = m_clock.elapsed() / 1000.0;
    if (!h.isEmpty() && t - h.last().x() < 1.0)
        return;
    h.append(QPointF(t, v));
    while (!h.isEmpty() && t - h.first().x() > 600.0)
        h.removeFirst();
    emit historyChanged();
}

void BoardMonitor::setThresholds(double tOff, double tOn, double vOff, double vOn)
{
    bool changed = false;
    auto set = [&changed](double &dst, double v) {
        if (qIsNaN(v) || (!qIsNaN(dst) && qFuzzyCompare(dst + 1000, v + 1000)))
            return;
        dst = v;
        changed = true;
    };
    set(m_tOff, tOff);
    set(m_tOn, tOn);
    set(m_vOff, vOff);
    set(m_vOn, vOn);
    if (changed) {
        emit thresholdsChanged();
        emit updated();
    }
}

void BoardMonitor::setState(int cause, int state, const QString &eventText, bool fromEvent)
{
    int &st = cause == 0 ? m_otemp : m_vbatSt;
    const bool changed = st != state;
    st = state;
    if (changed) {
        emit alarmChanged();
        emit updated();
    }
    if (state == 0 || eventText.isEmpty() || (!changed && !fromEvent))
        return;
    // the same event from U6 and "[U7]" (or from both ports) arrives twice within ~1 s
    const QString key = QStringLiteral("%1:%2").arg(cause).arg(state);
    const qint64 now = m_clock.elapsed();
    if (m_lastEvent.contains(key) && now - m_lastEvent.value(key) < 1500)
        return;
    m_lastEvent.insert(key, now);
    emit event(cause == 0 ? QStringLiteral("OTEMP") : QStringLiteral("VBAT"), eventText, state == 2);
}

void BoardMonitor::updateFromLog(const QStringList &names, const QVector<double> &values, const QString &src)
{
    auto field = [&](const char *n, double *v) {
        const int i = names.indexOf(QLatin1String(n));
        if (i < 0 || i >= values.size() || !std::isfinite(values[i]))
            return false;
        *v = values[i];
        return true;
    };
    double t = 0, rh = 0, vb = 0;
    const bool ht = field("temp", &t), hr = field("rh", &rh), hv = field("vbat", &vb);
    if (!ht && !hr && !hv && !m_seen)
        return; // firmware without board sensors
    const bool flagsChanged = ht != m_hasTemp || hr != m_hasRh || hv != m_hasVbat;
    m_hasTemp = ht;
    m_hasRh = hr;
    m_hasVbat = hv;
    if (ht)
        m_temp = t;
    if (hr)
        m_rh = rh;
    if (hv)
        m_vbat = vb;
    if (ht || hr || hv) {
        m_seen = true;
        m_age.start();
        if (!src.isEmpty())
            m_source = src;
    }
    if (ht)
        addHistory(m_tempHist, t);
    if (hv)
        addHistory(m_vbatHist, vb);
    if (ht || hr || hv || flagsChanged)
        emit updated();
}

void BoardMonitor::updateFault(int flt)
{
    const int prev = m_flt;
    m_flt = flt;
    // right after an `e` the log may still carry the old bits for a moment
    const bool settling = m_ackClock.isValid() && m_ackClock.elapsed() < 1500;
    for (int cause = 0; cause < 2; ++cause) {
        const int bit = cause == 0 ? 16 : 32;
        const int st = cause == 0 ? m_otemp : m_vbatSt;
        if ((flt & bit) && st == 0 && !settling) {
            // connected after the trip, or the event line was lost
            setState(cause, 2, QString(), false);
            emit event(cause == 0 ? QStringLiteral("OTEMP") : QStringLiteral("VBAT"),
                       cause == 0 ? otempText() : vbatText(), true);
            m_lastEvent.insert(QStringLiteral("%1:2").arg(cause), m_clock.elapsed());
        } else if (!(flt & bit) && (prev & bit) && st != 0) {
            setState(cause, 0, QString(), false); // motors allowed again
        }
    }
    if ((flt ^ prev) & (16 | 32))
        emit updated();
}

bool BoardMonitor::processText(const QString &line)
{
    const QString s = line.trimmed();
    Sense sense;
    if (parseSense(s, &sense)) {
        m_supported = true;
        m_senseError.clear();
        if (!sense.mcu.isEmpty())
            m_source = sense.mcu;
        setThresholds(sense.tOff, sense.tOn, sense.vOff, sense.vOn);
        if (sense.i2cErr >= 0)
            m_i2cErr = sense.i2cErr;
        // snapshot (also when the log is off)
        m_hasTemp = !qIsNaN(sense.t);
        m_hasRh = !qIsNaN(sense.rh);
        m_hasVbat = !qIsNaN(sense.vbat);
        if (m_hasTemp)
            m_temp = sense.t;
        if (m_hasRh)
            m_rh = sense.rh;
        if (m_hasVbat)
            m_vbat = sense.vbat;
        if (m_hasTemp || m_hasRh || m_hasVbat) {
            m_seen = true;
            m_age.start();
        }
        if (sense.hot >= 0) {
            m_hot = sense.hot == 1;
            if (m_hot && m_otemp != 2)
                setState(0, 2, otempTextFor(m_hasTemp ? m_temp : qQNaN()), false);
            else if (!m_hot && m_otemp == 2)
                setState(0, 1, tr("Hết quá nhiệt — bấm Enable để chạy lại"), false);
        }
        if (sense.low >= 0) {
            m_low = sense.low == 1;
            if (m_low && m_vbatSt != 2)
                setState(1, 2, vbatTextFor(m_hasVbat ? m_vbat : qQNaN()), false);
            else if (!m_low && m_vbatSt == 2)
                setState(1, 1, tr("Pin đã ổn — bấm Enable để chạy lại"), false);
        }
        emit updated();
        return true;
    }
    if (s.startsWith(QLatin1String("ERR no sensor data"), Qt::CaseInsensitive)) {
        m_supported = true;
        m_senseError = tr("Không có số liệu cảm biến (SHT40 + VBAT ở U7, link tới U7 mất)");
        emit updated();
        return true;
    }
    // `e` refused while the board protection holds the motors off
    if (s.startsWith(QLatin1String("ERR board over-temperature"), Qt::CaseInsensitive)) {
        m_hot = true;
        setState(0, 2, tr("Enable bị từ chối: quá nhiệt, motor vẫn tắt"), true);
        return true;
    }
    if (s.startsWith(QLatin1String("ERR board battery low"), Qt::CaseInsensitive)) {
        m_low = true;
        setState(1, 2, tr("Enable bị từ chối: pin yếu, motor vẫn tắt"), true);
        return true;
    }
    // OTEMP: board 72.0 C >= 70.0 C (SHT40 on U7), all motors off (flt 16)
    // OTEMP: cleared (< 60.0 C), send e to run again
    static const QRegularExpression reOt(QStringLiteral("^OTEMP:\\s*board\\s+(-?[0-9.]+)\\s*C\\s*>=\\s*(-?[0-9.]+)"),
                                         QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression reOc(QStringLiteral("^OTEMP:\\s*cleared\\s*\\(\\s*<\\s*(-?[0-9.]+)"),
                                         QRegularExpression::CaseInsensitiveOption);
    // VBAT: battery 6.31 V < 6.40 V, all motors off (flt 32)
    // VBAT: ok (7.40 V), send e to run again
    static const QRegularExpression reVl(QStringLiteral("^VBAT:\\s*battery\\s+([0-9.]+)\\s*V\\s*<\\s*([0-9.]+)"),
                                         QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression reVo(QStringLiteral("^VBAT:\\s*ok\\s*\\(\\s*([0-9.]+)\\s*V"),
                                         QRegularExpression::CaseInsensitiveOption);
    auto m = reOt.match(s);
    if (m.hasMatch()) {
        m_supported = true;
        m_hot = true;
        m_tripTemp = m.captured(1).toDouble();
        setThresholds(m.captured(2).toDouble(), qQNaN(), qQNaN(), qQNaN());
        setState(0, 2, otempTextFor(m_tripTemp), true);
        return true;
    }
    m = reOc.match(s);
    if (m.hasMatch()) {
        m_hot = false;
        setThresholds(qQNaN(), m.captured(1).toDouble(), qQNaN(), qQNaN());
        setState(0, 1, tr("Hết quá nhiệt (< %1 °C) — bấm Enable để chạy lại").arg(m.captured(1)), true);
        return true;
    }
    m = reVl.match(s);
    if (m.hasMatch()) {
        m_supported = true;
        m_low = true;
        m_tripVbat = m.captured(1).toDouble();
        setThresholds(qQNaN(), qQNaN(), m.captured(2).toDouble(), qQNaN());
        setState(1, 2, vbatTextFor(m_tripVbat), true);
        return true;
    }
    m = reVo.match(s);
    if (m.hasMatch()) {
        m_low = false;
        setState(1, 1, tr("Pin đã ổn (%1 V) — bấm Enable để chạy lại").arg(m.captured(1)), true);
        return true;
    }
    return false;
}

QString BoardMonitor::otempTextFor(double t) const
{
    return qIsNaN(t) ? tr("Quá nhiệt — đã tắt cả 6 motor")
                     : tr("Quá nhiệt %1 °C — đã tắt cả 6 motor").arg(QString::number(t, 'f', 1));
}

QString BoardMonitor::vbatTextFor(double v) const
{
    return qIsNaN(v) ? tr("Pin yếu — đã tắt cả 6 motor")
                     : tr("Pin yếu %1 V — đã tắt cả 6 motor").arg(QString::number(v, 'f', 2));
}

void BoardMonitor::onEnableSent()
{
    m_ackClock.start();
    // "safe again" acknowledged; a refusal ("ERR board ...") raises the alarm again
    if (m_otemp == 1)
        setState(0, 0, QString(), false);
    if (m_vbatSt == 1)
        setState(1, 0, QString(), false);
}

bool BoardMonitor::refresh()
{
    int port = m_ctrl->portOfMcu(QStringLiteral("U7"));
    if (port < 0)
        port = m_ctrl->mainPort();
    return m_ctrl->sendTo(port, QStringLiteral("sense"));
}

void BoardMonitor::reset()
{
    m_hasTemp = m_hasRh = m_hasVbat = false;
    m_source.clear();
    m_age.invalidate();
    m_supported = false;
    m_seen = false;
    m_tOff = m_tOn = m_vOff = m_vOn = qQNaN();
    m_i2cErr = 0;
    m_senseError.clear();
    m_hot = m_low = false;
    m_flt = 0;
    m_otemp = m_vbatSt = 0;
    m_tripTemp = m_tripVbat = qQNaN();
    m_lastEvent.clear();
    m_ackClock.invalidate();
    m_tempHist.clear();
    m_vbatHist.clear();
    emit thresholdsChanged();
    emit alarmChanged();
    emit historyChanged();
    emit updated();
    emit tick();
}
