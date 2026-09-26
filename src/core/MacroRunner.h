#pragma once

#include <QAbstractListModel>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QTimer>
#include <QVector>

class HandController;
class ConsoleModel;
class PoseLibrary;

// Saved command scripts. Each line is a CLI command, or a directive:
//   @wait <ms>             pause
//   @waitfor <text> [ms]   wait until the firmware prints a line containing <text>
//   @repeat <N> ... @end   loop (nestable)
//   @pose <name>           move to a saved pose
//   @log <text>            print to the console
// Lines starting with # are comments.
class MacroRunner : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(int currentLine READ currentLine NOTIFY statusChanged)
    Q_PROPERTY(QString runningName READ runningName NOTIFY runningChanged)
    Q_PROPERTY(int stepDelayMs READ stepDelayMs WRITE setStepDelayMs NOTIFY settingsChanged)

public:
    enum Roles { NameRole = Qt::UserRole + 1, ScriptRole };

    MacroRunner(HandController *ctrl, ConsoleModel *console, PoseLibrary *poses, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return m_macros.size(); }
    bool running() const { return m_running; }
    QString status() const { return m_status; }
    int currentLine() const { return m_pc; }
    QString runningName() const { return m_runningName; }
    int stepDelayMs() const { return m_stepDelay; }
    void setStepDelayMs(int d) { m_stepDelay = qBound(0, d, 5000); emit settingsChanged(); }

    Q_INVOKABLE int add(const QString &name, const QString &script);
    Q_INVOKABLE void update(int row, const QString &name, const QString &script);
    Q_INVOKABLE void remove(int row);
    Q_INVOKABLE QString script(int row) const;
    Q_INVOKABLE QString name(int row) const;
    Q_INVOKABLE bool run(int row);
    Q_INVOKABLE bool runText(const QString &text, const QString &name = QStringLiteral("script"));
    Q_INVOKABLE void stop();
    void resetDefaults();

    QJsonArray toJson() const;
    void fromJson(const QJsonArray &a);

signals:
    void countChanged();
    void runningChanged();
    void statusChanged();
    void settingsChanged();

private:
    struct Macro {
        QString name;
        QString script;
    };
    struct Frame {
        int start;   // first line of the loop body
        int remaining;
    };
    void next();
    void setStatus(const QString &s);
    void finish(const QString &why);
    int findEnd(int from) const;

    HandController *m_ctrl;
    ConsoleModel *m_console;
    PoseLibrary *m_poses;
    QVector<Macro> m_macros;
    QStringList m_lines;
    int m_pc = 0;
    QVector<Frame> m_stack;
    bool m_running = false;
    QString m_status;
    QString m_runningName;
    QTimer m_timer;
    int m_stepDelay = 30;
    QString m_waitFor;
    QElapsedTimer m_waitClock;
    int m_waitTimeout = 0;
    QMetaObject::Connection m_waitConn;
};
