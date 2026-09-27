#include "Telemetry.h"

#include "HandModel.h"

#include <QRegularExpression>

Telemetry::Telemetry(HandModel *hand, QObject *parent)
    : QObject(parent), m_hand(hand)
{
    m_rateClock.start();
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

void Telemetry::setCurrentLimitMa(double l)
{
    l = qMax(1.0, l);
    if (l == m_limit)
        return;
    m_limit = l;
    emit limitChanged();
}

void Telemetry::setAutoDisable(bool a)
{
    if (a == m_autoDisable)
        return;
    m_autoDisable = a;
    emit limitChanged();
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
    m_peak = 0;
    m_overLimit = false;
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
            j->setFeedback(md, td);
            outNames << QStringLiteral("%1_deg").arg(j->key());
            outValues << md;
            if (!qIsNaN(cur[i])) {
                j->setCurrent(cur[i]);
                maxI = qMax(maxI, qAbs(cur[i]));
            }
        }
        m_iMa = maxI;
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
            j->setFeedback(md, td);
            j->setCurrent(m_iMa);
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
    const bool over = qAbs(m_iMa) > m_limit;
    if (over && !m_overLimit)
        emit overCurrent(m_iMa);
    m_overLimit = over;

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
    if (s.startsWith(QLatin1String("HOME:"), Qt::CaseInsensitive)) {
        const QString rest = s.mid(5).trimmed().toLower();
        const int prev = m_homed;
        if (rest.startsWith(QLatin1String("done")))
            m_homed = 1;
        else if (rest.startsWith(QLatin1String("timeout")) || rest.startsWith(QLatin1String("abort")))
            m_homed = 0;
        if (prev != m_homed)
            emit homedChanged();
    } else if (s.startsWith(QLatin1String("ID:"), Qt::CaseInsensitive)) {
        m_fwId = s.mid(3).trimmed();
        emit firmwareIdChanged();
        static const QRegularExpression reJ(QStringLiteral("joints\\s*=\\s*(\\d+)"));
        static const QRegularExpression reC(QStringLiteral("cnt90\\s*=\\s*([0-9.]+)"));
        const auto mj = reJ.match(s);
        const auto mc = reC.match(s);
        emit configFromFirmware(mj.hasMatch() ? mj.captured(1).toInt() : 0,
                                mc.hasMatch() ? mc.captured(1).toDouble() : 0);
    }
}
