#include "TimelineModel.h"

#include "HandModel.h"
#include "LineParser.h"
#include "Trajectory.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtMath>
#include <algorithm>

static const char *kEasingKeys[] = {"linear", "smooth", "easein", "easeout", "step"};
static const char *kKindKeys[] = {"move", "hold", "ramp", "sine", "square", "triangle", "sawtooth", "power"};
static const char *kShapeKeys[] = {"in", "out", "inout", "outin"};

static bool isWave(int k)
{
    return k == TimelineModel::Sine || k == TimelineModel::Square || k == TimelineModel::Triangle
        || k == TimelineModel::Sawtooth;
}

static QString n2(double v) { return LineParser::num(v, 2); }

TimelineModel::TimelineModel(HandModel *hand, QObject *parent)
    : QAbstractListModel(parent), m_hand(hand)
{
}

int TimelineModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_clips.size();
}

QVariant TimelineModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_clips.size())
        return {};
    const int row = index.row();
    const Clip &c = m_clips[row];
    switch (role) {
    case TrackRole: return c.track;
    case StartRole: return c.start;
    case DurationRole: return c.duration;
    case TargetRole: return c.target;
    case EasingRole: return c.easing;
    case FromRole: return fromValue(row);
    case KindRole: return c.kind;
    case ARole: return c.a;
    case BRole: return c.b;
    case PeriodRole: return c.period;
    case ShiftRole: return c.shift;
    case ExponentRole: return c.exponent;
    case ShapeRole: return c.shape;
    case EndValueRole: return valueAt(c.track, c.start + c.duration);
    case CurveRole: return clipCurve(row, 48);
    }
    return {};
}

QHash<int, QByteArray> TimelineModel::roleNames() const
{
    return {{TrackRole, "track"}, {StartRole, "start"}, {DurationRole, "duration"},
            {TargetRole, "target"}, {EasingRole, "easing"}, {FromRole, "fromValue"},
            {KindRole, "kind"}, {ARole, "pa"}, {BRole, "pb"}, {PeriodRole, "period"},
            {ShiftRole, "shift"}, {ExponentRole, "exponent"}, {ShapeRole, "shape"},
            {EndValueRole, "endValue"}, {CurveRole, "curve"}};
}

int TimelineModel::trackCount() const
{
    return m_hand->count();
}

void TimelineModel::setLength(double l)
{
    l = qBound(0.5, l, 3600.0);
    if (qFuzzyCompare(l, m_length))
        return;
    m_length = l;
    emit lengthChanged();
    emit changed();
}

void TimelineModel::setName(const QString &n)
{
    if (n == m_name)
        return;
    m_name = n;
    emit nameChanged();
}

void TimelineModel::setSelected(int s)
{
    if (s < -1 || s >= m_clips.size())
        s = -1;
    if (s == m_selected)
        return;
    m_selected = s;
    emit selectedChanged();
}

QStringList TimelineModel::easingNames() const
{
    return {QStringLiteral("Linear"), QStringLiteral("Smooth"), QStringLiteral("Ease in"),
            QStringLiteral("Ease out"), QStringLiteral("Step")};
}

QStringList TimelineModel::kindNames() const
{
    return {QStringLiteral("Move to"), QStringLiteral("Hold"), QStringLiteral("Ramp"), QStringLiteral("Sine"),
            QStringLiteral("Square"), QStringLiteral("Triangle"), QStringLiteral("Sawtooth"), QStringLiteral("Power")};
}

QStringList TimelineModel::shapeNames() const
{
    return {QStringLiteral("In"), QStringLiteral("Out"), QStringLiteral("In-Out"), QStringLiteral("Out-In")};
}

