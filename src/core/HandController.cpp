#include "HandController.h"

#include "ConsoleModel.h"
#include "HandModel.h"
#include "LineParser.h"
#include "SerialLink.h"

#include <QHash>

namespace {
// MCB board layout: MCU and motor of each joint (fw 0.7-g474)
QString mcuOfKey(const QString &key)
{
    static const QHash<QString, QString> m = {
        {QStringLiteral("I"), QStringLiteral("U6")}, {QStringLiteral("T1"), QStringLiteral("U6")},
        {QStringLiteral("T2"), QStringLiteral("U6")}, {QStringLiteral("M"), QStringLiteral("U7")},
        {QStringLiteral("R"), QStringLiteral("U7")}, {QStringLiteral("P"), QStringLiteral("U7")}};
    return m.value(key);
}
} // namespace

HandController::HandController(SerialLink *link, ConsoleModel *console, HandModel *hand, QObject *parent)
    : QObject(parent), m_link(link), m_console(console), m_hand(hand)
{
    resetStreamState();
    connect(m_link, &SerialLink::connectedChanged, this, [this]() {
        resetStreamState();
        if (!m_link->connected())
            clearPortInfo();
    });
    // the "main joint" decides which motor the single-motor commands reach
    connect(m_hand, &HandModel::benchJointChanged, this, &HandController::portChanged);
}

void HandController::setPortInfo(const QString &mcu, const QStringList &keys)
{
    m_mcu = mcu;
    m_keys = keys;
    m_selected.clear();
    updateJointFlags();
    emit portChanged();
}

void HandController::clearPortInfo()
{
    if (m_keys.isEmpty() && m_mcu.isEmpty())
        return;
    m_mcu.clear();
    m_keys.clear();
    m_selected.clear();
    updateJointFlags();
    emit portChanged();
}

void HandController::updateJointFlags()
{
    const bool known = m_link->connected() && !m_keys.isEmpty();
    for (int i = 0; i < m_hand->count(); ++i) {
        Joint *j = m_hand->joint(i);
        j->setOffPort(known && !m_keys.contains(j->key()));
    }
}

bool HandController::jointOnPort(int joint) const
{
    const Joint *j = m_hand->joint(joint);
    if (!j)
        return false;
    return m_keys.isEmpty() || m_keys.contains(j->key());
}

bool HandController::benchOnPort() const
{
    return jointOnPort(m_hand->benchJoint());
}

QString HandController::benchHint() const
{
    return benchOnPort() ? QString() : whereIs(m_hand->benchJoint());
}

QString HandController::whereIs(int joint) const
{
    const Joint *j = m_hand->joint(joint);
    if (!j)
        return QString();
    const QString other = mcuOfKey(j->key());
    if (!other.isEmpty() && other != m_mcu)
        return tr("Khớp %1 (%2) nằm ở %3 — cắm cổng %3").arg(j->key(), j->name(), other);
    return tr("Khớp %1 (%2) không có trên cổng đang mở (%3: %4)")
        .arg(j->key(), j->name(), m_mcu.isEmpty() ? tr("MCU") : m_mcu, m_keys.join(QStringLiteral(", ")));
}

bool HandController::selectBenchMotor(bool warn)
{
    if (m_keys.isEmpty() || !m_link->connected())
        return false;
    const Joint *b = m_hand->joint(m_hand->benchJoint());
    if (!b)
        return false;
    if (!m_keys.contains(b->key())) {
        if (warn)
            m_console->error(whereIs(m_hand->benchJoint()));
        if (!m_selected.isEmpty()) {
            m_selected.clear();
            emit portChanged();
        }
        return false;
    }
    if (!send(QStringLiteral("m ") + b->key()))
        return false;
    m_selected = b->key();
    emit portChanged();
    return true;
}

bool HandController::sendSingle(const QString &line)
{
    if (!benchOnPort()) {
        m_console->error(tr("%1 — không gửi: %2").arg(benchHint(), line));
        return false;
    }
    return send(line);
}

