#include "TimelineModel.h"

#include "HandModel.h"
#include "Trajectory.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtMath>
#include <algorithm>

static const char *kEasingKeys[] = {"linear", "smooth", "easein", "easeout", "step"};

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
    const Clip &c = m_clips[index.row()];
    switch (role) {
    case TrackRole: return c.track;
    case StartRole: return c.start;
    case DurationRole: return c.duration;
    case TargetRole: return c.target;
    case EasingRole: return c.easing;
    case FromRole: return valueAt(c.track, c.start);
    }
    return {};
}

QHash<int, QByteArray> TimelineModel::roleNames() const
{
    return {{TrackRole, "track"}, {StartRole, "start"}, {DurationRole, "duration"},
            {TargetRole, "target"}, {EasingRole, "easing"}, {FromRole, "fromValue"}};
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

double TimelineModel::valueAt(int track, double t) const
{
    // clips of this track sorted by start
    QVector<const Clip *> cs;
    for (const Clip &c : m_clips)
        if (c.track == track)
            cs << &c;
    std::sort(cs.begin(), cs.end(), [](const Clip *a, const Clip *b) { return a->start < b->start; });
    double v = 0.0; // start extended
    for (const Clip *c : cs) {
        if (t < c->start)
            return v;
        if (t < c->start + c->duration)
            return v + (c->target - v) * ease(c->easing, (t - c->start) / c->duration);
        v = c->target;
    }
    return v;
}

double TimelineModel::freeSpaceAfter(int track, double t) const
{
    double next = m_length;
    for (const Clip &c : m_clips)
        if (c.track == track && c.start >= t - 1e-9)
            next = qMin(next, c.start);
    for (const Clip &c : m_clips)
        if (c.track == track && c.start < t && c.start + c.duration > t)
            return 0;
    return qMax(0.0, next - t);
}

int TimelineModel::addClip(int track, double start, double duration, double target, int easing)
{
    if (track < 0 || track >= trackCount())
        return -1;
    start = qBound(0.0, start, m_length);
    const double room = freeSpaceAfter(track, start);
    if (room < 0.05)
        return -1;
    duration = qBound(0.05, duration, room);
    const Joint *j = m_hand->joint(track);
    Clip c;
    c.track = track;
    c.start = start;
    c.duration = duration;
    c.target = j ? j->clamp(target) : target;
    c.easing = qBound(0, easing, 4);
    const int row = m_clips.size();
    beginInsertRows(QModelIndex(), row, row);
    m_clips.append(c);
    endInsertRows();
    emit countChanged();
    // "from" values of later clips on the track change
    emit dataChanged(index(0), index(m_clips.size() - 1), {FromRole});
    emit changed();
    return row;
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
        emit dataChanged(index(0), index(m_clips.size() - 1), {FromRole});
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

void TimelineModel::setClipTarget(int row, double target)
{
    if (row < 0 || row >= m_clips.size())
        return;
    Clip &c = m_clips[row];
    const Joint *j = m_hand->joint(c.track);
    c.target = j ? j->clamp(target) : target;
    emitRow(row);
}

void TimelineModel::setClipEasing(int row, int easing)
{
    if (row < 0 || row >= m_clips.size())
        return;
    m_clips[row].easing = qBound(0, easing, 4);
    emitRow(row);
}

QVariantMap TimelineModel::clip(int row) const
{
    if (row < 0 || row >= m_clips.size())
        return {};
    const Clip &c = m_clips[row];
    return {{QStringLiteral("track"), c.track}, {QStringLiteral("start"), c.start},
            {QStringLiteral("duration"), c.duration}, {QStringLiteral("target"), c.target},
            {QStringLiteral("easing"), c.easing}, {QStringLiteral("fromValue"), valueAt(c.track, c.start)}};
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

bool TimelineModel::saveJson(const QString &pathOrUrl)
{
    QString path = Trajectory::localPath(pathOrUrl);
    if (!path.endsWith(QLatin1String(".json"), Qt::CaseInsensitive))
        path += QStringLiteral(".json");
    QJsonArray clips;
    for (const Clip &c : m_clips) {
        clips.append(QJsonObject{
            {QStringLiteral("track"), m_hand->joint(c.track)->key()},
            {QStringLiteral("start"), c.start},
            {QStringLiteral("duration"), c.duration},
            {QStringLiteral("target"), c.target},
            {QStringLiteral("easing"), QString::fromLatin1(kEasingKeys[c.easing])},
        });
    }
    const QJsonObject root{{QStringLiteral("format"), QStringLiteral("cyberhand-timeline-1")},
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
    for (const QJsonValue &v : root.value(QStringLiteral("clips")).toArray()) {
        const QJsonObject o = v.toObject();
        int track = m_hand->indexOfKey(o.value(QStringLiteral("track")).toString());
        if (track < 0)
            track = o.value(QStringLiteral("track")).toInt(-1);
        const QString e = o.value(QStringLiteral("easing")).toString().toLower();
        int easing = Smooth;
        for (int k = 0; k < 5; ++k)
            if (e == QLatin1String(kEasingKeys[k]))
                easing = k;
        addClip(track, o.value(QStringLiteral("start")).toDouble(), o.value(QStringLiteral("duration")).toDouble(1),
                o.value(QStringLiteral("target")).toDouble(), easing);
    }
    m_path = path;
    setName(QFileInfo(path).completeBaseName());
    setSelected(-1);
    emit changed();
    return true;
}