QVariantList TimelineModel::library() const
{
    auto e = [](const char *key, const QString &name, const char *group, const char *icon) {
        return QVariantMap{{QStringLiteral("key"), QString::fromLatin1(key)}, {QStringLiteral("name"), name},
                           {QStringLiteral("group"), QString::fromLatin1(group)},
                           {QStringLiteral("icon"), QString::fromLatin1(icon)}};
    };
    return {
        e("hold", QStringLiteral("Hold"), "Linear", "hold"),
        e("rampUp", QStringLiteral("Ramp up"), "Linear", "ramp"),
        e("rampDown", QStringLiteral("Ramp down"), "Linear", "rampDown"),
        e("move", QStringLiteral("Move to"), "Linear", "curve"),
        e("sine", QStringLiteral("Sine"), "Wave", "sine"),
        e("square", QStringLiteral("Square"), "Wave", "square"),
        e("triangle", QStringLiteral("Triangle"), "Wave", "triangle"),
        e("sawtooth", QStringLiteral("Sawtooth"), "Wave", "sawtooth"),
        e("powIn", QStringLiteral("In"), "Power", "easeIn"),
        e("powOut", QStringLiteral("Out"), "Power", "easeOut"),
        e("powInOut", QStringLiteral("In-Out"), "Power", "inOut"),
        e("powOutIn", QStringLiteral("Out-In"), "Power", "outIn"),
    };
}

double TimelineModel::ease(int easing, double a)
{
    a = qBound(0.0, a, 1.0);
    switch (easing) {
    case Linear: return a;
    case EaseIn: return a * a;
    case EaseOut: return 1 - (1 - a) * (1 - a);
    case Step: return a > 0 ? 1.0 : 0.0;
    default: return 0.5 - 0.5 * qCos(M_PI * a);
    }
}

static double powShape(int shape, double u, double p)
{
    u = qBound(0.0, u, 1.0);
    p = qBound(0.1, p, 10.0);
    switch (shape) {
    case TimelineModel::ShapeOut: return 1 - std::pow(1 - u, p);
    case TimelineModel::ShapeInOut: return u < 0.5 ? 0.5 * std::pow(2 * u, p) : 1 - 0.5 * std::pow(2 - 2 * u, p);
    case TimelineModel::ShapeOutIn: return u < 0.5 ? 0.5 * (1 - std::pow(1 - 2 * u, p)) : 0.5 + 0.5 * std::pow(2 * u - 1, p);
    default: return std::pow(u, p);
    }
}

double TimelineModel::evalClip(const Clip &c, double tl, double from)
{
    const double u = c.duration > 0 ? qBound(0.0, tl / c.duration, 1.0) : 1.0;
    switch (c.kind) {
    case Move: return from + (c.target - from) * ease(c.easing, u);
    case Hold: return c.a;
    case Ramp: return c.a + (c.b - c.a) * u;
    case Power: return c.a + (c.b - c.a) * powShape(c.shape, u, c.exponent);
    default: break;
    }
    const double period = qMax(0.02, c.period);
    double ph = (tl - c.shift) / period;
    ph -= std::floor(ph);
    switch (c.kind) {
    case Sine: return (c.a + c.b) / 2 - (c.b - c.a) / 2 * qCos(2 * M_PI * ph);
    case Square: return ph < 0.5 ? c.a : c.b;
    case Triangle: return ph < 0.5 ? c.a + (c.b - c.a) * 2 * ph : c.b - (c.b - c.a) * (2 * ph - 1);
    case Sawtooth: return c.a + (c.b - c.a) * ph;
    }
    return from;
}

// value at the end of a clip (what the joint holds afterwards)
static double endOf(const TimelineModel::Clip &c, double from)
{
    const double tl = isWave(c.kind) ? qMax(0.0, c.duration - 1e-6) : c.duration;
    return TimelineModel::evalClip(c, tl, from);
}

double TimelineModel::valueAt(int track, double t) const
{
    QVector<const Clip *> cs;
    for (const Clip &c : m_clips)
        if (c.track == track)
            cs << &c;
    std::sort(cs.begin(), cs.end(), [](const Clip *a, const Clip *b) { return a->start < b->start; });
    const Joint *j = m_hand->joint(track);
    auto clampJ = [j](double v) { return j ? j->clamp(v) : v; };
    double v = clampJ(0.0); // start extended
    for (const Clip *c : cs) {
        if (t < c->start)
            return v;
        if (t < c->start + c->duration)
            return clampJ(evalClip(*c, t - c->start, v));
        v = clampJ(endOf(*c, v));
    }
    return v;
}

