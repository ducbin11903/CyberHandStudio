#pragma once

#include <QJsonObject>
#include <QObject>
#include <QStringList>
#include <functional>

// Config profiles stored as JSON files (one per profile) in a folder.
class ConfigStore : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QStringList profiles READ profiles NOTIFY profilesChanged)
    Q_PROPERTY(QString current READ current NOTIFY currentChanged)
    Q_PROPERTY(QString folder READ folder CONSTANT)
    Q_PROPERTY(QString lastMessage READ lastMessage NOTIFY lastMessageChanged)

public:
    explicit ConfigStore(const QString &folder, QObject *parent = nullptr);

    QStringList profiles() const { return m_profiles; }
    QString current() const { return m_current; }
    QString folder() const { return m_folder; }
    QString lastMessage() const { return m_msg; }

    void setCollector(std::function<QJsonObject()> f) { m_collect = std::move(f); }
    void setApplier(std::function<void(const QJsonObject &)> f) { m_apply = std::move(f); }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE bool load(const QString &name);
    Q_INVOKABLE bool save();
    Q_INVOKABLE bool saveAs(const QString &name);
    Q_INVOKABLE bool create(const QString &name);
    Q_INVOKABLE bool remove(const QString &name);
    Q_INVOKABLE bool reload() { return load(m_current); }
    Q_INVOKABLE void openInEditor(const QString &name) const;
    Q_INVOKABLE void openFolder() const;
    Q_INVOKABLE QString pathOf(const QString &name) const;

    void loadLastOrDefault();

signals:
    void profilesChanged();
    void currentChanged();
    void lastMessageChanged();
    void loaded();

private:
    void setMessage(const QString &m);
    static QString sanitize(const QString &name);

    QString m_folder;
    QStringList m_profiles;
    QString m_current;
    QString m_msg;
    std::function<QJsonObject()> m_collect;
    std::function<void(const QJsonObject &)> m_apply;
};
