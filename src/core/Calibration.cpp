#include "Calibration.h"

#include "ConsoleModel.h"
#include "HandController.h"
#include "HandModel.h"
#include "LineParser.h"
#include "SerialLink.h"
#include "Telemetry.h"

#include <QtMath>

Calibration::Calibration(HandModel *hand, HandController *ctrl, Telemetry *tel, SerialLink *link,
                         ConsoleModel *console, QObject *parent)
    : QObject(parent), m_hand(hand), m_ctrl(ctrl), m_tel(tel), m_link(link), m_console(console)
{
    m_timer.setInterval(50);
    connect(&m_timer, &QTimer::timeout, this, &Calibration::tick);
    connect(m_link, &SerialLink::connectedChanged, this, [this]() {
        if (!m_link->connected() && running())
            finish(tr("Mất kết nối"), false);
    });
    connect(m_tel, &Telemetry::homedChanged, this, [this]() {
        if (m_state != Homing)
            return;
        if (m_tel->homed() == 0) {
            finish(tr("Home thất bại"), false);
            return;
        }
        if (m_tel->homed() != 1)
            return;
        // extension stop = 0 counts = joint min angle
        const Joint *j = m_hand->joint(m_joint);
        m_pts.clear();
        m_pts.append({0.0, j ? j->minDeg() : 0.0});
        refit();
        m_state = Flexing;
        m_clock.restart();
        m_stall.invalidate();
        m_ctrl->setDuty(qAbs(m_sweepPct));
        setStatus(tr("Đang gập tới chặn…"));
        emit stateChanged();
    });
}

void Calibration::setJoint(int j)
{
    j = qBound(0, j, m_hand->count() - 1);
    if (j == m_joint || running())
        return;
    m_joint = j;
    m_pts.clear();
    refit();
    setStatus(QString());
    emit jointChanged();
}

QVariantList Calibration::points() const
{
    QVariantList l;
    for (const Pt &p : m_pts)
        l << QVariantMap{{QStringLiteral("cnt"), p.cnt}, {QStringLiteral("deg"), p.deg}};
    return l;
}

QString Calibration::equation() const
{
    if (!m_hasFit)
        return QString();
    const QString sign = m_offset < 0 ? QStringLiteral("−") : QStringLiteral("+");
    return QStringLiteral("cnt = %1·deg %2 %3   (R² = %4)")
        .arg(LineParser::num(m_slope, 3), sign, LineParser::num(qAbs(m_offset), 1))
        .arg(m_r2, 0, 'f', 4);
}

void Calibration::setSweepPct(double p)
{
    p = qBound(5.0, p, 100.0);
    if (qFuzzyCompare(p, m_sweepPct))
        return;
    m_sweepPct = p;
    emit settingsChanged();
}

void Calibration::setStopDeg(double d)
{
    d = qBound(-360.0, d, 360.0);
    if (qFuzzyCompare(d + 1000, m_stopDeg + 1000))
        return;
    m_stopDeg = d;
    emit settingsChanged();
}

void Calibration::setSyncOnConnect(bool s)
{
    if (s == m_sync)
        return;
    m_sync = s;
    emit settingsChanged();
}

bool Calibration::addPoint(double deg)
{
    const Joint *j = m_hand->joint(m_joint);
    if (!j || !j->hasFeedback()) {
        m_console->error(tr("Chưa có vị trí đo của %1 — cần log từ firmware").arg(j ? j->name() : QString()));
        return false;
    }
    addPointCounts(j->rawCounts(), deg);
    return true;
}

void Calibration::addPointCounts(double counts, double deg)
{
    m_pts.append({counts, deg});
    refit();
}

void Calibration::removePoint(int i)
{
    if (i < 0 || i >= m_pts.size())
        return;
    m_pts.remove(i);
    refit();
}

void Calibration::clearPoints()
{
    m_pts.clear();
    refit();
}