double TimelineModel::fromValue(int row) const
{
    const Clip &me = m_clips[row];
    QVector<const Clip *> cs;
    for (const Clip &c : m_clips)
        if (c.track == me.track && c.start < me.start - 1e-9)
            cs << &c;
    std::sort(cs.begin(), cs.end(), [](const Clip *a, const Clip *b) { return a->start < b->start; });
    const Joint *j = m_hand->joint(me.track);
    auto clampJ = [j](double v) { return j ? j->clamp(v) : v; };
    double v = clampJ(0.0);
    for (const Clip *c : cs)
        v = clampJ(endOf(*c, v));
    return v;
}

double TimelineModel::freeSpaceAfter(int track, double t) const
{
    double next = m_length;
    for (const Clip &c : m_clips)
        if (c.track == track && c.start >= t - 1e-9)
            next = qMin(next, c.start);
    for (const Clip &c : m_clips)
        if (c.track == track && c.start < t && c.start + c.duration > t + 1e-9)
            return 0;
    return qMax(0.0, next - t);
}

int TimelineModel::insertClip(Clip c)
{
    if (c.track < 0 || c.track >= trackCount())
        return -1;
    c.start = qBound(0.0, c.start, m_length);
    const double room = freeSpaceAfter(c.track, c.start);
    if (room < 0.05) {
        m_error = tr("Không còn chỗ trống ở vị trí này");
        emit lastErrorChanged();
        return -1;
    }
    c.duration = qBound(0.05, c.duration, room);
    if (const Joint *j = m_hand->joint(c.track)) {
        c.target = j->clamp(c.target);
        c.a = j->clamp(c.a);
        c.b = j->clamp(c.b);
    }
    c.kind = qBound(0, c.kind, int(KindCount) - 1);
    c.easing = qBound(0, c.easing, 4);
    c.shape = qBound(0, c.shape, 3);
    c.period = qMax(0.02, c.period);
    c.exponent = qBound(0.1, c.exponent, 10.0);
    const int row = m_clips.size();
    beginInsertRows(QModelIndex(), row, row);
    m_clips.append(c);
    endInsertRows();
    emit countChanged();
    // "from" values of later clips on the track change
    emit dataChanged(index(0), index(m_clips.size() - 1));
    emit changed();
    return row;
}

int TimelineModel::addClip(int track, double start, double duration, double target, int easing)
{
    Clip c;
    c.track = track;
    c.start = start;
    c.duration = duration;
    c.kind = Move;
    c.target = target;
    c.easing = easing;
    c.b = target;
    return insertClip(c);
}

TimelineModel::Clip TimelineModel::presetClip(const QString &key, int track, bool *ok) const
{
    *ok = true;
    Clip c;
    c.track = track;
    const Joint *j = m_hand->joint(track);
    const double lo = j ? j->minDeg() : 0, hi = j ? j->maxDeg() : 90;
    c.a = lo;
    c.b = hi;
    if (key.startsWith(QLatin1String("custom:"))) {
        const int i = key.mid(7).toInt();
        if (i < 0 || i >= m_custom.size()) {
            *ok = false;
            return c;
        }
        const QVariantMap m = m_custom[i].toMap();
        bool okc = false;
        c = clipFromJson(QJsonObject::fromVariantMap(m.value(QStringLiteral("clip")).toMap()), &okc);
        c.track = track;
        *ok = okc;
        return c;
    }
    if (key == QLatin1String("hold")) { c.kind = Hold; c.duration = 1; }
    else if (key == QLatin1String("rampUp")) { c.kind = Ramp; c.a = lo; c.b = hi; c.duration = 1; }
    else if (key == QLatin1String("rampDown")) { c.kind = Ramp; c.a = hi; c.b = lo; c.duration = 1; }
    else if (key == QLatin1String("move")) { c.kind = Move; c.easing = Smooth; c.duration = 1; }
    else if (key == QLatin1String("sine")) { c.kind = Sine; c.duration = 2; }
    else if (key == QLatin1String("square")) { c.kind = Square; c.duration = 2; }
    else if (key == QLatin1String("triangle")) { c.kind = Triangle; c.duration = 2; }
    else if (key == QLatin1String("sawtooth")) { c.kind = Sawtooth; c.duration = 2; }
    else if (key == QLatin1String("powIn")) { c.kind = Power; c.shape = ShapeIn; c.duration = 1; }
    else if (key == QLatin1String("powOut")) { c.kind = Power; c.shape = ShapeOut; c.duration = 1; }
    else if (key == QLatin1String("powInOut")) { c.kind = Power; c.shape = ShapeInOut; c.duration = 1; }
    else if (key == QLatin1String("powOutIn")) { c.kind = Power; c.shape = ShapeOutIn; c.duration = 1; }
    else *ok = false;
    c.period = 1;
    c.exponent = 2;
    return c;
}

