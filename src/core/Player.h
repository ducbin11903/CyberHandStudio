#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVector>

class Trajectory;
class HandController;
class HandModel;

// Plays a Trajectory: advances time, updates the 3D preview and streams joint targets.
class Player : public QObject
{
    Q_OBJECT
    Q_PROPERTY(Trajectory *trajectory READ trajectory WRITE setTrajectory NOTIFY trajectoryChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(double position READ position WRITE seek NOTIFY positionChanged)
    Q_PROPERTY(double duration READ duration NOTIFY trajectoryChanged)
    Q_PROPERTY(double speed READ speed WRITE setSpeed NOTIFY speedChanged)
    Q_PROPERTY(int loopMode READ loopMode WRITE setLoopMode NOTIFY loopModeChanged)
    Q_PROPERTY(double sendRate READ sendRate WRITE setSendRate NOTIFY sendRateChanged)
    Q_PROPERTY(bool output READ output WRITE setOutput NOTIFY outputChanged)
    Q_PROPERTY(bool softStart READ softStart WRITE setSoftStart NOTIFY softChanged)
    Q_PROPERTY(bool softStop READ softStop WRITE setSoftStop NOTIFY softChanged)
    Q_PROPERTY(double rampTime READ rampTime WRITE setRampTime NOTIFY softChanged)
    Q_PROPERTY(bool ramping READ ramping NOTIFY playingChanged)

public:
    enum LoopMode { Once = 0, Loop = 1, Bounce = 2 };
    Q_ENUM(LoopMode)

    Player(HandModel *hand, HandController *ctrl, QObject *parent = nullptr);

    Trajectory *trajectory() const { return m_traj; }
    void setTrajectory(Trajectory *t);
    bool playing() const { return m_playing; }
    double position() const { return m_pos; }
    double duration() const;
    double speed() const { return m_speed; }
    void setSpeed(double s);
    int loopMode() const { return m_loop; }
    void setLoopMode(int m);
    double sendRate() const { return m_rate; }
    void setSendRate(double r);
    bool output() const { return m_output; }
    void setOutput(bool o);
    bool softStart() const { return m_softStart; }
    void setSoftStart(bool s) { m_softStart = s; emit softChanged(); }
    bool softStop() const { return m_softStop; }
    void setSoftStop(bool s) { m_softStop = s; emit softChanged(); }
    double rampTime() const { return m_rampTime; }
    void setRampTime(double t) { m_rampTime = qBound(0.1, t, 10.0); emit softChanged(); }
    bool ramping() const { return m_rampPhase != 0; }

    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void toggle();
    Q_INVOKABLE void toStart();
    Q_INVOKABLE void toEnd();
    Q_INVOKABLE void seek(double t);
    Q_INVOKABLE static QStringList loopModeNames();

signals:
    void trajectoryChanged();
    void playingChanged();
    void positionChanged();
    void speedChanged();
    void loopModeChanged();
    void sendRateChanged();
    void outputChanged();
    void softChanged();
    void finished();

private:
    void tick();
    void stopNow();
    void apply(const QVector<double> &q, bool force);
    void startRamp(int phase, const QVector<double> &from, const QVector<double> &to);

    HandModel *m_hand;
    HandController *m_ctrl;
    QPointer<Trajectory> m_traj;
    QTimer m_timer;
    QElapsedTimer m_clock;
    bool m_playing = false;
    double m_pos = 0;
    double m_speed = 1.0;
    int m_loop = Once;
    int m_dir = 1;
    double m_rate = 50;
    bool m_output = true;
    bool m_softStart = true;
    bool m_softStop = false;
    double m_rampTime = 1.0;
    int m_rampPhase = 0; // 0 none, 1 ramp-in, 2 ramp-out
    double m_rampT = 0;
    QVector<double> m_rampFrom, m_rampTo;
};
