#pragma once

#include <QObject>
#include <QVariantList>
#include <QVector>

class SerialLink;
class ConsoleModel;
class HandModel;

// Turns UI actions into CLI command lines (one action = one line) and routes them to
// the serial port that owns the joint.
// MCB G474: two MCUs with 3 motors each (U6 = I, T1, T2 | U7 = M, R, P).
//  - 1 port: fw 0.7 sees 3 joints; fw 0.8 with the UART link bridges the other MCU (keys = all 6).
//  - 2 ports: one USB port per MCU, the app routes per joint.
class HandController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int protocol READ protocol WRITE setProtocol NOTIFY protocolChanged)
    Q_PROPERTY(double deadband READ deadband WRITE setDeadband NOTIFY settingsChanged)
    Q_PROPERTY(double kp READ kp WRITE setKp NOTIFY settingsChanged)
    Q_PROPERTY(double ki READ ki WRITE setKi NOTIFY settingsChanged)
    Q_PROPERTY(double kd READ kd WRITE setKd NOTIFY settingsChanged)
    Q_PROPERTY(double dutyLimit READ dutyLimit WRITE setDutyLimit NOTIFY settingsChanged)
    Q_PROPERTY(double logRate READ logRate WRITE setLogRate NOTIFY settingsChanged)
    Q_PROPERTY(double homePct READ homePct WRITE setHomePct NOTIFY settingsChanged)
    Q_PROPERTY(double homeMa READ homeMa WRITE setHomeMa NOTIFY settingsChanged)
    Q_PROPERTY(double velocity READ velocity WRITE setVelocity NOTIFY settingsChanged)
    Q_PROPERTY(double powerRate READ powerRate WRITE setPowerRate NOTIFY settingsChanged)
    Q_PROPERTY(QString lastCommand READ lastCommand NOTIFY lastCommandChanged)
    Q_PROPERTY(QStringList protocolNames READ protocolNames CONSTANT)
    // any port open
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(int openPorts READ openPorts NOTIFY connectedChanged)
    // "2 cổng" mode: Connect opens port 1 and port 2
    Q_PROPERTY(bool dualPort READ dualPort WRITE setDualPort NOTIFY dualPortChanged)
    // per port: open, name, label, mcu, keys, selected, peer, peerUp, id, main
    Q_PROPERTY(QVariantList ports READ ports NOTIFY portChanged)
    // MCU of the open ports ("U6", "U6+U7") and the joints they reach
    Q_PROPERTY(QString portMcu READ portMcu NOTIFY portChanged)
    Q_PROPERTY(QStringList portKeys READ portKeys NOTIFY portChanged)
    Q_PROPERTY(bool layoutKnown READ layoutKnown NOTIFY portChanged)
    // motor selected (m <key>) on the port of the main joint
    Q_PROPERTY(QString selectedKey READ selectedKey NOTIFY portChanged)
    Q_PROPERTY(QString idText READ idText NOTIFY portChanged)
    // console with 2 ports: 0 auto (by command), 1 port 1, 2 port 2, 3 both
    Q_PROPERTY(int consoleTarget READ consoleTarget WRITE setConsoleTarget NOTIFY consoleTargetChanged)
    Q_PROPERTY(QStringList consoleTargets READ consoleTargets NOTIFY portChanged)
    // single-motor commands (o + - r 0 p t v k x H z j I a s) reach the main joint
    Q_PROPERTY(bool benchOnPort READ benchOnPort NOTIFY portChanged)
    Q_PROPERTY(QString benchHint READ benchHint NOTIFY portChanged)