int TimelineModel::addPreset(int track, double start, const QString &key)
{
    bool ok = false;
    Clip c = presetClip(key, track, &ok);
    if (!ok) {
        m_error = tr("Không có clip mẫu \"%1\"").arg(key);
        emit lastErrorChanged();
        return -1;
    }
    c.start = start;
    // continue from where the joint is at that time
    const Joint *j = m_hand->joint(track);
    const double lo = j ? j->minDeg() : 0, hi = j ? j->maxDeg() : 90;
    const double from = valueAt(track, start - 1e-6);
    const bool low = from < (lo + hi) / 2;
    if (!key.startsWith(QLatin1String("custom:"))) {
        if (key == QLatin1String("hold")) c.a = from;
        else if (key == QLatin1String("move")) c.target = low ? hi : lo;
        else if (key == QLatin1String("rampUp") && from < hi - 1) c.a = from;
        else if (key == QLatin1String("rampDown") && from > lo + 1) c.a = from;
        else if (c.kind == Power) { c.a = from; c.b = low ? hi : lo; }
    }
    return insertClip(c);
}

int TimelineModel::duplicateClip(int row)
{
    if (row < 0 || row >= m_clips.size())
        return -1;
    Clip c = m_clips[row];
    c.start = c.start + c.duration;
    const int r = insertClip(c);
    if (r >= 0)
        setSelected(r);
    return r;
}

void TimelineModel::removeClip(int row)
{
    if (row < 0 || row >= m_clips.size())
        return;
    beginRemoveRows(QModelIndex(), row, row);
    m_clips.remove(row);
    endRemoveRows();
    if (m_selected == row)
        setSelected(-1);
    else if (m_selected > row)
        setSelected(m_selected - 1);
    emit countChanged();
    if (!m_clips.isEmpty())
        emit dataChanged(index(0), index(m_clips.size() - 1));
    emit changed();
}

void TimelineModel::neighbours(int row, double *lo, double *hi) const
{
    const Clip &me = m_clips[row];
    *lo = 0;
    *hi = m_length;
    for (int i = 0; i < m_clips.size(); ++i) {
        if (i == row || m_clips[i].track != me.track)
            continue;
        const Clip &c = m_clips[i];
        if (c.start + c.duration <= me.start + 1e-9)
            *lo = qMax(*lo, c.start + c.duration);
        else if (c.start >= me.start + me.duration - 1e-9 || c.start >= me.start)
            *hi = qMin(*hi, c.start);
    }
}

void TimelineModel::emitRow(int row)
{
    emit dataChanged(index(0), index(m_clips.size() - 1));
    emit changed();
    Q_UNUSED(row)
}

void TimelineModel::moveClip(int row, double start)
{
    if (row < 0 || row >= m_clips.size())
        return;
    double lo, hi;
    neighbours(row, &lo, &hi);
    Clip &c = m_clips[row];
    const double ns = qBound(lo, start, qMax(lo, hi - c.duration));
    if (qFuzzyCompare(ns + 1, c.start + 1))
        return;
    c.start = ns;
    emitRow(row);
}

void TimelineModel::resizeClip(int row, double duration)
{
    if (row < 0 || row >= m_clips.size())
        return;
    double lo, hi;
    neighbours(row, &lo, &hi);
    Clip &c = m_clips[row];
    const double nd = qBound(0.05, duration, qMax(0.05, hi - c.start));
    if (qFuzzyCompare(nd, c.duration))
        return;
    c.duration = nd;
    emitRow(row);
}

void TimelineModel::setClipEnd(int row, double end)
{
    if (row < 0 || row >= m_clips.size())
        return;
    resizeClip(row, end - m_clips[row].start);
}

