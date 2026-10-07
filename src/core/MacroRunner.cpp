#include "MacroRunner.h"

#include "ConsoleModel.h"
#include "HandController.h"
#include "PoseLibrary.h"

#include <QJsonObject>
#include <QRegularExpression>

MacroRunner::MacroRunner(HandController *ctrl, ConsoleModel *console, PoseLibrary *poses, QObject *parent)
    : QAbstractListModel(parent), m_ctrl(ctrl), m_console(console), m_poses(poses)
{
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &MacroRunner::next);
    resetDefaults();
}

int MacroRunner::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_macros.size();
}

QVariant MacroRunner::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_macros.size())
        return {};
    switch (role) {
    case NameRole:
    case Qt::DisplayRole: return m_macros[index.row()].name;
    case ScriptRole: return m_macros[index.row()].script;
    }
    return {};
}

QHash<int, QByteArray> MacroRunner::roleNames() const
{
    return {{NameRole, "name"}, {ScriptRole, "script"}};
}

int MacroRunner::add(const QString &name, const QString &script)
{
    beginInsertRows(QModelIndex(), m_macros.size(), m_macros.size());
    m_macros.append({name.trimmed().isEmpty() ? QStringLiteral("Macro %1").arg(m_macros.size() + 1) : name.trimmed(), script});
    endInsertRows();
    emit countChanged();
    return m_macros.size() - 1;
}

void MacroRunner::update(int row, const QString &name, const QString &script)
{
    if (row < 0 || row >= m_macros.size())
        return;
    if (!name.trimmed().isEmpty())
        m_macros[row].name = name.trimmed();
    m_macros[row].script = script;
    emit dataChanged(index(row), index(row));
}

void MacroRunner::remove(int row)
{
    if (row < 0 || row >= m_macros.size())
        return;
    beginRemoveRows(QModelIndex(), row, row);
    m_macros.remove(row);
    endRemoveRows();
    emit countChanged();
}

QString MacroRunner::script(int row) const
{
    return (row >= 0 && row < m_macros.size()) ? m_macros[row].script : QString();
}

QString MacroRunner::name(int row) const
{
    return (row >= 0 && row < m_macros.size()) ? m_macros[row].name : QString();
}

bool MacroRunner::run(int row)
{
    if (row < 0 || row >= m_macros.size())
        return false;
    return runText(m_macros[row].script, m_macros[row].name);
}

bool MacroRunner::runText(const QString &text, const QString &name)
{
    if (m_running)
        stop();
    m_lines = text.split(QLatin1Char('\n'));
    m_pc = 0;
    m_stack.clear();
    m_running = true;
    m_runningName = name;
    emit runningChanged();
    m_console->info(tr("Macro \"%1\" bắt đầu").arg(name));
    setStatus(tr("Đang chạy"));
    m_timer.start(0);
    return true;
}

void MacroRunner::stop()
{
    if (!m_running)
        return;
    finish(tr("Đã dừng"));
}

void MacroRunner::finish(const QString &why)
{
    m_timer.stop();
    if (m_waitConn)
        disconnect(m_waitConn);
    m_waitFor.clear();
    m_running = false;
    setStatus(why);
    m_console->info(tr("Macro \"%1\": %2").arg(m_runningName, why));
    emit runningChanged();
}

void MacroRunner::setStatus(const QString &s)
{
    m_status = s;
    emit statusChanged();
}

int MacroRunner::findEnd(int from) const
{
    int depth = 0;
    for (int i = from; i < m_lines.size(); ++i) {
        const QString t = m_lines[i].trimmed().toLower();
        if (t.startsWith(QLatin1String("@repeat")))
            ++depth;
        else if (t == QLatin1String("@end")) {
            if (depth == 0)
                return i;
            --depth;
        }
    }
    return -1;
}