public:
    enum Protocol { BenchSingle = 0, PerJoint = 1, OneLine = 2 };
    Q_ENUM(Protocol)

    HandController(SerialLink *link, ConsoleModel *console, HandModel *hand, QObject *parent = nullptr);
    // second USB port (2-port mode); index 1
    void addLink(SerialLink *link);
    SerialLink *linkAt(int port) const;

    int protocol() const { return m_protocol; }
    void setProtocol(int p);
    double deadband() const { return m_deadband; }
    void setDeadband(double d) { m_deadband = qMax(0.0, d); emit settingsChanged(); }
    double kp() const { return m_kp; }
    double ki() const { return m_ki; }
    double kd() const { return m_kd; }
    void setKp(double v) { m_kp = v; emit settingsChanged(); }
    void setKi(double v) { m_ki = v; emit settingsChanged(); }
    void setKd(double v) { m_kd = v; emit settingsChanged(); }
    double dutyLimit() const { return m_dutyLimit; }
    void setDutyLimit(double v) { m_dutyLimit = qBound(0.0, v, 100.0); emit settingsChanged(); }
    double logRate() const { return m_logRate; }
    void setLogRate(double v) { m_logRate = qBound(0.0, v, 100.0); emit settingsChanged(); }
    double homePct() const { return m_homePct; }
    void setHomePct(double v) { m_homePct = qBound(0.0, v, 100.0); emit settingsChanged(); }
    double homeMa() const { return m_homeMa; }
    void setHomeMa(double v) { m_homeMa = qMax(0.0, v); emit settingsChanged(); }
    double velocity() const { return m_velocity; }
    void setVelocity(double v) { m_velocity = v; emit settingsChanged(); }
    // current (OCM) averaging window in firmware, as updates per second (`a <hz>`)
    double powerRate() const { return m_powerRate; }
    void setPowerRate(double v) { m_powerRate = qBound(1.0, v, 1000.0); emit settingsChanged(); }
    QString lastCommand() const { return m_last; }
    QStringList protocolNames() const;

    // raw line typed by the user or a macro; with 2 ports routed by the command
    // (g <j>, e/d/c/m <key> -> owner, e d c D i l G -> both, "@U7 s" -> U7, rest -> main joint's port)
    Q_INVOKABLE bool send(const QString &line);
    // console input: send() or the port picked in consoleTarget
    Q_INVOKABLE bool sendConsole(const QString &line);
    // one port, no routing
    Q_INVOKABLE bool sendTo(int port, const QString &line);
    // every open port
    Q_INVOKABLE bool sendAll(const QString &line);
    // every motor to duty 0 / off on every open port (G474: D, bench board: 0 then d)
    Q_INVOKABLE void stopAll();

    // e / d / c without a joint = every motor of the MCU (both MCUs with 2 ports)
    Q_INVOKABLE bool enable() { return sendAll(QStringLiteral("e")); }
    Q_INVOKABLE bool disable() { return sendAll(QStringLiteral("d")); }
    Q_INVOKABLE bool home();
    Q_INVOKABLE bool zero() { return sendSingle(QStringLiteral("z")); }
    Q_INVOKABLE bool clearFault() { return sendAll(QStringLiteral("c")); }
    Q_INVOKABLE bool status() { return sendSingle(QStringLiteral("s")); }
    Q_INVOKABLE bool help() { return sendTo(mainPort(), QStringLiteral("h")); }
    Q_INVOKABLE bool stopOpenLoop() { return sendSingle(QStringLiteral("0")); }
    Q_INVOKABLE bool hold() { return sendSingle(QStringLiteral("v 0")); }
    Q_INVOKABLE bool setDuty(double pct);
    Q_INVOKABLE bool dutyUp() { return sendSingle(QStringLiteral("+")); }
    Q_INVOKABLE bool dutyDown() { return sendSingle(QStringLiteral("-")); }
    Q_INVOKABLE bool reverse() { return sendSingle(QStringLiteral("r")); }
    // one motor of a G474 MCU: e <key> / d <key>
    Q_INVOKABLE bool enableJoint(int joint);
    Q_INVOKABLE bool disableJoint(int joint);
    Q_INVOKABLE bool moveJoint(int joint, double deg);
    Q_INVOKABLE bool moveCounts(int counts);
    Q_INVOKABLE bool stepCounts(int counts);
    Q_INVOKABLE bool runVelocity(double degPerSec);
    Q_INVOKABLE bool applyDutyLimit();
    Q_INVOKABLE bool applyPid();
    Q_INVOKABLE bool applyLogRate();
    Q_INVOKABLE bool applyPose(const QVariantList &deg);
    Q_INVOKABLE bool applyPowerRate();
    Q_INVOKABLE bool requestId() { return sendAll(QStringLiteral("i")); }
    // firmware joint calibration: counts at 90 deg (`j <cnt>`)
    Q_INVOKABLE bool sendCountsPer90(double cnt);
    // firmware over-current protection: `I <mA> <ms> <resp> <deg>`,
    // resp 0 trip (disable), 1 retract, 2 hold, 3 warn only
    Q_INVOKABLE bool sendProtection(double ma, double ms, int fwResponse, double retractDeg);
    // send the joint commands as they are now (manual sliders)
    Q_INVOKABLE int streamCommands(bool force = false);

    bool connected() const;
    int openPorts() const;
    bool portOpen(int port) const;
    bool dualPort() const { return m_dual; }
    void setDualPort(bool d);
    QVariantList ports() const;
    QString portMcu() const;
    QStringList portKeys() const;
    bool layoutKnown() const;
    QString selectedKey() const;
    QString idText() const;
    int consoleTarget() const { return m_consoleTarget; }
    void setConsoleTarget(int t);
    QStringList consoleTargets() const;
    bool benchOnPort() const;
    QString benchHint() const;
    // ID line of the firmware on a port (any firmware): shown per port
    void setPortId(int port, const QString &id);
    // G474 ID: MCU name, joints of its motors (fw 0.8 bridge: all 6), peer MCU behind the UART link
    void setPortInfo(int port, const QString &mcu, const QStringList &keys, const QString &peer = QString());
    void clearPortInfo(int port);
    // fw 0.8 link state ("LINK: up U7" / "LINK: lost U7", log field lk); true if it changed
    bool setPeerUp(int port, bool up, const QString &peer = QString());
    bool portHasPeer(int port) const;
    // -1 unknown / no peer, 0 lost, 1 up
    int peerState(int port) const { return (port >= 0 && port < m_ports.size()) ? m_ports[port].peerUp : -1; }
    // "U6" once the ID is known, else the port name
    Q_INVOKABLE QString portLabel(int port) const;
    // open port reporting this MCU, -1 if none
    Q_INVOKABLE int portOfMcu(const QString &mcu) const;
    // port that drives the joint: -1 if no open port has it (layout known) or nothing is open
    Q_INVOKABLE int portOfJoint(int joint) const;
    // port of the main joint (single-motor commands), else the first open port
    Q_INVOKABLE int mainPort() const;
    // true when no MCU layout is known (single bench board) or an open port drives the joint
    Q_INVOKABLE bool jointOnPort(int joint) const;
    // "Khớp M (Middle) nằm ở U7 — cổng U7 chưa mở"
    Q_INVOKABLE QString whereIs(int joint) const;
    // `m <key of the main joint>`; false (and a console note when warn) if not on this port
    Q_INVOKABLE bool selectBenchMotor(bool warn = true);
    // joint calibration to the firmware: `j <cnt>` (G474: m <key>, j <cnt>, m <main joint>)
    Q_INVOKABLE bool sendJointCountsPer90(int joint, double cnt);
    static QString protectionCommand(double ma, double ms, int fwResponse, double retractDeg);

    // Streaming path (player, stream input, gamepad): sends only joints that moved
    // more than the deadband, logged quietly. Always updates the preview.
    int sendJoints(const QVector<double> &deg, bool force = false, bool quiet = true);
    Q_INVOKABLE void resetStreamState();