bool TimelineModel::setClipTrack(int row, int track)
{
    if (row < 0 || row >= m_clips.size() || track < 0 || track >= trackCount())
        return false;
    Clip &c = m_clips[row];
    if (c.track == track)
        return true;
    for (int i = 0; i < m_clips.size(); ++i) {
        const Clip &o = m_clips[i];
        if (i != row && o.track == track && o.start < c.start + c.duration - 1e-9 && c.start < o.start + o.duration - 1e-9) {
            m_error = tr("Làn %1 đã có clip ở khoảng thời gian này").arg(m_hand->joint(track)->name());
            emit lastErrorChanged();
            return false;
        }
    }
    c.track = track;
    const Joint *j = m_hand->joint(track);
    c.target = j->clamp(c.target);
    c.a = j->clamp(c.a);
    c.b = j->clamp(c.b);
    emitRow(row);
    return true;
}

void TimelineModel::setClipTarget(int row, double target)
{
    setClipParam(row, QStringLiteral("target"), target);
}

void TimelineModel::setClipEasing(int row, int easing)
{
    setClipParam(row, QStringLiteral("easing"), easing);
}

void TimelineModel::setClipParam(int row, const QString &p, double v)
{
    if (row < 0 || row >= m_clips.size() || qIsNaN(v))
        return;
    // timing goes through move/resize so neighbours are respected
    if (p == QLatin1String("start"))
        return moveClip(row, v);
    if (p == QLatin1String("end"))
        return setClipEnd(row, v);
    if (p == QLatin1String("duration"))
        return resizeClip(row, v);
    Clip &c = m_clips[row];
    const Joint *j = m_hand->joint(c.track);
    auto clampJ = [j](double x) { return j ? j->clamp(x) : x; };
    if (p == QLatin1String("kind")) {
        const int k = qBound(0, int(v), int(KindCount) - 1);
        if (k == c.kind)
            return;
        const double from = fromValue(row);
        if (c.kind == Move && k != Move) {
            c.a = from;
            c.b = c.target;
        } else if (k == Move) {
            c.target = c.b;
        }
        if (isWave(k) && qAbs(c.b - c.a) < 1e-6) {
            c.a = j ? j->minDeg() : 0;
            c.b = j ? j->maxDeg() : 90;
        }
        c.kind = k;
    } else if (p == QLatin1String("target")) c.target = clampJ(v);
    else if (p == QLatin1String("easing")) c.easing = qBound(0, int(v), 4);
    else if (p == QLatin1String("a")) c.a = clampJ(v);
    else if (p == QLatin1String("b")) c.b = clampJ(v);
    else if (p == QLatin1String("period")) c.period = qBound(0.02, v, 3600.0);
    else if (p == QLatin1String("shift")) c.shift = v;
    else if (p == QLatin1String("exponent")) c.exponent = qBound(0.1, v, 10.0);
    else if (p == QLatin1String("shape")) c.shape = qBound(0, int(v), 3);
    else if (p == QLatin1String("slope")) {
        // ramp: keep the start value, move the end value to match the slope
        c.b = clampJ(c.a + v * c.duration);
    } else
        return;
    emitRow(row);
}

QVariantMap TimelineModel::clip(int row) const
{
    if (row < 0 || row >= m_clips.size())
        return {};
    const Clip &c = m_clips[row];
    return {{QStringLiteral("row"), row},
            {QStringLiteral("track"), c.track},
            {QStringLiteral("start"), c.start},
            {QStringLiteral("duration"), c.duration},
            {QStringLiteral("end"), c.start + c.duration},
            {QStringLiteral("kind"), c.kind},
            {QStringLiteral("kindName"), kindNames().value(c.kind)},
            {QStringLiteral("target"), c.target},
            {QStringLiteral("easing"), c.easing},
            {QStringLiteral("a"), c.a},
            {QStringLiteral("b"), c.b},
            {QStringLiteral("period"), c.period},
            {QStringLiteral("shift"), c.shift},
            {QStringLiteral("exponent"), c.exponent},
            {QStringLiteral("shape"), c.shape},
            {QStringLiteral("slope"), c.duration > 0 ? (c.b - c.a) / c.duration : 0.0},
            {QStringLiteral("fromValue"), fromValue(row)},
            {QStringLiteral("endValue"), valueAt(c.track, c.start + c.duration)},
            {QStringLiteral("formula"), formula(row)}};
}

