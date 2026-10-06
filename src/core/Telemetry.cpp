#include "Telemetry.h"

#include "HandModel.h"

#include <QRegularExpression>

Telemetry::Telemetry(HandModel *hand, QObject *parent)
    : QObject(parent), m_hand(hand)
{
    m_rateClock.start();
    // the "current limit" shown in the status panel is the main joint's own limit
    connect(m_hand, &HandModel::benchJointChanged, this, &Telemetry::limitChanged);
    for (int i = 0; i < m_hand->count(); ++i)
        connect(m_hand->joint(i), &Joint::configChanged, this, &Telemetry::limitChanged);
}

double Telemetry::currentLimitMa() const
{
    const Joint *j = m_hand->joint(m_hand->benchJoint());
    return j ? j->currentLimitMa() : 300.0;
}

QStringList Telemetry::responseNames() const
{
    return {tr("Cảnh báo"), tr("Tắt driver"), tr("Lùi (RETR)"), tr("Giữ tại chỗ")};
}

void Telemetry::setOverResponse(int r)
{
    r = qBound(0, r, 3);
    if (r == m_response)
        return;
    m_response = r;
    emit limitChanged();
}

void Telemetry::setOverSource(int s)
{
    s = qBound(0, s, 1);
    if (s == m_source)
        return;
    m_source = s;
    emit limitChanged();
}

void Telemetry::setRetractDeg(double d)
{
    d = qBound(0.5, d, 90.0);
    if (qFuzzyCompare(d, m_retractDeg))
        return;
    m_retractDeg = d;
    emit limitChanged();
}

void Telemetry::setTripMs(double ms)
{
    ms = qBound(1.0, ms, 5000.0);
    if (qFuzzyCompare(ms, m_tripMs))
        return;
    m_tripMs = ms;
    emit limitChanged();
}

QString Telemetry::recentEvent() const
{
    return (m_eventClock.isValid() && m_eventClock.elapsed() < 3000) ? m_event : QString();
}

void Telemetry::noteEvent(const QString &code, const QString &text)
{
    m_event = code;
    m_eventText = text;
    m_eventClock.start();
    emit eventChanged();
    emit updated();
}

double Telemetry::posDeg() const
{
    const Joint *j = m_hand->joint(m_hand->benchJoint());
    return j ? j->countsToDeg(m_pos) : 0;
}

double Telemetry::tgtDeg() const
{
    const Joint *j = m_hand->joint(m_hand->benchJoint());
    return j ? j->countsToDeg(m_tgt) : 0;
}

double Telemetry::velDeg() const
{
    const Joint *j = m_hand->joint(m_hand->benchJoint());
    return j ? j->countsToDeg(m_velPos) : 0;
}

double Telemetry::outputRpm() const
{
    const Joint *j = m_hand->joint(m_hand->benchJoint());
    return (j && j->countsPerRev() > 0) ? m_velPos / j->countsPerRev() * 60.0 : 0.0;
}

void Telemetry::setCurrentLimitMa(double l)
{
    if (Joint *j = m_hand->joint(m_hand->benchJoint()))
        j->setCurrentLimitMa(l);
}

void Telemetry::setAutoDisable(bool a)
{
    setOverResponse(a ? Disable : Warn);
}

void Telemetry::resetPeak()
{
    m_peak = qAbs(m_iMa);
    m_hand->resetPeaks();
    emit updated();
}

void Telemetry::reset()
{
    m_hasData = false;
    m_multi = false;
    m_lastMultiT = -1;
    m_posHist.clear();
    m_velPos = 0;
    m_hasDiag = false;
    m_noMotion = false;
    m_fields.clear();
    m_homed = -1;
    if (!m_fwId.isEmpty() || !m_fwKeys.isEmpty()) {
        m_fwId.clear();
        m_fwMcu.clear();
        m_fwKeys.clear();
        m_fwG474 = false;
        emit firmwareIdChanged();
    }
    m_peak = 0;
    m_overLimit = false;
    m_overMask = 0;
    m_totalMa = 0;
    m_hand->clearFeedback();
    emit homedChanged();
    emit updated();
}

QString Telemetry::faultText(int flt)
{
    QStringList l;
    if (flt & 1) l << QStringLiteral("DIAG");
    if (flt & 2) l << QStringLiteral("Quá dòng");
    if (flt & 4) l << QStringLiteral("KEY");
    return l.join(QStringLiteral(" · "));
}

