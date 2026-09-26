#include "ConsoleModel.h"

#include <QClipboard>
#include <QFile>
#include <QGuiApplication>
#include <QTextStream>
#include <QUrl>

ConsoleModel::ConsoleModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int ConsoleModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_lines.size();
}

QVariant ConsoleModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_lines.size())
        return {};
    const Line &l = m_lines[index.row()];
    switch (role) {
    case TimeRole: return l.time;
    case TextRole:
    case Qt::DisplayRole: return l.text;
    case KindRole: return l.kind;
    }
    return {};
}

QHash<int, QByteArray> ConsoleModel::roleNames() const
{
    return {{TimeRole, "time"}, {TextRole, "text"}, {KindRole, "kind"}};
}

void ConsoleModel::setShowTelemetry(bool s)
{
    if (s == m_showTelemetry)
        return;
    m_showTelemetry = s;
    emit showTelemetryChanged();
}

void ConsoleModel::setShowStreamed(bool s)
{
    if (s == m_showStreamed)
        return;
    m_showStreamed = s;
    emit showStreamedChanged();
}

void ConsoleModel::setMaxLines(int m)
{
    m = qBound(100, m, 100000);
    if (m == m_max)
        return;
    m_max = m;
    emit maxLinesChanged();
}

void ConsoleModel::append(int kind, const QString &text)
{
    ++m_serial;
    emit lineAdded(kind, text);
    if (m_lines.size() >= m_max) {
        const int drop = m_lines.size() - m_max + 1 + m_max / 10;
        beginRemoveRows(QModelIndex(), 0, drop - 1);
        m_lines.remove(0, drop);
        endRemoveRows();
    }
    const int row = m_lines.size();
    beginInsertRows(QModelIndex(), row, row);
    m_lines.append({QTime::currentTime().toString(QStringLiteral("HH:mm:ss.zzz")), text, kind});
    endInsertRows();
    emit countChanged();
}

void ConsoleModel::onTelemetryLine(const QString &line)
{
    if (m_showTelemetry)
        append(Telemetry, line);
}

void ConsoleModel::onSent(const QString &line, bool quiet)
{
    if (quiet && !m_showStreamed) {
        ++m_hiddenStreamed;
        emit hiddenStreamedChanged();
        return;
    }
    append(Tx, line);
}

void ConsoleModel::clear()
{
    beginResetModel();
    m_lines.clear();
    endResetModel();
    m_hiddenStreamed = 0;
    emit hiddenStreamedChanged();
    emit countChanged();
}

void ConsoleModel::copyAll() const
{
    QString all;
    for (const Line &l : m_lines)
        all += l.time + QLatin1Char(' ') + l.text + QLatin1Char('\n');
    if (auto *cb = QGuiApplication::clipboard())
        cb->setText(all);
}

bool ConsoleModel::saveToFile(const QString &pathOrUrl) const
{
    const QUrl url(pathOrUrl);
    const QString path = url.isLocalFile() ? url.toLocalFile() : pathOrUrl;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    QTextStream ts(&f);
    static const char *tags[] = {"<", ">", "i", "!", "~"};
    for (const Line &l : m_lines)
        ts << l.time << ' ' << tags[qBound(0, l.kind, 4)] << ' ' << l.text << '\n';
    return true;
}
