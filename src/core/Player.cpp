#include "Player.h"

#include "HandController.h"
#include "HandModel.h"
#include "Trajectory.h"

#include <QtMath>

Player::Player(HandModel *hand, HandController *ctrl, QObject *parent)
    : QObject(parent), m_hand(hand), m_ctrl(ctrl)
{
    m_timer.setTimerType(Qt::PreciseTimer);
    m_timer.setInterval(int(1000.0 / m_rate));
    connect(&m_timer, &QTimer::timeout, this, &Player::tick);
}

void Player::setTrajectory(Trajectory *t)
{
    if (t == m_traj)
        return;
    stop();
    if (m_traj)
        disconnect(m_traj, nullptr, this, nullptr);
    m_traj = t;
    if (m_traj)
        connect(m_traj, &Trajectory::dataChanged, this, [this]() {
            if (m_pos > duration())
                seek(duration());
            emit trajectoryChanged();
        });
    m_pos = 0;
    emit trajectoryChanged();
    emit positionChanged();
}

double Player::duration() const
{
    return m_traj ? m_traj->duration() : 0.0;
}

void Player::setSpeed(double s)
{
    s = qBound(0.05, s, 10.0);
    if (qFuzzyCompare(s, m_speed))
        return;
    m_speed = s;
    emit speedChanged();
}

void Player::setLoopMode(int m)
{
    m = qBound(0, m, 2);
    if (m == m_loop)
        return;
    m_loop = m;
    emit loopModeChanged();
}

void Player::setSendRate(double r)
{
    r = qBound(1.0, r, 200.0);
    if (qFuzzyCompare(r, m_rate))
        return;
    m_rate = r;
    m_timer.setInterval(qMax(1, int(1000.0 / m_rate)));
    emit sendRateChanged();
}

void Player::setOutput(bool o)
{
    if (o == m_output)
        return;
    m_output = o;
    emit outputChanged();
}

QStringList Player::loopModeNames()
{
    return {QStringLiteral("Once"), QStringLiteral("Loop"), QStringLiteral("Bounce")};
}

void Player::startRamp(int phase, const QVector<double> &from, const QVector<double> &to)
{
    m_rampPhase = phase;
    m_rampT = 0;
    m_rampFrom = from;
    m_rampTo = to;
}

void Player::play()
{
    if (!m_traj || m_traj->empty())
        return;
    if (m_pos >= duration() && m_loop == Once)
        m_pos = 0;
    m_dir = 1;
    m_ctrl->resetStreamState();
    if (m_softStart) {
        const QVector<double> start = m_traj->sample(m_pos);
        startRamp(1, m_hand->measuredVector(), start);
    } else {
        m_rampPhase = 0;
    }
    m_playing = true;
    m_clock.start();
    m_timer.start();
    emit playingChanged();
    tick();
}

void Player::pause()
{
    if (!m_playing)
        return;
    m_playing = false;
    m_rampPhase = 0;
    m_timer.stop();
    emit playingChanged();
}

void Player::stop()
{
    if (m_playing && m_softStop && m_rampPhase != 2 && m_traj && !m_traj->empty()) {
        // ramp back to the first frame, then stop
        startRamp(2, m_hand->commandVector(), m_traj->sample(0));
        m_clock.restart();
        emit playingChanged();
        return;
    }
    stopNow();
}

void Player::stopNow()
{
    const bool wasPlaying = m_playing;
    m_playing = false;
    m_rampPhase = 0;
    m_timer.stop();
    m_pos = 0;
    if (wasPlaying)
        emit playingChanged();
    emit positionChanged();
    if (m_traj && !m_traj->empty())
        m_hand->setCommands(m_traj->sample(0));
}

void Player::toggle()
{
    if (m_playing && m_rampPhase != 2)
        pause();
    else
        play();
}

void Player::toStart()
{
    seek(0);
}

void Player::toEnd()
{
    seek(duration());
}

void Player::seek(double t)
{
    t = qBound(0.0, t, duration());
    m_pos = t;
    emit positionChanged();
    if (m_traj && !m_traj->empty()) {
        const QVector<double> q = m_traj->sample(m_pos);
        if (m_playing)
            apply(q, false);
        else
            m_hand->setCommands(q);
    }
}

void Player::apply(const QVector<double> &q, bool force)
{
    if (m_output)
        m_ctrl->sendJoints(q, force, true);
    else
        m_hand->setCommands(q);
}

void Player::tick()
{
    if (!m_playing || !m_traj)
        return;
    const double dt = qMin(0.25, m_clock.nsecsElapsed() / 1e9);
    m_clock.restart();

    if (m_rampPhase != 0) {
        m_rampT += dt;
        const double a = qMin(1.0, m_rampT / m_rampTime);
        const double e = 0.5 - 0.5 * qCos(M_PI * a);
        QVector<double> q(m_rampTo.size());
        for (int i = 0; i < q.size(); ++i) {
            const double f = m_rampFrom.value(i, qQNaN());
            const double t = m_rampTo[i];
            q[i] = qIsNaN(f) ? t : (qIsNaN(t) ? f : f + (t - f) * e);
        }
        apply(q, false);
        if (a >= 1.0) {
            const int phase = m_rampPhase;
            m_rampPhase = 0;
            if (phase == 2) {
                stopNow();
            } else {
                emit playingChanged();
            }
        }
        return;
    }

    const double dur = duration();
    m_pos += dt * m_speed * m_dir;
    bool finishedNow = false;
    if (m_pos >= dur) {
        if (m_loop == Loop) {
            m_pos = dur > 0 ? std::fmod(m_pos, dur) : 0;
        } else if (m_loop == Bounce) {
            m_pos = dur - (m_pos - dur);
            m_dir = -1;
        } else {
            m_pos = dur;
            finishedNow = true;
        }
    } else if (m_pos <= 0 && m_dir < 0) {
        if (m_loop == Bounce) {
            m_pos = -m_pos;
            m_dir = 1;
        } else {
            m_pos = 0;
        }
    }
    m_pos = qBound(0.0, m_pos, dur);
    apply(m_traj->sample(m_pos), false);
    emit positionChanged();

    if (finishedNow) {
        // make sure the last frame is actually reached
        apply(m_traj->sample(dur), true);
        m_playing = false;
        m_timer.stop();
        emit playingChanged();
        emit finished();
    }
}