void Telemetry::processTelemetry(double t, const QStringList &names, const QVector<double> &values)
{
    QStringList outNames = names;
    QVector<double> outValues = values;

    bool sawQ = false;
    QVector<double> q(6, qQNaN()), tg(6, qQNaN()), cur(6, qQNaN());
    for (int k = 0; k < names.size() && k < values.size(); ++k) {
        const QString &n = names[k];
        const double v = values[k];
        m_fields.insert(n, v);
        if (n == QLatin1String("en")) m_en = int(v);
        else if (n == QLatin1String("duty")) m_duty = v;
        else if (n == QLatin1String("out")) m_out = v;
        else if (n == QLatin1String("i_mA")) m_iMa = v;
        else if (n == QLatin1String("flt")) m_flt = int(v);
        else if (n == QLatin1String("diag")) { m_diag = int(v); m_hasDiag = true; }
        else if (n == QLatin1String("pos")) m_pos = v;
        else if (n == QLatin1String("tgt")) m_tgt = v;
        else if (n == QLatin1String("vel")) m_vel = v;
        else if (n == QLatin1String("mode")) m_mode = int(v);
        else if (n.size() == 2 && n[1].isDigit()) {
            const int idx = n[1].digitValue() - 1;
            if (idx >= 0 && idx < 6) {
                if (n[0] == QLatin1Char('q')) { q[idx] = v; sawQ = true; }
                else if (n[0] == QLatin1Char('t')) tg[idx] = v;
                else if (n[0] == QLatin1Char('i')) cur[idx] = v;
            }
        }
    }

    if (sawQ) {
        m_multi = true;
        double maxI = 0;
        for (int i = 0; i < 6; ++i) {
            Joint *j = m_hand->joint(i);
            if (!j || qIsNaN(q[i]))
                continue;
            const double md = j->countsToDeg(q[i]);
            const double td = qIsNaN(tg[i]) ? md : j->countsToDeg(tg[i]);
            j->setFeedback(md, td, q[i]);
            outNames << QStringLiteral("%1_deg").arg(j->key());
            outValues << md;
            if (!qIsNaN(cur[i])) {
                j->setCurrent(cur[i]);
                maxI = qMax(maxI, qAbs(cur[i]));
            }
        }
        m_iMa = maxI;
        double sum = 0;
        for (int i = 0; i < 6; ++i)
            if (!qIsNaN(cur[i]))
                sum += qAbs(cur[i]);
        m_totalMa = sum;
        // Status panel follows the selected ("main") joint
        const int b = m_hand->benchJoint();
        if (!qIsNaN(q[b])) {
            const double prev = m_pos;
            m_pos = q[b];
            m_tgt = qIsNaN(tg[b]) ? q[b] : tg[b];
            if (m_lastMultiT >= 0 && t > m_lastMultiT)
                m_vel = (m_pos - prev) / (t - m_lastMultiT);
            m_lastMultiT = t;
            if (!qIsNaN(cur[b]))
                m_iMa = cur[b];
        }
    } else if (names.contains(QLatin1String("pos"))) {
        m_multi = false;
        Joint *j = m_hand->joint(m_hand->benchJoint());
        if (j) {
            const double md = j->countsToDeg(m_pos);
            const double td = (m_mode == 1) ? j->countsToDeg(m_tgt) : md;
            j->setFeedback(md, td, m_pos);
            j->setCurrent(m_iMa);
            m_totalMa = qAbs(m_iMa);
            outNames << QStringLiteral("pos_deg") << QStringLiteral("tgt_deg");
            outValues << md << ((m_mode == 1) ? td : qQNaN());
        }
    }

    // position-based velocity and "duty but no motion" hint
    if (names.contains(QLatin1String("pos")) || sawQ) {
        m_posHist.append({t, m_pos});
        while (m_posHist.size() > 2 && t - m_posHist.first().t > 0.6)
            m_posHist.removeFirst();
        const PosSample &a = m_posHist.first();
        m_velPos = (t - a.t) > 1e-3 ? (m_pos - a.pos) / (t - a.t) : 0.0;
        const bool driving = m_en && qAbs(m_out) >= 15.0 && m_mode != 1;
        const bool windowFull = (t - a.t) >= 0.4;
        m_noMotion = driving && windowFull && qAbs(m_pos - a.pos) <= 2.0;
    }

    if (qAbs(m_iMa) > m_peak)
        m_peak = qAbs(m_iMa);
    // each joint against its own limit; rising edge -> overCurrent(joint)
    quint32 mask = 0;
    for (int i = 0; i < m_hand->count(); ++i) {
        const Joint *j = m_hand->joint(i);
        if (j->overLimit())
            mask |= (1u << i);
    }
    for (int i = 0; i < m_hand->count(); ++i) {
        if ((mask & (1u << i)) && !(m_overMask & (1u << i)))
            emit overCurrent(i, m_hand->joint(i)->currentMa());
    }
    m_overMask = mask;
    m_overLimit = mask != 0;

    m_hasData = true;
    ++m_count;
    const qint64 ms = m_rateClock.elapsed();
    if (ms >= 1000) {
        m_rate = m_count * 1000.0 / ms;
        m_count = 0;
        m_rateClock.restart();
        emit rateChanged();
    }
    emit updated();
    emit sample(t, outNames, outValues);
}

