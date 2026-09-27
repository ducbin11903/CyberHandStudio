#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QVector>

class HandModel;
class HandController;
class Telemetry;
class SerialLink;
class ConsoleModel;

// Encoder-count <-> joint-angle calibration for one joint.
// Points are (raw counts, true angle): taken by hand (angle read with a protractor,
// counts from the live log) or by the auto sweep (home to the extension stop, then
// drive open loop to the flexion stop). A least-squares line cnt = slope*deg + offset
// gives counts per 90 deg; Apply writes it to the joint and to the firmware (`j`).
class Calibration : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int joint READ joint WRITE setJoint NOTIFY jointChanged)
    Q_PROPERTY(QVariantList points READ points NOTIFY pointsChanged)
    Q_PROPERTY(bool hasFit READ hasFit NOTIFY pointsChanged)
    Q_PROPERTY(double slope READ slope NOTIFY pointsChanged)
    Q_PROPERTY(double offset READ offset NOTIFY pointsChanged)
    Q_PROPERTY(double r2 READ r2 NOTIFY pointsChanged)
    Q_PROPERTY(double fitCountsPer90 READ fitCountsPer90 NOTIFY pointsChanged)
    Q_PROPERTY(QString equation READ equation NOTIFY pointsChanged)
    Q_PROPERTY(bool running READ running NOTIFY stateChanged)
    Q_PROPERTY(QString status READ status NOTIFY stateChanged)
    Q_PROPERTY(double sweepPct READ sweepPct WRITE setSweepPct NOTIFY settingsChanged)
    Q_PROPERTY(double stopDeg READ stopDeg WRITE setStopDeg NOTIFY settingsChanged)
    Q_PROPERTY(bool syncOnConnect READ syncOnConnect WRITE setSyncOnConnect NOTIFY settingsChanged)

public:
    Calibration(HandModel *hand, HandController *ctrl, Telemetry *tel, SerialLink *link,
                ConsoleModel *console, QObject *parent = nullptr);

    int joint() const { return m_joint; }
    void setJoint(int j);
    QVariantList points() const;
    bool hasFit() const { return m_hasFit; }
    double slope() const { return m_slope; }
    double offset() const { return m_offset; }
    double r2() const { return m_r2; }
    double fitCountsPer90() const { return m_slope * 90.0; }
    QString equation() const;
    bool running() const { return m_state != Idle; }
    QString status() const { return m_status; }
    double sweepPct() const { return m_sweepPct; }
    void setSweepPct(double p);
    double stopDeg() const { return m_stopDeg; }
    void setStopDeg(double d);
    bool syncOnConnect() const { return m_sync; }
    void setSyncOnConnect(bool s);

    Q_INVOKABLE bool addPoint(double deg);
    // add a point with explicit counts (tests, typed values)
    Q_INVOKABLE void addPointCounts(double counts, double deg);
    Q_INVOKABLE void removePoint(int i);
    Q_INVOKABLE void clearPoints();
    Q_INVOKABLE bool apply();
    Q_INVOKABLE bool startSweep();
    Q_INVOKABLE void stopSweep();

    // least squares y = a*x + b, returns false if x has no spread
    static bool fitLine(const QVector<double> &x, const QVector<double> &y, double *a, double *b, double *r2);

signals:
    void jointChanged();
    void pointsChanged();
    void stateChanged();
    void settingsChanged();
    void applied(int joint, double countsPer90);

private:
    enum State { Idle, Homing, Flexing };
    void refit();
    void tick();
    void finish(const QString &msg, bool ok);
    void setStatus(const QString &s);

    HandModel *m_hand;
    HandController *m_ctrl;
    Telemetry *m_tel;
    SerialLink *m_link;
    ConsoleModel *m_console;
    int m_joint = 2;
    struct Pt { double cnt; double deg; };
    QVector<Pt> m_pts;
    bool m_hasFit = false;
    double m_slope = 0, m_offset = 0, m_r2 = 0;
    State m_state = Idle;
    QString m_status;
    double m_sweepPct = 30;
    double m_stopDeg = 90;
    bool m_sync = true;
    QTimer m_timer;
    QElapsedTimer m_clock;
    QElapsedTimer m_stall;
};