bool Calibration::fitLine(const QVector<double> &x, const QVector<double> &y, double *a, double *b, double *r2)
{
    const int n = qMin(x.size(), y.size());
    if (n < 2)
        return false;
    double sx = 0, sy = 0;
    for (int i = 0; i < n; ++i) {
        sx += x[i];
        sy += y[i];
    }
    const double mx = sx / n, my = sy / n;
    double sxx = 0, sxy = 0, syy = 0;
    for (int i = 0; i < n; ++i) {
        sxx += (x[i] - mx) * (x[i] - mx);
        sxy += (x[i] - mx) * (y[i] - my);
        syy += (y[i] - my) * (y[i] - my);
    }
    if (sxx < 1e-12)
        return false;
    *a = sxy / sxx;
    *b = my - *a * mx;
    double ssr = 0;
    for (int i = 0; i < n; ++i) {
        const double e = y[i] - (*a * x[i] + *b);
        ssr += e * e;
    }
    *r2 = syy > 1e-12 ? 1.0 - ssr / syy : 1.0;
    return true;
}

void Calibration::refit()
{
    QVector<double> x, y;
    for (const Pt &p : m_pts) {
        x << p.deg;
        y << p.cnt;
    }
    m_hasFit = fitLine(x, y, &m_slope, &m_offset, &m_r2) && qAbs(m_slope) > 1e-9;
    if (!m_hasFit) {
        m_slope = m_offset = m_r2 = 0;
    }
    emit pointsChanged();
}

bool Calibration::apply()
{
    Joint *j = m_hand->joint(m_joint);
    if (!j || !m_hasFit)
        return false;
    const double c90 = fitCountsPer90();
    j->setCountsPer90(c90);
    m_console->info(tr("%1: %2 count/90°").arg(j->name(), LineParser::num(c90, 1)));
    if (m_link->connected())
        m_ctrl->sendJointCountsPer90(m_joint, c90);
    emit applied(m_joint, c90);
    return true;
}

bool Calibration::startSweep()
{
    if (running())
        return false;
    if (!m_link->connected()) {
        m_console->error(tr("Chưa kết nối"));
        return false;
    }
    // home / open loop act on the selected motor: make this joint the main joint
    const bool g474 = !m_ctrl->portKeys().isEmpty();
    if (g474 && !m_ctrl->jointOnPort(m_joint)) {
        m_console->error(m_ctrl->whereIs(m_joint));
        return false;
    }
    if ((m_ctrl->protocol() == HandController::BenchSingle || g474) && m_joint != m_hand->benchJoint())
        m_hand->setBenchJoint(m_joint); // G474: sends m <key>
    m_state = Homing;
    m_clock.start();
    setStatus(tr("Đang home về chặn duỗi…"));
    emit stateChanged();
    m_ctrl->enable();
    m_ctrl->home();
    m_timer.start();
    return true;
}

void Calibration::stopSweep()
{
    if (!running())
        return;
    finish(tr("Đã dừng"), false);
}

void Calibration::finish(const QString &msg, bool ok)
{
    const bool wasFlexing = m_state == Flexing;
    m_state = Idle;
    m_timer.stop();
    if (wasFlexing || !ok)
        m_ctrl->stopOpenLoop();
    setStatus(msg);
    if (ok)
        m_console->info(tr("Hiệu chuẩn: %1").arg(msg));
    else
        m_console->error(tr("Hiệu chuẩn: %1").arg(msg));
    emit stateChanged();
}

void Calibration::tick()
{
    if (m_state == Homing) {
        if (m_clock.elapsed() > 15000)
            finish(tr("Home quá 15 s"), false);
        return;
    }
    if (m_state != Flexing)
        return;
    if (m_tel->flt()) {
        finish(tr("Firmware báo lỗi (flt=%1)").arg(m_tel->flt()), false);
        return;
    }
    // stalled at the flexion stop: duty applied, encoder still
    const bool stalled = m_clock.elapsed() > 600 && m_tel->noMotion();
    if (stalled) {
        if (!m_stall.isValid())
            m_stall.start();
        if (m_stall.elapsed() >= 300) {
            const Joint *j = m_hand->joint(m_joint);
            m_pts.append({j ? j->rawCounts() : m_tel->pos(), m_stopDeg});
            refit();
            finish(m_hasFit ? tr("xong, %1 count/90°").arg(LineParser::num(fitCountsPer90(), 1))
                            : tr("không đo được hành trình"),
                   m_hasFit);
            return;
        }
    } else {
        m_stall.invalidate();
    }
    if (m_clock.elapsed() > 15000)
        finish(tr("Không tới chặn gập trong 15 s"), false);
}

void Calibration::setStatus(const QString &s)
{
    if (s == m_status)
        return;
    m_status = s;
    emit stateChanged();
}
