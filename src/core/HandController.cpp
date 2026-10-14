#include "HandController.h"

#include "ConsoleModel.h"
#include "HandModel.h"
#include "LineParser.h"
#include "CanBus.h"
#include "Transport.h"

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

HandController::HandController(Transport *link, ConsoleModel *console, HandModel *hand, QObject *parent)
    : QObject(parent), m_console(console), m_hand(hand)
{
    resetStreamState();
    addLink(link);
    // the "main joint" decides which motor (and port) the single-motor commands reach
    connect(m_hand, &HandModel::benchJointChanged, this, &HandController::portChanged);
}

void HandController::addLink(Transport *link)
{
    if (!link || m_ports.size() >= 2)
        return;
    const int idx = m_ports.size();
    Port p;
    p.link = link;
    m_ports.append(p);
    m_serial[idx] = link;
    connect(link, &Transport::connectedChanged, this, [this, idx]() {
        if (!useCan())
            onLinkConnection(idx);
    });
    connect(link, &Transport::portNameChanged, this, &HandController::portChanged);
}

void HandController::setCan(CanBus *bus)
{
    m_can = bus;
    for (int i = 0; i < 2; ++i) {
        Transport *node = bus->node(i + 1);
        connect(node, &Transport::connectedChanged, this, [this, i]() {
            if (useCan())
                onLinkConnection(i);
        });
        connect(node, &Transport::portNameChanged, this, &HandController::portChanged);
    }
}

void HandController::setConnectionMode(int m)
{
    m = qBound(0, m, m_can ? 2 : 1);
    if (m == m_mode || connected())
        return;
    m_mode = m;
    for (int i = 0; i < m_ports.size() && i < 2; ++i) {
        clearPortInfo(i);
        m_ports[i].link = (m == 2 && m_can) ? static_cast<Transport *>(m_can->node(i + 1)) : m_serial[i];
    }
    resetStreamState();
    updateJointFlags();
    emit dualPortChanged();
    emit portChanged();
}

Transport *HandController::linkAt(int port) const
{
    return (port >= 0 && port < m_ports.size()) ? m_ports[port].link : nullptr;
}

void HandController::onLinkConnection(int port)
{
    resetStreamState();
    if (!portOpen(port))
        clearPortInfo(port);
    updateJointFlags();
    emit connectedChanged();
    emit portChanged();
}

bool HandController::portOpen(int port) const
{
    const Transport *l = linkAt(port);
    return l && l->connected();
}

bool HandController::connected() const
{
    return openPorts() > 0;
}

int HandController::openPorts() const
{
    return openPortList().size();
}

QVector<int> HandController::openPortList() const
{
    QVector<int> v;
    for (int i = 0; i < m_ports.size(); ++i)
        if (portOpen(i))
            v << i;
    return v;
}

void HandController::setDualPort(bool d)
{
    setConnectionMode(d ? 1 : 0);
}

void HandController::setConsoleTarget(int t)
{
    t = qBound(0, t, 3);
    if (t == m_consoleTarget)
        return;
    m_consoleTarget = t;
    emit consoleTargetChanged();
}

QStringList HandController::consoleTargets() const
{
    return {tr("Tự động"), portLabel(0), portLabel(1), tr("Cả hai")};
}

QString HandController::portLabel(int port) const
{
    if (port < 0 || port >= m_ports.size())
        return QString();
    const Port &p = m_ports[port];
    if (!p.mcu.isEmpty())
        return p.mcu;
    if (p.link && !p.link->portName().isEmpty())
        return p.link->portName();
    return tr("Cổng %1").arg(port + 1);
}

int HandController::portOfMcu(const QString &mcu) const
{
    for (int i : openPortList())
        if (!m_ports[i].mcu.isEmpty() && m_ports[i].mcu.compare(mcu, Qt::CaseInsensitive) == 0)
            return i;
    return -1;
}