void MacroRunner::next()
{
    if (!m_running)
        return;
    while (m_pc < m_lines.size()) {
        const int lineNo = m_pc;
        const QString line = m_lines[m_pc].trimmed();
        ++m_pc;
        emit statusChanged();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;

        // "@U7 s" is a command for one MCU (fw 0.8 link / 2 ports), not a macro directive
        static const QRegularExpression reMcu(QStringLiteral("^@U\\d+\\s"), QRegularExpression::CaseInsensitiveOption);
        if (!line.startsWith(QLatin1Char('@')) || reMcu.match(line).hasMatch()) {
            if (!m_ctrl->send(line)) {
                finish(tr("Lỗi ở dòng %1 (chưa kết nối?)").arg(lineNo + 1));
                return;
            }
            m_timer.start(m_stepDelay);
            return;
        }

        const QStringList parts = line.mid(1).split(QLatin1Char(' '), Qt::SkipEmptyParts);
        const QString cmd = parts.value(0).toLower();
        if (cmd == QLatin1String("wait")) {
            m_timer.start(qMax(0, parts.value(1).toInt()));
            setStatus(tr("Chờ %1 ms").arg(parts.value(1)));
            return;
        }
        if (cmd == QLatin1String("waitfor")) {
            QString needle = line.mid(1 + 7).trimmed();
            int timeout = 30000;
            const int sp = needle.lastIndexOf(QLatin1Char(' '));
            bool ok = false;
            if (sp > 0) {
                const int t = needle.mid(sp + 1).toInt(&ok);
                if (ok) {
                    timeout = t;
                    needle = needle.left(sp).trimmed();
                }
            }
            m_waitFor = needle;
            m_waitTimeout = timeout;
            m_waitClock.start();
            setStatus(tr("Chờ \"%1\"").arg(needle));
            m_waitConn = connect(m_console, &ConsoleModel::lineAdded, this, [this](int kind, const QString &text) {
                if (kind == ConsoleModel::Rx && text.contains(m_waitFor, Qt::CaseInsensitive)) {
                    disconnect(m_waitConn);
                    m_waitFor.clear();
                    m_timer.start(m_stepDelay);
                }
            });
            QTimer::singleShot(timeout, this, [this, lineNo]() {
                if (m_running && !m_waitFor.isEmpty() && m_pc == lineNo + 1)
                    finish(tr("Quá thời gian chờ ở dòng %1").arg(lineNo + 1));
            });
            return;
        }
        if (cmd == QLatin1String("repeat")) {
            const int n = qMax(1, parts.value(1).toInt());
            const int end = findEnd(m_pc);
            if (end < 0) {
                finish(tr("Thiếu @end cho @repeat ở dòng %1").arg(lineNo + 1));
                return;
            }
            m_stack.append({m_pc, n});
            continue;
        }
        if (cmd == QLatin1String("end")) {
            if (m_stack.isEmpty())
                continue;
            Frame &f = m_stack.last();
            --f.remaining;
            if (f.remaining > 0) {
                m_pc = f.start;
                m_console->info(tr("Lặp còn %1").arg(f.remaining));
            } else {
                m_stack.removeLast();
            }
            continue;
        }
        if (cmd == QLatin1String("pose")) {
            const QString name = line.mid(1 + 4).trimmed();
            const int idx = m_poses->find(name);
            if (idx < 0) {
                finish(tr("Không có pose \"%1\"").arg(name));
                return;
            }
            m_poses->apply(idx);
            m_timer.start(m_stepDelay);
            return;
        }
        if (cmd == QLatin1String("log")) {
            m_console->info(line.mid(1 + 3).trimmed());
            continue;
        }
        finish(tr("Lệnh lạ ở dòng %1: %2").arg(lineNo + 1).arg(line));
        return;
    }
    finish(tr("Xong"));
}

void MacroRunner::resetDefaults()
{
    beginResetModel();
    m_macros.clear();
    m_macros.append({QStringLiteral("Home + gập/duỗi 20 lần"),
                     QStringLiteral("# Bench 1 ngón: home rồi gập 0 -> 90 -> 0, lặp 20 lần\n"
                                    "e\nH\n@waitfor HOME: 20000\n@repeat 20\ng 90\n@wait 2500\ng 0\n@wait 2500\n@end\nd\n")});
    m_macros.append({QStringLiteral("Home → 45° → 0 → tắt"),
                     QStringLiteral("e\nH\n@waitfor HOME: 20000\ng 45\n@wait 2000\ng 0\n@wait 2000\nd\n")});
    m_macros.append({QStringLiteral("Quét tốc độ log"),
                     QStringLiteral("l 5\n@wait 2000\nl 20\n@wait 2000\nl 50\n@wait 2000\nl 100\n@wait 2000\nl 20\n")});
    endResetModel();
    emit countChanged();
}

QJsonArray MacroRunner::toJson() const
{
    QJsonArray a;
    for (const Macro &m : m_macros)
        a.append(QJsonObject{{QStringLiteral("name"), m.name}, {QStringLiteral("script"), m.script}});
    return a;
}

void MacroRunner::fromJson(const QJsonArray &a)
{
    if (a.isEmpty())
        return;
    beginResetModel();
    m_macros.clear();
    for (const QJsonValue &v : a) {
        const QJsonObject o = v.toObject();
        m_macros.append({o.value(QStringLiteral("name")).toString(), o.value(QStringLiteral("script")).toString()});
    }
    endResetModel();
    emit countChanged();
}