QVariantList TimelineModel::clipCurve(int row, int points) const
{
    QVariantList l;
    if (row < 0 || row >= m_clips.size())
        return l;
    const Clip &c = m_clips[row];
    const Joint *j = m_hand->joint(c.track);
    const double from = fromValue(row);
    points = qBound(2, points, 2000);
    // waves: sample densely enough to show every period edge
    if (isWave(c.kind))
        points = qBound(points, int(c.duration / qMax(0.02, c.period) * 24), 2000);
    for (int i = 0; i <= points; ++i) {
        double tl = c.duration * i / points;
        if (i == points && isWave(c.kind))
            tl = qMax(0.0, c.duration - 1e-6);
        const double v = evalClip(c, tl, from);
        l << (j ? j->clamp(v) : v);
    }
    return l;
}

QString TimelineModel::formula(int row) const
{
    if (row < 0 || row >= m_clips.size())
        return {};
    const Clip &c = m_clips[row];
    const QString D = n2(c.duration);
    const QString s = c.shift >= 0 ? QStringLiteral("t − %1").arg(n2(c.shift)) : QStringLiteral("t + %1").arg(n2(-c.shift));
    const QString ph = QStringLiteral("(%1)/%2").arg(s, n2(c.period));
    const QString a = n2(c.a), d = n2(c.b - c.a);
    switch (c.kind) {
    case Move: {
        static const char *f[] = {"t/D", "smooth(t/D)", "(t/D)²", "1 − (1 − t/D)²", "step(t)"};
        return QStringLiteral("y = y₀ + (%1 − y₀)·%2   y₀ = %3, D = %4 s")
            .arg(n2(c.target), QString::fromUtf8(f[qBound(0, c.easing, 4)]), n2(fromValue(row)), D);
    }
    case Hold: return QStringLiteral("y = %1").arg(a);
    case Ramp: return QStringLiteral("y = %1 + %2·t/%3   (%4 °/s)").arg(a, d, D, n2((c.b - c.a) / qMax(1e-9, c.duration)));
    case Sine: return QStringLiteral("y = %1 − %2·cos(2π%3)").arg(n2((c.a + c.b) / 2), n2((c.b - c.a) / 2), ph);
    case Square: return QStringLiteral("y = %1 nếu frac(%2) < ½, ngược lại %3").arg(a, ph, n2(c.b));
    case Triangle: return QStringLiteral("y = %1 + %2·tri(%3)").arg(a, d, ph);
    case Sawtooth: return QStringLiteral("y = %1 + %2·frac(%3)").arg(a, d, ph);
    case Power: {
        const QString p = n2(c.exponent);
        const QString u = QStringLiteral("t/") + D;
        QString f;
        switch (c.shape) {
        case ShapeOut: f = QStringLiteral("(1 − (1 − %1)^%2)").arg(u, p); break;
        case ShapeInOut: f = QStringLiteral("inout(%1, %2)").arg(u, p); break;
        case ShapeOutIn: f = QStringLiteral("outin(%1, %2)").arg(u, p); break;
        default: f = QStringLiteral("(%1)^%2").arg(u, p); break;
        }
        return QStringLiteral("y = %1 + %2·%3").arg(a, d, f);
    }
    }
    return {};
}

void TimelineModel::clear()
{
    beginResetModel();
    m_clips.clear();
    endResetModel();
    m_path.clear();
    setName(QStringLiteral("untitled"));
    setSelected(-1);
    emit countChanged();
    emit changed();
}

