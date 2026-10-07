#include "EventLog.h"

#include <QTime>

EventLog::EventLog(QObject *parent)
    : QAbstractListModel(parent)
{
}

int EventLog::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_items.size();
}

QVariant EventLog::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_items.size())
        return {};
    const Item &it = m_items[index.row()];
    switch (role) {
    case TimeRole: return it.time;
    case CodeRole: return it.code;
    case TextRole:
    case Qt::DisplayRole: return it.text;
    case AlarmRole: return it.alarm;
    }
    return {};
}

QHash<int, QByteArray> EventLog::roleNames() const
{
    return {{TimeRole, "time"}, {CodeRole, "code"}, {TextRole, "text"}, {AlarmRole, "alarm"}};
}

void EventLog::add(const QString &code, const QString &text, bool alarm)
{
    if (m_items.size() >= m_max) {
        beginRemoveRows(QModelIndex(), 0, 0);
        m_items.removeFirst();
        endRemoveRows();
    }
    const int row = m_items.size();
    beginInsertRows(QModelIndex(), row, row);
    m_items.append({QTime::currentTime().toString(QStringLiteral("HH:mm:ss")), code, text, alarm});
    endInsertRows();
    emit countChanged();
}

void EventLog::clear()
{
    beginResetModel();
    m_items.clear();
    endResetModel();
    emit countChanged();
}
