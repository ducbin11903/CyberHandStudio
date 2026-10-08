#include "PoseLibrary.h"

#include "HandModel.h"

#include <QJsonObject>

PoseLibrary::PoseLibrary(HandModel *hand, QObject *parent)
    : QAbstractListModel(parent), m_hand(hand)
{
    resetDefaults();
}

int PoseLibrary::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_poses.size();
}

QVariant PoseLibrary::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_poses.size())
        return {};
    switch (role) {
    case NameRole:
    case Qt::DisplayRole: return m_poses[index.row()].name;
    case ValuesRole: return values(index.row());
    }
    return {};
}

QHash<int, QByteArray> PoseLibrary::roleNames() const
{
    return {{NameRole, "name"}, {ValuesRole, "values"}};
}

QStringList PoseLibrary::names() const
{
    QStringList l;
    for (const Pose &p : m_poses)
        l << p.name;
    return l;
}

int PoseLibrary::add(const QString &name, const QVariantList &values)
{
    Pose p;
    p.name = name.trimmed().isEmpty() ? QStringLiteral("Pose %1").arg(m_poses.size() + 1) : name.trimmed();
    for (int i = 0; i < m_hand->count(); ++i)
        p.q << values.value(i, 0.0).toDouble();
    const int existing = find(p.name);
    if (existing >= 0) {
        m_poses[existing] = p;
        emit dataChanged(index(existing), index(existing));
        return existing;
    }
    beginInsertRows(QModelIndex(), m_poses.size(), m_poses.size());
    m_poses.append(p);
    endInsertRows();
    emit countChanged();
    return m_poses.size() - 1;
}

int PoseLibrary::capture(const QString &name)
{
    return add(name, m_hand->previews());
}

void PoseLibrary::remove(int row)
{
    if (row < 0 || row >= m_poses.size())
        return;
    beginRemoveRows(QModelIndex(), row, row);
    m_poses.remove(row);
    endRemoveRows();
    emit countChanged();
}

void PoseLibrary::rename(int row, const QString &name)
{
    if (row < 0 || row >= m_poses.size() || name.trimmed().isEmpty())
        return;
    m_poses[row].name = name.trimmed();
    emit dataChanged(index(row), index(row));
    emit countChanged();
}

int PoseLibrary::find(const QString &name) const
{
    for (int i = 0; i < m_poses.size(); ++i)
        if (m_poses[i].name.compare(name.trimmed(), Qt::CaseInsensitive) == 0)
            return i;
    return -1;
}

QVariantList PoseLibrary::values(int row) const
{
    QVariantList l;
    if (row < 0 || row >= m_poses.size())
        return l;
    for (double v : m_poses[row].q)
        l << v;
    return l;
}

void PoseLibrary::apply(int row)
{
    if (row < 0 || row >= m_poses.size())
        return;
    emit applyRequested(values(row), m_poses[row].name);
}

void PoseLibrary::resetDefaults()
{
    beginResetModel();
    m_poses.clear();
    m_poses.append({QStringLiteral("Open"), {0, 0, 0, 0, 0, 0}});
    m_poses.append({QStringLiteral("Fist"), {60, 60, 90, 90, 90, 90}});
    m_poses.append({QStringLiteral("Point"), {60, 60, 0, 90, 90, 90}});
    m_poses.append({QStringLiteral("Pinch"), {45, 70, 50, 0, 0, 0}}); // T2 flex 45, T1 rotate 70
    m_poses.append({QStringLiteral("Peace"), {60, 60, 0, 0, 90, 90}});
    endResetModel();
    emit countChanged();
}

QJsonArray PoseLibrary::toJson() const
{
    QJsonArray a;
    for (const Pose &p : m_poses) {
        QJsonArray q;
        for (double v : p.q)
            q.append(v);
        a.append(QJsonObject{{QStringLiteral("name"), p.name}, {QStringLiteral("q"), q}});
    }
    return a;
}

void PoseLibrary::fromJson(const QJsonArray &a)
{
    if (a.isEmpty())
        return;
    beginResetModel();
    m_poses.clear();
    for (const QJsonValue &v : a) {
        const QJsonObject o = v.toObject();
        Pose p;
        p.name = o.value(QStringLiteral("name")).toString();
        for (const QJsonValue &x : o.value(QStringLiteral("q")).toArray())
            p.q << x.toDouble();
        p.q.resize(m_hand->count());
        if (!p.name.isEmpty())
            m_poses.append(p);
    }
    endResetModel();
    emit countChanged();
}