void TimelineModel::makeDemo()
{
    beginResetModel();
    m_clips.clear();
    endResetModel();
    m_length = 6.0;
    emit lengthChanged();
    // opposition: thumb rotates in, index closes to meet it, others curl a bit, then release
    auto add = [&](int track, double s, double d, double tgt, int e) { addClip(track, s, d, tgt, e); };
    add(0, 0.0, 1.0, 70, Smooth);   // T2 rotate
    add(0, 3.6, 1.2, 0, Smooth);
    add(1, 0.4, 1.0, 45, Smooth);   // T1 flex
    add(1, 3.4, 1.2, 0, EaseOut);
    add(2, 0.8, 1.2, 55, Smooth);   // I
    add(2, 3.2, 1.2, 0, Smooth);
    add(3, 1.0, 1.4, 80, Linear);   // M
    add(3, 3.8, 1.2, 0, Linear);
    add(4, 1.2, 1.4, 85, Linear);   // R
    add(4, 4.0, 1.2, 0, Linear);
    add(5, 1.4, 1.4, 90, EaseIn);   // P
    add(5, 4.2, 1.2, 0, EaseOut);
    m_path.clear();
    setName(QStringLiteral("finger_opposition"));
    emit changed();
}

void TimelineModel::compileInto(Trajectory *out, double rate) const
{
    if (!out)
        return;
    rate = qBound(5.0, rate, 500.0);
    const int n = int(std::ceil(m_length * rate)) + 1;
    QVector<double> times(n);
    QVector<QVector<double>> vals(trackCount(), QVector<double>(n));
    for (int i = 0; i < n; ++i) {
        const double t = qMin(m_length, i / rate);
        times[i] = t;
        for (int tr = 0; tr < trackCount(); ++tr)
            vals[tr][i] = valueAt(tr, t);
    }
    out->setData(times, vals);
    out->setName(m_name);
}

QJsonObject TimelineModel::clipToJson(const Clip &c)
{
    QJsonObject o{{QStringLiteral("start"), c.start}, {QStringLiteral("duration"), c.duration},
                  {QStringLiteral("kind"), QString::fromLatin1(kKindKeys[qBound(0, c.kind, int(KindCount) - 1)])}};
    switch (c.kind) {
    case Move:
        o.insert(QStringLiteral("target"), c.target);
        o.insert(QStringLiteral("easing"), QString::fromLatin1(kEasingKeys[qBound(0, c.easing, 4)]));
        break;
    case Hold:
        o.insert(QStringLiteral("value"), c.a);
        break;
    case Ramp:
        o.insert(QStringLiteral("from"), c.a);
        o.insert(QStringLiteral("to"), c.b);
        break;
    case Power:
        o.insert(QStringLiteral("from"), c.a);
        o.insert(QStringLiteral("to"), c.b);
        o.insert(QStringLiteral("exponent"), c.exponent);
        o.insert(QStringLiteral("shape"), QString::fromLatin1(kShapeKeys[qBound(0, c.shape, 3)]));
        break;
    default:
        o.insert(QStringLiteral("trough"), c.a);
        o.insert(QStringLiteral("crest"), c.b);
        o.insert(QStringLiteral("period"), c.period);
        o.insert(QStringLiteral("shift"), c.shift);
        break;
    }
    return o;
}

TimelineModel::Clip TimelineModel::clipFromJson(const QJsonObject &o, bool *ok)
{
    Clip c;
    const QString k = o.value(QStringLiteral("kind")).toString(QStringLiteral("move")).toLower();
    bool found = false;
    for (int i = 0; i < int(KindCount); ++i) {
        if (k == QLatin1String(kKindKeys[i])) {
            c.kind = i;
            found = true;
        }
    }
    c.start = o.value(QStringLiteral("start")).toDouble();
    c.duration = o.value(QStringLiteral("duration")).toDouble(1);
    c.target = o.value(QStringLiteral("target")).toDouble();
    const QString e = o.value(QStringLiteral("easing")).toString(QStringLiteral("smooth")).toLower();
    for (int i = 0; i < 5; ++i)
        if (e == QLatin1String(kEasingKeys[i]))
            c.easing = i;
    switch (c.kind) {
    case Move: c.b = c.target; break;
    case Hold: c.a = o.value(QStringLiteral("value")).toDouble(); break;
    case Ramp:
    case Power:
        c.a = o.value(QStringLiteral("from")).toDouble();
        c.b = o.value(QStringLiteral("to")).toDouble(90);
        break;
    default:
        c.a = o.value(QStringLiteral("trough")).toDouble(0);
        c.b = o.value(QStringLiteral("crest")).toDouble(90);
        break;
    }
    c.period = o.value(QStringLiteral("period")).toDouble(1);
    c.shift = o.value(QStringLiteral("shift")).toDouble(0);
    c.exponent = o.value(QStringLiteral("exponent")).toDouble(2);
    const QString s = o.value(QStringLiteral("shape")).toString(QStringLiteral("in")).toLower();
    for (int i = 0; i < 4; ++i)
        if (s == QLatin1String(kShapeKeys[i]))
            c.shape = i;
    if (ok)
        *ok = found;
    return c;
}

