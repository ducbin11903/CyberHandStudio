#include "HandModel.h"

#include <QtMath>

Joint::Joint(int index, const QString &key, const QString &name, QObject *parent)
    : QObject(parent), m_index(index), m_key(key), m_name(name)
{
}

void Joint::setName(const QString &n)
{
    if (n == m_name || n.isEmpty())
        return;
    m_name = n;
    emit configChanged();
}

void Joint::setCountsPer90(double c)
{
    if (c == 0 || c == m_cnt90)
        return;
    m_cnt90 = c;
    emit configChanged();
}

void Joint::setMinDeg(double v)
{
    if (v == m_min)
        return;
    m_min = v;
    emit configChanged();
}

void Joint::setMaxDeg(double v)
{
    if (v == m_max)
        return;
    m_max = v;
    emit configChanged();
}

void Joint::setEnabled(bool e)
{
    if (e == m_enabled)
        return;
    m_enabled = e;
    emit configChanged();
}

void Joint::setCommandDeg(double d)
{
    d = clamp(d);
    if (qFuzzyCompare(d + 1000.0, m_command + 1000.0))
        return;
    m_command = d;
    emit commandChanged();
    if (!m_hasFeedback)
        emit previewChanged();
}

void Joint::setFeedback(double measuredDeg, double targetDeg)
{
    const bool had = m_hasFeedback;
    m_measured = measuredDeg;
    m_target = targetDeg;
    m_hasFeedback = true;
    emit stateChanged();
    emit previewChanged();
    Q_UNUSED(had)
}

void Joint::setCurrent(double ma)
{
    m_current = ma;
    if (qAbs(ma) > m_peak)
        m_peak = qAbs(ma);
    emit stateChanged();
}

void Joint::clearFeedback()
{
    if (!m_hasFeedback && m_current == 0)
        return;
    m_hasFeedback = false;
    m_current = 0;
    emit stateChanged();
    emit previewChanged();
}

void Joint::resetPeak()
{
    m_peak = qAbs(m_current);
    emit stateChanged();
}

QJsonObject Joint::toJson() const
{
    return {
        {QStringLiteral("key"), m_key},
        {QStringLiteral("name"), m_name},
        {QStringLiteral("countsPer90"), m_cnt90},
        {QStringLiteral("minDeg"), m_min},
        {QStringLiteral("maxDeg"), m_max},
        {QStringLiteral("enabled"), m_enabled},
    };
}

void Joint::fromJson(const QJsonObject &o)
{
    if (o.contains(QStringLiteral("name")))
        m_name = o.value(QStringLiteral("name")).toString(m_name);
    m_cnt90 = o.value(QStringLiteral("countsPer90")).toDouble(m_cnt90);
    if (m_cnt90 == 0)
        m_cnt90 = 937;
    m_min = o.value(QStringLiteral("minDeg")).toDouble(m_min);
    m_max = o.value(QStringLiteral("maxDeg")).toDouble(m_max);
    m_enabled = o.value(QStringLiteral("enabled")).toBool(m_enabled);
    emit configChanged();
}

// ---------------------------------------------------------------------------

HandModel::HandModel(QObject *parent)
    : QObject(parent)
{
    const QStringList keys = {QStringLiteral("T2"), QStringLiteral("T1"), QStringLiteral("I"),
                              QStringLiteral("M"), QStringLiteral("R"), QStringLiteral("P")};
    const QStringList names = {QStringLiteral("Thumb Rotation"), QStringLiteral("Thumb Flexion"),
                               QStringLiteral("Index"), QStringLiteral("Middle"),
                               QStringLiteral("Ring"), QStringLiteral("Pinky")};
    for (int i = 0; i < keys.size(); ++i) {
        auto *j = new Joint(i, keys[i], names[i], this);
        connect(j, &Joint::configChanged, this, &HandModel::namesChanged);
        connect(j, &Joint::commandChanged, this, &HandModel::commandsChanged);
        m_joints.append(j);
    }
}

QList<QObject *> HandModel::jointObjects() const
{
    QList<QObject *> l;
    for (Joint *j : m_joints)
        l.append(j);
    return l;
}

void HandModel::setBenchJoint(int j)
{
    j = qBound(0, j, m_joints.size() - 1);
    if (j == m_bench)
        return;
    m_bench = j;
    clearFeedback();
    emit benchJointChanged();
}

QStringList HandModel::names() const
{
    QStringList l;
    for (Joint *j : m_joints)
        l << j->name();
    return l;
}

QStringList HandModel::keys() const
{
    QStringList l;
    for (Joint *j : m_joints)
        l << j->key();
    return l;
}

int HandModel::indexOfKey(const QString &key) const
{
    for (Joint *j : m_joints) {
        if (j->key().compare(key, Qt::CaseInsensitive) == 0)
            return j->index();
    }
    return -1;
}

QVector<double> HandModel::commandVector() const
{
    QVector<double> v;
    for (Joint *j : m_joints)
        v << j->commandDeg();
    return v;
}

QVector<double> HandModel::previewVector() const
{
    QVector<double> v;
    for (Joint *j : m_joints)
        v << j->previewDeg();
    return v;
}

QVector<double> HandModel::measuredVector() const
{
    QVector<double> v;
    for (Joint *j : m_joints)
        v << (j->hasFeedback() ? j->measuredDeg() : j->commandDeg());
    return v;
}

QVariantList HandModel::commands() const
{
    QVariantList l;
    for (Joint *j : m_joints)
        l << j->commandDeg();
    return l;
}

QVariantList HandModel::previews() const
{
    QVariantList l;
    for (Joint *j : m_joints)
        l << j->previewDeg();
    return l;
}

void HandModel::setCommands(const QVector<double> &deg)
{
    for (int i = 0; i < m_joints.size() && i < deg.size(); ++i) {
        if (!qIsNaN(deg[i]))
            m_joints[i]->setCommandDeg(deg[i]);
    }
}

void HandModel::setCommandList(const QVariantList &deg)
{
    QVector<double> v;
    for (const QVariant &d : deg)
        v << d.toDouble();
    setCommands(v);
}

void HandModel::resetPeaks()
{
    for (Joint *j : m_joints)
        j->resetPeak();
}

void HandModel::clearFeedback()
{
    for (Joint *j : m_joints)
        j->clearFeedback();
}

QJsonArray HandModel::toJson() const
{
    QJsonArray a;
    for (Joint *j : m_joints)
        a.append(j->toJson());
    return a;
}

void HandModel::fromJson(const QJsonArray &a)
{
    for (const QJsonValue &v : a) {
        const QJsonObject o = v.toObject();
        const int i = indexOfKey(o.value(QStringLiteral("key")).toString());
        if (i >= 0)
            m_joints[i]->fromJson(o);
    }
    emit namesChanged();
}