QStringList HandController::effectiveKeys(int port) const
{
    if (port < 0 || port >= m_ports.size())
        return {};
    const Port &p = m_ports[port];
    if (p.peer.isEmpty() || p.peerUp != 0)
        return p.keys;
    // link to the other MCU lost: its joints are not reachable through this port
    QStringList k;
    for (const QString &key : p.keys)
        if (mcuOfKey(key) != p.peer)
            k << key;
    return k;
}

bool HandController::layoutKnown() const
{
    for (int i : openPortList())
        if (!m_ports[i].keys.isEmpty())
            return true;
    return false;
}

int HandController::portOfJoint(int joint) const
{
    const Joint *j = m_hand->joint(joint);
    const QVector<int> open = openPortList();
    if (!j || open.isEmpty())
        return -1;
    bool layout = false;
    for (int i : open) {
        if (m_ports[i].keys.isEmpty())
            continue;
        layout = true;
        if (effectiveKeys(i).contains(j->key()))
            return i;
    }
    // bench board without MCU layout: everything goes to the (first) open port
    return layout ? -1 : open.first();
}

int HandController::mainPort() const
{
    const int p = portOfJoint(m_hand->benchJoint());
    if (p >= 0)
        return p;
    const QVector<int> open = openPortList();
    return open.isEmpty() ? 0 : open.first();
}

int HandController::portFor(int joint) const
{
    const int p = portOfJoint(joint);
    return p >= 0 ? p : mainPort();
}

QString HandController::portMcu() const
{
    QStringList l;
    for (int i : openPortList())
        if (!m_ports[i].mcu.isEmpty())
            l << m_ports[i].mcu;
    return l.join(QLatin1Char('+'));
}

QStringList HandController::portKeys() const
{
    QStringList keys;
    for (int i : openPortList())
        for (const QString &k : effectiveKeys(i))
            if (!keys.contains(k))
                keys << k;
    // app joint order (T2 T1 I M R P)
    QStringList ordered;
    for (int i = 0; i < m_hand->count(); ++i)
        if (keys.contains(m_hand->joint(i)->key()))
            ordered << m_hand->joint(i)->key();
    return ordered;
}

QString HandController::selectedKey() const
{
    const int p = mainPort();
    return (p < m_ports.size() && portOpen(p)) ? m_ports[p].selected : QString();
}

QString HandController::idText() const
{
    QStringList l;
    const QVector<int> open = openPortList();
    for (int i : open) {
        if (m_ports[i].id.isEmpty())
            continue;
        l << (open.size() > 1 ? portLabel(i) + QStringLiteral(": ") + m_ports[i].id : m_ports[i].id);
    }
    return l.join(QLatin1Char('\n'));
}

QVariantList HandController::ports() const
{
    QVariantList list;
    const int main = mainPort();
    for (int i = 0; i < m_ports.size(); ++i) {
        const Port &p = m_ports[i];
        list << QVariantMap{
            {QStringLiteral("index"), i},
            {QStringLiteral("open"), portOpen(i)},
            {QStringLiteral("name"), p.link ? p.link->portName() : QString()},
            {QStringLiteral("label"), portLabel(i)},
            {QStringLiteral("mcu"), p.mcu},
            {QStringLiteral("keys"), effectiveKeys(i)},
            {QStringLiteral("selected"), p.selected},
            {QStringLiteral("peer"), p.peer},
            {QStringLiteral("peerUp"), p.peerUp},
            {QStringLiteral("id"), p.id},
            {QStringLiteral("main"), portOpen(i) && i == main}};
    }
    return list;
}

void HandController::setPortId(int port, const QString &id)
{
    if (port < 0 || port >= m_ports.size() || m_ports[port].id == id)
        return;
    m_ports[port].id = id;
    emit portChanged();
}