bool TimelineModel::saveJson(const QString &pathOrUrl)
{
    QString path = Trajectory::localPath(pathOrUrl);
    if (!path.endsWith(QLatin1String(".json"), Qt::CaseInsensitive))
        path += QStringLiteral(".json");
    QJsonArray clips;
    for (const Clip &c : m_clips) {
        QJsonObject o = clipToJson(c);
        o.insert(QStringLiteral("track"), m_hand->joint(c.track)->key());
        clips.append(o);
    }
    const QJsonObject root{{QStringLiteral("format"), QStringLiteral("cyberhand-timeline-2")},
                           {QStringLiteral("name"), QFileInfo(path).completeBaseName()},
                           {QStringLiteral("length"), m_length},
                           {QStringLiteral("clips"), clips}};
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        m_error = tr("Không ghi được file: %1").arg(path);
        emit lastErrorChanged();
        return false;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    m_path = path;
    setName(QFileInfo(path).completeBaseName());
    emit nameChanged();
    return true;
}

bool TimelineModel::loadJson(const QString &pathOrUrl)
{
    const QString path = Trajectory::localPath(pathOrUrl);
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        m_error = tr("Không mở được file: %1").arg(path);
        emit lastErrorChanged();
        return false;
    }
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (doc.isNull()) {
        m_error = tr("JSON lỗi: %1").arg(err.errorString());
        emit lastErrorChanged();
        return false;
    }
    const QJsonObject root = doc.object();
    beginResetModel();
    m_clips.clear();
    endResetModel();
    m_length = root.value(QStringLiteral("length")).toDouble(6.0);
    emit lengthChanged();
    int skipped = 0;
    for (const QJsonValue &v : root.value(QStringLiteral("clips")).toArray()) {
        const QJsonObject o = v.toObject();
        int track = m_hand->indexOfKey(o.value(QStringLiteral("track")).toString());
        if (track < 0)
            track = o.value(QStringLiteral("track")).toInt(-1);
        bool ok = false;
        Clip c = clipFromJson(o, &ok);
        c.track = track;
        if (!ok || insertClip(c) < 0)
            ++skipped;
    }
    m_path = path;
    setName(QFileInfo(path).completeBaseName());
    setSelected(-1);
    if (skipped) {
        m_error = tr("Bỏ qua %1 clip lỗi hoặc chồng nhau").arg(skipped);
        emit lastErrorChanged();
    }
    emit changed();
    return true;
}

void TimelineModel::saveCustom(int row, const QString &name)
{
    if (row < 0 || row >= m_clips.size())
        return;
    QJsonObject o = clipToJson(m_clips[row]);
    o.remove(QStringLiteral("start"));
    const QString n = name.trimmed().isEmpty() ? kindNames().value(m_clips[row].kind) : name.trimmed();
    m_custom.append(QVariantMap{{QStringLiteral("name"), n}, {QStringLiteral("kind"), m_clips[row].kind},
                                {QStringLiteral("clip"), o.toVariantMap()}});
    emit customChanged();
}

void TimelineModel::removeCustom(int index)
{
    if (index < 0 || index >= m_custom.size())
        return;
    m_custom.removeAt(index);
    emit customChanged();
}

QJsonArray TimelineModel::customToJson() const
{
    return QJsonArray::fromVariantList(m_custom);
}

void TimelineModel::customFromJson(const QJsonArray &a)
{
    m_custom.clear();
    for (const QJsonValue &v : a) {
        QVariantMap m = v.toObject().toVariantMap();
        bool ok = false;
        const Clip c = clipFromJson(QJsonObject::fromVariantMap(m.value(QStringLiteral("clip")).toMap()), &ok);
        if (!ok)
            continue;
        m.insert(QStringLiteral("kind"), c.kind);
        m_custom.append(m);
    }
    emit customChanged();
}