signals:
    void portChanged();
    void connectedChanged();
    void dualPortChanged();
    void consoleTargetChanged();
    void protocolChanged();
    void settingsChanged();
    void lastCommandChanged();

private:
    struct Port {
        SerialLink *link = nullptr;
        QString mcu;        // ID mcu=
        QStringList keys;   // ID keys=
        QString selected;   // last m <key>
        QString peer;       // ID link= (fw 0.8 bridge)
        int peerUp = -1;    // -1 unknown, 0 lost, 1 up
        QString id;         // ID text
    };
    bool write(const QString &line, bool quiet);
    bool writeTo(int port, const QString &line, bool quiet);
    // single-motor command: refused (console note) when no open port drives the main joint
    bool sendSingle(const QString &line);
    void updateJointFlags();
    void onLinkConnection(int port);
    QString jointCommand(int joint, double deg) const;
    // keys the port can drive now (bridge keys of a lost peer removed)
    QStringList effectiveKeys(int port) const;
    QVector<int> openPortList() const;
    QVector<int> routeOf(QString &line) const;
    int portOfToken(const QString &tok, bool jointNumbers) const;
    int portFor(int joint) const;

    QVector<Port> m_ports;
    ConsoleModel *m_console;
    HandModel *m_hand;
    int m_protocol = BenchSingle;
    double m_deadband = 0.2;
    double m_kp = 0.002, m_ki = 0, m_kd = 0.00002;
    double m_dutyLimit = 60;
    double m_logRate = 20;
    double m_homePct = 30;
    double m_homeMa = 250;
    double m_velocity = 30;
    double m_powerRate = 50;
    QString m_last;
    QVector<double> m_lastSent;
    bool m_dual = false;
    int m_consoleTarget = 0;
};