void HandController::setPortInfo(int port, const QString &mcu, const QStringList &keys, const QString &peer)
{
    if (port < 0 || port >= m_ports.size())
        return;
    Port &p = m_ports[port];
    // a bridge ID lists the peer's joints only while the link is up
    p.peerUp = -1;
    if (!peer.isEmpty()) {
        p.peerUp = 0;
        for (const QString &k : keys)
            if (mcuOfKey(k) == peer)
                p.peerUp = 1;
    }
    p.mcu = mcu;
    p.keys = keys;
    p.peer = peer;
    if (!keys.contains(p.selected))
        p.selected.clear();
    updateJointFlags();
    emit portChanged();
}

void HandController::clearPortInfo(int port)
{
    if (port < 0 || port >= m_ports.size())
        return;
    Port &p = m_ports[port];
    const bool had = !p.keys.isEmpty() || !p.mcu.isEmpty() || !p.id.isEmpty();
    p.mcu.clear();
    p.keys.clear();
    p.selected.clear();
    p.peer.clear();
    p.peerUp = -1;
    p.id.clear();
    if (had) {
        updateJointFlags();
        emit portChanged();
    }
}

bool HandController::setPeerUp(int port, bool up, const QString &peer)
{
    if (port < 0 || port >= m_ports.size())
        return false;
    Port &p = m_ports[port];
    if (!peer.isEmpty() && p.peer.isEmpty())
        p.peer = peer;
    const int v = up ? 1 : 0;
    if (p.peerUp == v)
        return false;
    p.peerUp = v;
    updateJointFlags();
    emit portChanged();
    return true;
}

bool HandController::portHasPeer(int port) const
{
    return port >= 0 && port < m_ports.size() && !m_ports[port].peer.isEmpty();
}

void HandController::updateJointFlags()
{
    const bool known = layoutKnown();
    for (int i = 0; i < m_hand->count(); ++i)
        m_hand->joint(i)->setOffPort(known && portOfJoint(i) < 0);
}

bool HandController::jointOnPort(int joint) const
{
    if (!m_hand->joint(joint))
        return false;
    return !layoutKnown() || portOfJoint(joint) >= 0;
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
    const QString owner = mcuOfKey(j->key());
    for (int i : openPortList())
        if (!owner.isEmpty() && m_ports[i].peer == owner && m_ports[i].peerUp == 0)
            return tr("Khớp %1 (%2) nằm ở %3 — link %4-%3 đang mất").arg(j->key(), j->name(), owner, portLabel(i));
    if (!owner.isEmpty() && portOfMcu(owner) < 0) {
        if (useCan())
            return tr("Khớp %1 (%2) nằm ở %3 — %3 chưa trả lời qua CAN").arg(j->key(), j->name(), owner);
        if (dualPort())
            return tr("Khớp %1 (%2) nằm ở %3 — cổng %3 chưa mở").arg(j->key(), j->name(), owner);
        return tr("Khớp %1 (%2) nằm ở %3 — cắm cổng %3 hoặc bật chế độ 2 cổng").arg(j->key(), j->name(), owner);
    }
    return tr("Khớp %1 (%2) không có trên cổng đang mở (%3: %4)")
        .arg(j->key(), j->name(), portMcu().isEmpty() ? tr("MCU") : portMcu(), portKeys().join(QStringLiteral(", ")));
}

bool HandController::selectBenchMotor(bool warn)
{
    if (!layoutKnown())
        return false;
    const Joint *b = m_hand->joint(m_hand->benchJoint());
    if (!b)
        return false;
    const int p = portOfJoint(m_hand->benchJoint());
    if (p < 0) {
        if (warn)
            m_console->error(whereIs(m_hand->benchJoint()));
        emit portChanged();
        return false;
    }
    if (!writeTo(p, QStringLiteral("m ") + b->key(), false))
        return false;
    emit portChanged();
    return true;
}