void Telemetry::processText(const QString &line)
{
    const QString s = line.trimmed();
    // "HOME: done ...", also "HOME I: done" / "HOME(I): timeout" from multi-motor firmware
    static const QRegularExpression reHome(QStringLiteral("^HOME\\b[^:]{0,12}:\\s*(\\w+)"),
                                           QRegularExpression::CaseInsensitiveOption);
    const auto mh = reHome.match(s);
    if (mh.hasMatch()) {
        const QString rest = mh.captured(1).toLower();
        const int prev = m_homed;
        int result = -1;
        if (rest.startsWith(QLatin1String("done")) || rest == QLatin1String("ok"))
            result = 1;
        else if (rest.startsWith(QLatin1String("timeout")) || rest.startsWith(QLatin1String("abort"))
                 || rest.startsWith(QLatin1String("fail")) || rest.startsWith(QLatin1String("err")))
            result = 0;
        if (result >= 0) {
            m_homed = result;
            if (prev != m_homed)
                emit homedChanged();
            emit homeResult(result);
        }
    } else if (s.startsWith(QLatin1String("OVERI:"), Qt::CaseInsensitive)) {
        // firmware reaction: "OVERI: retract i=412 mA ..." / hold / trip / warn
        const QString what = s.mid(6).trimmed().section(QLatin1Char(' '), 0, 0).toLower();
        QString code = QStringLiteral("TRIP");
        if (what.startsWith(QLatin1String("retr"))) code = QStringLiteral("RETR");
        else if (what.startsWith(QLatin1String("hold"))) code = QStringLiteral("HOLD");
        else if (what.startsWith(QLatin1String("warn"))) code = QStringLiteral("WARN");
        noteEvent(code, s);
    } else if (s.startsWith(QLatin1String("ID:"), Qt::CaseInsensitive)) {
        m_fwId = s.mid(3).trimmed();
        static const QRegularExpression reMcu(QStringLiteral("\\bmcu\\s*=\\s*(\\S+)"));
        static const QRegularExpression reKeys(QStringLiteral("\\bkeys\\s*=\\s*([A-Za-z0-9_,]+)"));
        const auto mm = reMcu.match(s);
        const auto mk = reKeys.match(s);
        m_fwMcu = mm.hasMatch() ? mm.captured(1) : QString();
        m_fwKeys.clear();
        if (mk.hasMatch()) {
            for (const QString &k : mk.captured(1).split(QLatin1Char(','), Qt::SkipEmptyParts)) {
                // keep the app's spelling of the key (T2, I, ...)
                const int idx = m_hand->indexOfKey(k.trimmed());
                m_fwKeys << (idx >= 0 ? m_hand->joint(idx)->key() : k.trimmed());
            }
        }
        m_fwG474 = s.contains(QLatin1String("CyberHand-G474"), Qt::CaseInsensitive) || !m_fwKeys.isEmpty();
        emit firmwareIdChanged();
        static const QRegularExpression reJ(QStringLiteral("joints\\s*=\\s*(\\d+)"));
        static const QRegularExpression reC(QStringLiteral("cnt90\\s*=\\s*([0-9.]+)"));
        const auto mj = reJ.match(s);
        const auto mc = reC.match(s);
        emit configFromFirmware(mj.hasMatch() ? mj.captured(1).toInt() : 0,
                                mc.hasMatch() ? mc.captured(1).toDouble() : 0);
    }
}