bool HandController::enableJoint(int joint)
{
    const Joint *j = m_hand->joint(joint);
    if (!j)
        return false;
    if (m_keys.isEmpty())
        return enable();
    if (!jointOnPort(joint)) {
        m_console->error(whereIs(joint));
        return false;
    }
    return send(QStringLiteral("e ") + j->key());
}

bool HandController::disableJoint(int joint)
{
    const Joint *j = m_hand->joint(joint);
    if (!j)
        return false;
    if (m_keys.isEmpty())
        return disable();
    if (!jointOnPort(joint)) {
        m_console->error(whereIs(joint));
        return false;
    }
    return send(QStringLiteral("d ") + j->key());
}

bool HandController::sendJointCountsPer90(int joint, double cnt)
{
    const Joint *j = m_hand->joint(joint);
    if (!j || cnt < 1 || !m_link->connected())
        return false;
    const QString jcmd = QStringLiteral("j ") + LineParser::num(cnt, 0);
    if (m_keys.isEmpty()) {
        // single-joint bench board: only the main joint has a motor
        if (m_protocol == BenchSingle && joint == m_hand->benchJoint())
            return send(jcmd);
        return false;
    }
    if (!jointOnPort(joint)) {
        m_console->error(whereIs(joint));
        return false;
    }
    bool ok = send(QStringLiteral("m ") + j->key()) && send(jcmd);
    m_selected = j->key();
    if (joint != m_hand->benchJoint())
        selectBenchMotor(false); // back to the main joint (stays on this one if the main joint is elsewhere)
    emit portChanged();
    return ok;
}

QString HandController::protectionCommand(double ma, double ms, int fwResponse, double retractDeg)
{
    return QStringLiteral("I %1 %2 %3 %4")
        .arg(LineParser::num(ma, 0), LineParser::num(ms, 0))
        .arg(qBound(0, fwResponse, 3))
        .arg(LineParser::num(retractDeg, 1));
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
    if (!benchOnPort()) {
        m_console->error(benchHint());
        return false;
    }
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
    return sendSingle(QStringLiteral("o ") + LineParser::num(qBound(-100.0, pct, 100.0), 0));
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
    if (!jointOnPort(joint)) {
        m_console->info(tr("%1 — chỉ cập nhật mô phỏng").arg(whereIs(joint)));
        return false;
    }
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
    return sendSingle(QStringLiteral("p %1").arg(counts));
}

bool HandController::stepCounts(int counts)
{
    return sendSingle(QStringLiteral("t %1").arg(counts));
}

bool HandController::runVelocity(double degPerSec)
{
    m_velocity = degPerSec;
    emit settingsChanged();
    return sendSingle(QStringLiteral("v ") + LineParser::num(degPerSec, 1));
}

bool HandController::applyDutyLimit()
{
    return sendSingle(QStringLiteral("x ") + LineParser::num(m_dutyLimit, 0));
}

bool HandController::applyPid()
{
    return sendSingle(QStringLiteral("k %1 %2 %3")
                    .arg(LineParser::num(m_kp, 7), LineParser::num(m_ki, 7), LineParser::num(m_kd, 7)));
}

bool HandController::applyLogRate()
{
    return send(QStringLiteral("l ") + LineParser::num(m_logRate, 0));
}

bool HandController::applyPowerRate()
{
    return sendSingle(QStringLiteral("a ") + LineParser::num(m_powerRate, 0));
}

bool HandController::sendCountsPer90(double cnt)
{
    if (cnt < 1)
        return false;
    return sendSingle(QStringLiteral("j ") + LineParser::num(cnt, 0));
}

bool HandController::sendProtection(double ma, double ms, int fwResponse, double retractDeg)
{
    return sendSingle(protectionCommand(ma, ms, fwResponse, retractDeg));
}

int HandController::streamCommands(bool force)
{
    return sendJoints(m_hand->commandVector(), force, !force);
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
        if (qIsNaN(clamped[i]) || !m_hand->joint(i)->enabled() || !jointOnPort(i))
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