bool HandController::sendSingle(const QString &line)
{
    if (!benchOnPort()) {
        m_console->error(tr("%1 — không gửi: %2").arg(benchHint(), line));
        return false;
    }
    return writeTo(mainPort(), line, false);
}

bool HandController::enableJoint(int joint)
{
    const Joint *j = m_hand->joint(joint);
    if (!j)
        return false;
    if (!layoutKnown())
        return enable();
    const int p = portOfJoint(joint);
    if (p < 0) {
        m_console->error(whereIs(joint));
        return false;
    }
    return writeTo(p, QStringLiteral("e ") + j->key(), false);
}

bool HandController::disableJoint(int joint)
{
    const Joint *j = m_hand->joint(joint);
    if (!j)
        return false;
    if (!layoutKnown())
        return disable();
    const int p = portOfJoint(joint);
    if (p < 0) {
        m_console->error(whereIs(joint));
        return false;
    }
    return writeTo(p, QStringLiteral("d ") + j->key(), false);
}

bool HandController::sendJointCountsPer90(int joint, double cnt)
{
    const Joint *j = m_hand->joint(joint);
    if (!j || cnt < 1 || !connected())
        return false;
    const QString jcmd = QStringLiteral("j ") + LineParser::num(cnt, 0);
    if (!layoutKnown()) {
        // single-joint bench board: only the main joint has a motor
        if (m_protocol == BenchSingle && joint == m_hand->benchJoint())
            return writeTo(mainPort(), jcmd, false);
        return false;
    }
    const int p = portOfJoint(joint);
    if (p < 0) {
        m_console->error(whereIs(joint));
        return false;
    }
    const bool ok = writeTo(p, QStringLiteral("m ") + j->key(), false) && writeTo(p, jcmd, false);
    // back to the main joint when it is driven by the same port
    const Joint *b = m_hand->joint(m_hand->benchJoint());
    if (joint != m_hand->benchJoint() && b && portOfJoint(m_hand->benchJoint()) == p)
        writeTo(p, QStringLiteral("m ") + b->key(), false);
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
    return writeTo(mainPort(), line, quiet);
}

bool HandController::writeTo(int port, const QString &line, bool quiet)
{
    Transport *l = linkAt(port);
    if (!l || !l->connected()) {
        if (!quiet)
            m_console->error(tr("Chưa kết nối — không gửi được: %1").arg(line));
        return false;
    }
    const bool ok = l->sendLine(line, quiet);
    if (!ok)
        return false;
    // track the selected motor of the port (m <key>)
    if (line.startsWith(QLatin1String("m ")) && !m_ports[port].keys.isEmpty()) {
        const int idx = m_hand->indexOfKey(line.mid(2).trimmed());
        if (idx >= 0 && m_ports[port].keys.contains(m_hand->joint(idx)->key()) && m_ports[port].selected != m_hand->joint(idx)->key()) {
            m_ports[port].selected = m_hand->joint(idx)->key();
            emit portChanged();
        }
    }
    if (!quiet) {
        m_last = openPorts() > 1 ? QStringLiteral("[%1] %2").arg(portLabel(port), line) : line;
        emit lastCommandChanged();
    }
    return true;
}

int HandController::portOfToken(const QString &tok, bool jointNumbers) const
{
    int joint = m_hand->indexOfKey(tok);
    if (joint < 0 && jointNumbers) {
        bool ok = false;
        const int n = tok.toInt(&ok);
        if (ok && n >= 1 && n <= m_hand->count())
            joint = n - 1;
    }
    return joint >= 0 ? portOfJoint(joint) : -1;
}

