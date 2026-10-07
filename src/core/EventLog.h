#pragma once

#include <QAbstractListModel>
#include <QVector>

// Events with time: over-current reactions (RETR / HOLD / TRIP / WARN), LINK, OTEMP, VBAT.
class EventLog : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Roles { TimeRole = Qt::UserRole + 1, CodeRole, TextRole, AlarmRole };

    explicit EventLog(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return m_items.size(); }
    // alarm = red (motors off / refused), else info
    Q_INVOKABLE void add(const QString &code, const QString &text, bool alarm = true);
    Q_INVOKABLE void clear();
    Q_INVOKABLE QString lastText() const { return m_items.isEmpty() ? QString() : m_items.last().text; }

signals:
    void countChanged();

private:
    struct Item {
        QString time;
        QString code;
        QString text;
        bool alarm;
    };
    QVector<Item> m_items;
    int m_max = 500;
};
