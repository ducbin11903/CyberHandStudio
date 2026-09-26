#include "HandController.h"

#include "ConsoleModel.h"
#include "HandModel.h"
#include "LineParser.h"
#include "SerialLink.h"

HandController::HandController(SerialLink *link, ConsoleModel *console, HandModel *hand, QObject *parent)
    : QObject(parent), m_link(link), m_console(console), m_hand(hand)
{
    resetStreamState();
    connect(m_link, &SerialLink::connectedChanged, this, &HandController::resetStreamState);
}

void HandController::setProtocol(int p)
{
    p = qBound(0, p, 2);
    if (p == m_protocol)
        return;
    m_protocol = p;
    resetStreamState();
    emit protocolChanged();
}

QStringList HandController::protocolNames() const
{
    return {tr("Bench 1 khớp: g <độ>"), tr("Từng khớp: g <khớp> <độ>"), tr("1 dòng: G q1..q6")};
}

void HandController::resetStreamState()
{
    m_lastSent = QVector<double>(m_hand->count(), qQNaN());
}

bool HandController::write(const QString &line, bool quiet)
{
    if (!m_link->connected()) {
        if (!quiet)
            m_console->error(tr("Chưa kết nối — không gửi được: %1").arg(line));
        return false;
    }
    const bool ok = m_link->sendLine(line, quiet);
    if (ok && !quiet) {
        m_last = line;
        emit lastCommandChanged();
    }
    return ok;
}

bool HandController::send(const QString &line)
{
    const QString t = line.trimmed();
    if (t.isEmpty())
        return false;
    return write(t, false);
}

bool HandController::home()
{
    QString cmd = QStringLiteral("H");
    if (m_homePct > 0) {
        cmd += QLatin1Char(' ') + LineParser::num(m_homePct, 0);
        if (m_homeMa > 0)
            cmd += QLatin1Char(' ') + LineParser::num(m_homeMa, 0);
    }
    return send(cmd);
}

bool HandController::setDuty(double pct)
{
    return send(QStringLiteral("o ") + LineParser::num(qBound(-100.0, pct, 100.0), 0));
}

QString HandController::jointCommand(int joint, double deg) const
{
    switch (m_protocol) {
    case PerJoint:
        return QStringLiteral("g %1 %2").arg(joint + 1).arg(LineParser::num(deg, 1));
    default:
        return QStringLiteral("g ") + LineParser::num(deg, 1);
    }
}

bool HandController::moveJoint(int joint, double deg)
{
    Joint *j = m_hand->joint(joint);
    if (!j)
        return false;
    deg = j->clamp(deg);
    j->setCommandDeg(deg);
    if (m_protocol == BenchSingle && joint != m_hand->benchJoint()) {
        m_console->info(tr("Bench chỉ có khớp %1 — %2 chỉ cập nhật mô phỏng").arg(m_hand->joint(m_hand->benchJoint())->name(), j->name()));
        return false;
    }
    if (m_protocol == OneLine) {
        QVector<double> v = m_hand->commandVector();
        return sendJoints(v, true, false) > 0;
    }
    const bool ok = send(jointCommand(joint, deg));
    if (ok && joint < m_lastSent.size())
        m_lastSent[joint] = deg;
    return ok;
}

bool HandController::moveCounts(int counts)
{
    return send(QStringLiteral("p %1").arg(counts));
}

bool HandController::stepCounts(int counts)
{
    return send(QStringLiteral("t %1").arg(counts));
}

bool HandController::runVelocity(double degPerSec)
{
    m_velocity = degPerSec;
    emit settingsChanged();
    return send(QStringLiteral("v ") + LineParser::num(degPerSec, 1));
}

bool HandController::applyDutyLimit()
{
    return send(QStringLiteral("x ") + LineParser::num(m_dutyLimit, 0));
}

bool HandController::applyPid()
{
    return send(QStringLiteral("k %1 %2 %3")
                    .arg(LineParser::num(m_kp, 7), LineParser::num(m_ki, 7), LineParser::num(m_kd, 7)));
}

bool HandController::applyLogRate()
{
    return send(QStringLiteral("l ") + LineParser::num(m_logRate, 0));
}

bool HandController::applyPose(const QVariantList &deg)
{
    QVector<double> v;
    for (const QVariant &d : deg)
        v << d.toDouble();
    return sendJoints(v, true, false) >= 0;
}

int HandController::sendJoints(const QVector<double> &deg, bool force, bool quiet)
{
    QVector<double> clamped(m_hand->count(), qQNaN());
    for (int i = 0; i < m_hand->count() && i < deg.size(); ++i) {
        if (qIsNaN(deg[i]))
            continue;
        const Joint *j = m_hand->joint(i);
        clamped[i] = j->clamp(deg[i]);
    }
    m_hand->setCommands(clamped);
    if (!m_link->connected())
        return 0;

    auto changed = [&](int i) {
        if (qIsNaN(clamped[i]) || !m_hand->joint(i)->enabled())
            return false;
        return force || qIsNaN(m_lastSent[i]) || qAbs(clamped[i] - m_lastSent[i]) >= m_deadband;
    };

    int sent = 0;
    switch (m_protocol) {
    case BenchSingle: {
        const int b = m_hand->benchJoint();
        if (changed(b) && write(jointCommand(b, clamped[b]), quiet)) {
            m_lastSent[b] = clamped[b];
            ++sent;
        }
        break;
    }
    case PerJoint:
        for (int i = 0; i < clamped.size(); ++i) {
            if (changed(i) && write(jointCommand(i, clamped[i]), quiet)) {
                m_lastSent[i] = clamped[i];
                ++sent;
            }
        }
        break;
    case OneLine: {
        bool any = false;
        for (int i = 0; i < clamped.size(); ++i)
            any = any || changed(i);
        if (!any)
            break;
        QStringList parts;
        for (int i = 0; i < clamped.size(); ++i) {
            const double v = qIsNaN(clamped[i]) ? m_hand->joint(i)->commandDeg() : clamped[i];
            parts << LineParser::num(v, 1);
        }
        if (write(QStringLiteral("G ") + parts.join(QLatin1Char(' ')), quiet)) {
            for (int i = 0; i < clamped.size(); ++i)
                if (!qIsNaN(clamped[i]))
                    m_lastSent[i] = clamped[i];
            ++sent;
        }
        break;
    }
    }
    return sent;
}