QVector<int> HandController::routeOf(QString &line) const
{
    const QVector<int> open = openPortList();
    if (open.size() <= 1)
        return {mainPort()}; // one port: the firmware sees every command (fw 0.8 routes itself)
    // "@U7 s": straight to that MCU's port
    if (line.startsWith(QLatin1Char('@'))) {
        const QString tag = line.section(QLatin1Char(' '), 0, 0).mid(1);
        const QString rest = line.section(QLatin1Char(' '), 1).trimmed();
        const int p = portOfMcu(tag);
        if (p >= 0 && !rest.isEmpty()) {
            line = rest;
            return {p};
        }
        return {mainPort()};
    }
    const QStringList t = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    const QString c = t.value(0);
    static const QStringList perKey = {QStringLiteral("e"), QStringLiteral("d"), QStringLiteral("c"), QStringLiteral("m")};
    if (perKey.contains(c) && t.size() >= 2) {
        const int p = portOfToken(t[1], false);
        return {p >= 0 ? p : mainPort()};
    }
    if (c == QLatin1String("g") && t.size() >= 3) {
        const int p = portOfToken(t[1], true);
        return {p >= 0 ? p : mainPort()};
    }
    // board sensors live on U7 (fw 0.9; sim* = fw_sim test commands)
    static const QStringList board = {QStringLiteral("sense"), QStringLiteral("simtemp"), QStringLiteral("simvbat"),
                                      QStringLiteral("simsht")};
    if (board.contains(c)) {
        const int p = portOfMcu(QStringLiteral("U7"));
        return {p >= 0 ? p : mainPort()};
    }
    static const QStringList both = {QStringLiteral("e"), QStringLiteral("d"), QStringLiteral("c"), QStringLiteral("D"),
                                     QStringLiteral("i"), QStringLiteral("l"), QStringLiteral("G")};
    if (both.contains(c))
        return open;
    return {mainPort()};
}

bool HandController::send(const QString &line)
{
    QString t = line.trimmed();
    if (t.isEmpty())
        return false;
    const QVector<int> targets = routeOf(t);
    if (useCan() && targets.size() > 1)
        return sendAll(t); // both MCUs: one 0x200 frame
    bool ok = false;
    for (int p : targets)
        ok = writeTo(p, t, false) || ok;
    return ok;
}

bool HandController::sendConsole(const QString &line)
{
    const QString t = line.trimmed();
    if (t.isEmpty())
        return false;
    if (openPorts() < 2 || m_consoleTarget == 0)
        return send(t);
    if (m_consoleTarget == 3)
        return sendAll(t);
    return writeTo(m_consoleTarget - 1, t, false);
}

bool HandController::sendTo(int port, const QString &line)
{
    const QString t = line.trimmed();
    return !t.isEmpty() && writeTo(port, t, false);
}

bool HandController::sendPosAll(const QVector<double> &deg, bool quiet)
{
    if (!m_can || !m_can->connected()) {
        if (!quiet)
            m_console->error(tr("CAN chưa kết nối — không gửi được vị trí khớp"));
        return false;
    }
    m_can->sendPosAll(deg);
    QStringList parts;
    for (int i = 0; i < deg.size() && i < m_hand->count(); ++i)
        if (!qIsNaN(deg[i]))
            parts << QStringLiteral("%1=%2").arg(m_hand->joint(i)->key(), LineParser::num(deg[i], 1));
    const QString text = QStringLiteral("POS_ALL ") + parts.join(QLatin1Char(' '));
    m_console->onSent(text, quiet, QStringLiteral("CAN"));
    if (!quiet) {
        m_last = text;
        emit lastCommandChanged();
    }
    return true;
}

bool HandController::estop()
{
    if (useCan() && m_can && m_can->connected()) {
        m_console->onSent(QStringLiteral("E-STOP (0x000)"), false, QStringLiteral("CAN"));
        return m_can->estop();
    }
    if (!connected())
        return false;
    stopAll();
    return true;
}

void HandController::stopAll()
{
    if (useCan()) {
        sendAll(QStringLiteral("D")); // every motor of both MCUs off (0x200)
        return;
    }
    for (int p : openPortList()) {
        if (!m_ports[p].keys.isEmpty()) {
            writeTo(p, QStringLiteral("D"), false); // MCB G474: all motors off (bridge: both MCUs)
        } else {
            writeTo(p, QStringLiteral("0"), false); // open loop duty 0
            writeTo(p, QStringLiteral("d"), false); // driver off
        }
    }
}

