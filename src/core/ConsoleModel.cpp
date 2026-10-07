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
    return parent.isValid() ? 0 : m_rows.size();
}

QVariant ConsoleModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size())
        return {};
    const Line &l = m_lines[m_rows[index.row()]];
    switch (role) {
    case TimeRole: return l.time;
    case TextRole:
    case Qt::DisplayRole: return l.text;
    case KindRole: return l.kind;
    case SrcRole: return l.src;
    }
    return {};
}

QHash<int, QByteArray> ConsoleModel::roleNames() const
{
    return {{TimeRole, "time"}, {TextRole, "text"}, {KindRole, "kind"}, {SrcRole, "src"}};
}

void ConsoleModel::setSourceFilter(const QString &f)
{
    if (f == m_filter)
        return;
    m_filter = f;
    beginResetModel();
    rebuildRows();
    endResetModel();
    emit sourceFilterChanged();
    emit countChanged();
}

void ConsoleModel::rebuildRows()
{
    m_rows.clear();
    for (int i = 0; i < m_lines.size(); ++i)
        if (matches(m_lines[i]))
            m_rows.append(i);
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

void ConsoleModel::append(int kind, const QString &text, const QString &src)
{
    ++m_serial;
    emit lineAdded(kind, text);
    if (m_lines.size() >= m_max) {
        const int drop = m_lines.size() - m_max + 1 + m_max / 10;
        int rows = 0;
        while (rows < m_rows.size() && m_rows[rows] < drop)
            ++rows;
        if (rows > 0) {
            beginRemoveRows(QModelIndex(), 0, rows - 1);
            m_rows.remove(0, rows);
            endRemoveRows();
        }
        m_lines.remove(0, drop);
        for (int &r : m_rows)
            r -= drop;
    }
    const Line l{QTime::currentTime().toString(QStringLiteral("HH:mm:ss.zzz")), text, kind, src};
    m_lines.append(l);
    if (matches(l)) {
        const int row = m_rows.size();
        beginInsertRows(QModelIndex(), row, row);
        m_rows.append(m_lines.size() - 1);
        endInsertRows();
        emit countChanged();
    }
    if (!src.isEmpty() && !m_sources.contains(src)) {
        m_sources.append(src);
        m_sources.sort();
        emit sourcesChanged();
    }
}

void ConsoleModel::onTelemetryLine(const QString &line, const QString &src)
{
    if (m_showTelemetry)
        append(Telemetry, line, src);
}

void ConsoleModel::onSent(const QString &line, bool quiet, const QString &src)
{
    if (quiet && !m_showStreamed) {
        ++m_hiddenStreamed;
        emit hiddenStreamedChanged();
        return;
    }
    append(Tx, line, src);
}

void ConsoleModel::clear()
{
    beginResetModel();
    m_lines.clear();
    m_rows.clear();
    endResetModel();
    if (!m_sources.isEmpty()) {
        m_sources.clear();
        emit sourcesChanged();
    }
    m_hiddenStreamed = 0;
    emit hiddenStreamedChanged();
    emit countChanged();
}

void ConsoleModel::copyAll() const
{
    QString all;
    for (int r : m_rows) {
        const Line &l = m_lines[r];
        all += l.time + QLatin1Char(' ') + (l.src.isEmpty() ? QString() : QLatin1Char('[') + l.src + QStringLiteral("] ")) + l.text + QLatin1Char('\n');
    }
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
    for (int r : m_rows) {
        const Line &l = m_lines[r];
        ts << l.time << ' ' << tags[qBound(0, l.kind, 4)] << ' ';
        if (!l.src.isEmpty())
            ts << '[' << l.src << "] ";
        ts << l.text << '\n';
    }
    return true;
}
