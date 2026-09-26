#include "ConfigStore.h"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSettings>
#include <QUrl>

ConfigStore::ConfigStore(const QString &folder, QObject *parent)
    : QObject(parent), m_folder(folder)
{
    QDir().mkpath(m_folder);
    refresh();
}

QString ConfigStore::sanitize(const QString &name)
{
    QString s = name.trimmed();
    static const QString bad = QStringLiteral("\\/:*?\"<>|");
    for (QChar &c : s)
        if (bad.contains(c))
            c = QLatin1Char('_');
    return s;
}

QString ConfigStore::pathOf(const QString &name) const
{
    return QDir(m_folder).filePath(sanitize(name) + QStringLiteral(".json"));
}

void ConfigStore::setMessage(const QString &m)
{
    m_msg = m;
    emit lastMessageChanged();
}

void ConfigStore::refresh()
{
    QStringList l;
    const QFileInfoList files = QDir(m_folder).entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    for (const QFileInfo &fi : files)
        l << fi.completeBaseName();
    if (l != m_profiles) {
        m_profiles = l;
        emit profilesChanged();
    }
}

bool ConfigStore::load(const QString &name)
{
    QFile f(pathOf(name));
    if (!f.open(QIODevice::ReadOnly)) {
        setMessage(tr("Không mở được cấu hình %1").arg(name));
        return false;
    }
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (doc.isNull()) {
        setMessage(tr("Cấu hình %1 lỗi JSON: %2").arg(name, err.errorString()));
        return false;
    }
    if (m_apply)
        m_apply(doc.object());
    m_current = sanitize(name);
    QSettings().setValue(QStringLiteral("lastProfile"), m_current);
    emit currentChanged();
    setMessage(tr("Đã nạp %1").arg(m_current));
    emit loaded();
    return true;
}

bool ConfigStore::save()
{
    if (m_current.isEmpty())
        m_current = QStringLiteral("Default");
    return saveAs(m_current);
}

bool ConfigStore::saveAs(const QString &name)
{
    const QString n = sanitize(name);
    if (n.isEmpty() || !m_collect)
        return false;
    QJsonObject o = m_collect();
    o.insert(QStringLiteral("format"), QStringLiteral("cyberhand-config-1"));
    QFile f(pathOf(n));
    if (!f.open(QIODevice::WriteOnly)) {
        setMessage(tr("Không ghi được %1").arg(f.fileName()));
        return false;
    }
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    f.close();
    const bool changed = n != m_current;
    m_current = n;
    QSettings().setValue(QStringLiteral("lastProfile"), m_current);
    refresh();
    if (changed)
        emit currentChanged();
    setMessage(tr("Đã lưu %1").arg(n));
    return true;
}

bool ConfigStore::create(const QString &name)
{
    const QString n = sanitize(name);
    if (n.isEmpty())
        return false;
    // a new profile starts from the current settings
    return saveAs(n);
}

bool ConfigStore::remove(const QString &name)
{
    const bool ok = QFile::remove(pathOf(name));
    refresh();
    if (ok && sanitize(name) == m_current) {
        m_current.clear();
        emit currentChanged();
    }
    return ok;
}

void ConfigStore::openInEditor(const QString &name) const
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(pathOf(name.isEmpty() ? m_current : name)));
}

void ConfigStore::openFolder() const
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(m_folder));
}

void ConfigStore::loadLastOrDefault()
{
    const QString last = QSettings().value(QStringLiteral("lastProfile")).toString();
    if (!last.isEmpty() && m_profiles.contains(last) && load(last))
        return;
    if (m_profiles.contains(QStringLiteral("Default")) && load(QStringLiteral("Default")))
        return;
    saveAs(QStringLiteral("Default"));
}