bool HandController::sendAll(const QString &line)
{
    const QString t = line.trimmed();
    if (t.isEmpty())
        return false;
    if (useCan()) {
        // CMD_TEXT 0x200: one frame, both MCUs
        if (!m_can || !m_can->connected()) {
            m_console->error(tr("Chưa kết nối — không gửi được: %1").arg(t));
            return false;
        }
        if (!m_can->sendText(0, t))
            return false;
        for (int n = 1; n <= 2; ++n)
            m_can->node(n)->noteSent(t, false, t.toUtf8().size());
        m_last = t;
        emit lastCommandChanged();
        return true;
    }
    const QVector<int> open = openPortList();
    if (open.isEmpty())
        return writeTo(0, t, false); // "Chưa kết nối" note
    bool ok = false;
    for (int p : open)
        ok = writeTo(p, t, false) || ok;
    return ok;
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
    return writeTo(mainPort(), cmd, false);
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
    if (useCan()) {
        // one joint = POS_ALL with the other five at 0x7FFF
        QVector<double> q(m_hand->count(), qQNaN());
        q[joint] = deg;
        const bool ok = sendPosAll(q, false);
        if (ok && joint < m_lastSent.size())
            m_lastSent[joint] = deg;
        return ok;
    }
    if (m_protocol == BenchSingle && joint != m_hand->benchJoint()) {
        m_console->info(tr("Bench chỉ có khớp %1 — %2 chỉ cập nhật mô phỏng").arg(m_hand->joint(m_hand->benchJoint())->name(), j->name()));
        return false;
    }
    if (m_protocol == OneLine) {
        QVector<double> v = m_hand->commandVector();
        return sendJoints(v, true, false) > 0;
    }
    const bool ok = writeTo(portFor(joint), jointCommand(joint, deg), false);
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
    // CAN: STATUS frame rate
    return sendAll((useCan() ? QStringLiteral("canlog ") : QStringLiteral("l ")) + LineParser::num(m_logRate, 0));
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
    if (!connected())
        return 0;

    auto changed = [&](int i) {
        if (qIsNaN(clamped[i]) || !m_hand->joint(i)->enabled() || !jointOnPort(i))
            return false;
        return force || qIsNaN(m_lastSent[i]) || qAbs(clamped[i] - m_lastSent[i]) >= m_deadband;
    };

    if (useCan()) {
        // every joint that moved goes in one POS_ALL frame (the rest 0x7FFF)
        QVector<double> q(m_hand->count(), qQNaN());
        int n = 0;
        for (int i = 0; i < clamped.size(); ++i) {
            if (!changed(i))
                continue;
            q[i] = clamped[i];
            ++n;
        }
        if (n == 0 || !sendPosAll(q, quiet))
            return 0;
        for (int i = 0; i < clamped.size(); ++i)
            if (!qIsNaN(q[i]))
                m_lastSent[i] = q[i];
        return n;
    }

    int sent = 0;
    switch (m_protocol) {
    case BenchSingle: {
        const int b = m_hand->benchJoint();
        if (changed(b) && writeTo(portFor(b), jointCommand(b, clamped[b]), quiet)) {
            m_lastSent[b] = clamped[b];
            ++sent;
        }
        break;
    }
    case PerJoint:
        for (int i = 0; i < clamped.size(); ++i) {
            if (changed(i) && writeTo(portFor(i), jointCommand(i, clamped[i]), quiet)) {
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
        // every MCU gets the full line and takes its own joints
        bool ok = false;
        for (int p : openPortList())
            ok = writeTo(p, QStringLiteral("G ") + parts.join(QLatin1Char(' ')), quiet) || ok;
        if (ok) {
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
