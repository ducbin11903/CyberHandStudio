#pragma once

#include <QObject>
#include <QVector>

class SerialLink;
class ConsoleModel;
class HandModel;

// Turns UI actions into CLI command lines (one action = one line).
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
    // MCB G474: one USB port per MCU, 3 motors each (U6 = I, T1, T2 | U7 = M, R, P)
    Q_PROPERTY(QString portMcu READ portMcu NOTIFY portChanged)
    Q_PROPERTY(QStringList portKeys READ portKeys NOTIFY portChanged)
    Q_PROPERTY(QString selectedKey READ selectedKey NOTIFY portChanged)
    // single-motor commands (o + - r 0 p t v k x H z j I a s) reach the main joint
    Q_PROPERTY(bool benchOnPort READ benchOnPort NOTIFY portChanged)
    Q_PROPERTY(QString benchHint READ benchHint NOTIFY portChanged)

public:
    enum Protocol { BenchSingle = 0, PerJoint = 1, OneLine = 2 };
    Q_ENUM(Protocol)

    HandController(SerialLink *link, ConsoleModel *console, HandModel *hand, QObject *parent = nullptr);

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

    // raw line typed by the user or a macro
    Q_INVOKABLE bool send(const QString &line);

    // e / d / c without a joint = every motor of the MCU
    Q_INVOKABLE bool enable() { return send(QStringLiteral("e")); }
    Q_INVOKABLE bool disable() { return send(QStringLiteral("d")); }
    Q_INVOKABLE bool home();
    Q_INVOKABLE bool zero() { return sendSingle(QStringLiteral("z")); }
    Q_INVOKABLE bool clearFault() { return send(QStringLiteral("c")); }
    Q_INVOKABLE bool status() { return sendSingle(QStringLiteral("s")); }
    Q_INVOKABLE bool help() { return send(QStringLiteral("h")); }
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
    Q_INVOKABLE bool requestId() { return send(QStringLiteral("i")); }
    // firmware joint calibration: counts at 90 deg (`j <cnt>`)
    Q_INVOKABLE bool sendCountsPer90(double cnt);
    // firmware over-current protection: `I <mA> <ms> <resp> <deg>`,
    // resp 0 trip (disable), 1 retract, 2 hold, 3 warn only
    Q_INVOKABLE bool sendProtection(double ma, double ms, int fwResponse, double retractDeg);
    // send the joint commands as they are now (manual sliders)
    Q_INVOKABLE int streamCommands(bool force = false);

    QString portMcu() const { return m_mcu; }
    QStringList portKeys() const { return m_keys; }
    QString selectedKey() const { return m_selected; }
    bool benchOnPort() const;
    QString benchHint() const;
    // ID line of a G474 MCU: its name and the joints of motors 1..3
    void setPortInfo(const QString &mcu, const QStringList &keys);
    void clearPortInfo();
    // true when no MCU layout is known (single bench board) or the joint is on this port
    Q_INVOKABLE bool jointOnPort(int joint) const;
    // "Khớp M (Middle) nằm ở U7 — cắm cổng U7"
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
    void protocolChanged();
    void settingsChanged();
    void lastCommandChanged();

private:
    bool write(const QString &line, bool quiet);
    // single-motor command: refused (console note) when the main joint is on the other MCU
    bool sendSingle(const QString &line);
    void updateJointFlags();
    QString jointCommand(int joint, double deg) const;

    SerialLink *m_link;
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
    QString m_mcu;
    QStringList m_keys;
    QString m_selected;
};
