#pragma once

#include <QAbstractListModel>
#include <QJsonArray>
#include <QVector>

class HandModel;

// Named hand poses (6 joint angles, degrees).
class PoseLibrary : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(QStringList names READ names NOTIFY countChanged)

public:
    enum Roles { NameRole = Qt::UserRole + 1, ValuesRole };

    explicit PoseLibrary(HandModel *hand, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return m_poses.size(); }
    QStringList names() const;

    Q_INVOKABLE int add(const QString &name, const QVariantList &values);
    Q_INVOKABLE int capture(const QString &name);
    Q_INVOKABLE void remove(int row);
    Q_INVOKABLE void rename(int row, const QString &name);
    Q_INVOKABLE int find(const QString &name) const;
    Q_INVOKABLE QVariantList values(int row) const;
    Q_INVOKABLE void apply(int row);
    Q_INVOKABLE void applyByName(const QString &name) { apply(find(name)); }
    void resetDefaults();

    QJsonArray toJson() const;
    void fromJson(const QJsonArray &a);

signals:
    void countChanged();
    void applyRequested(const QVariantList &values, const QString &name);

private:
    struct Pose {
        QString name;
        QVector<double> q;
    };
    HandModel *m_hand;
    QVector<Pose> m_poses;
};
