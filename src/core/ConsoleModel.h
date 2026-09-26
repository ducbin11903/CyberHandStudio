#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QVector>

// Console lines: received text, sent commands, app info/errors.
class ConsoleModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool showTelemetry READ showTelemetry WRITE setShowTelemetry NOTIFY showTelemetryChanged)
    Q_PROPERTY(bool showStreamed READ showStreamed WRITE setShowStreamed NOTIFY showStreamedChanged)
    Q_PROPERTY(int maxLines READ maxLines WRITE setMaxLines NOTIFY maxLinesChanged)
    Q_PROPERTY(qint64 hiddenStreamed READ hiddenStreamed NOTIFY hiddenStreamedChanged)

public:
    enum Kind { Rx = 0, Tx = 1, Info = 2, Error = 3, Telemetry = 4 };
    Q_ENUM(Kind)
    enum Roles { TimeRole = Qt::UserRole + 1, TextRole, KindRole };

    explicit ConsoleModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return m_lines.size(); }
    bool showTelemetry() const { return m_showTelemetry; }
    void setShowTelemetry(bool s);
    bool showStreamed() const { return m_showStreamed; }
    void setShowStreamed(bool s);
    int maxLines() const { return m_max; }
    void setMaxLines(int m);
    qint64 hiddenStreamed() const { return m_hiddenStreamed; }

    Q_INVOKABLE void append(int kind, const QString &text);
    Q_INVOKABLE void info(const QString &text) { append(Info, text); }
    Q_INVOKABLE void error(const QString &text) { append(Error, text); }
    Q_INVOKABLE void clear();
    Q_INVOKABLE void copyAll() const;
    Q_INVOKABLE bool saveToFile(const QString &pathOrUrl) const;
    // Latest Rx text line that contains `needle` since `sinceSerial` (for macros).
    qint64 serial() const { return m_serial; }

public slots:
    void onRx(const QString &line) { append(Rx, line); }
    void onTelemetryLine(const QString &line);
    void onSent(const QString &line, bool quiet);

signals:
    void countChanged();
    void showTelemetryChanged();
    void showStreamedChanged();
    void maxLinesChanged();
    void hiddenStreamedChanged();
    void lineAdded(int kind, const QString &text);

private:
    struct Line {
        QString time;
        QString text;
        int kind;
    };
    QVector<Line> m_lines;
    bool m_showTelemetry = false;
    bool m_showStreamed = false;
    int m_max = 3000;
    qint64 m_hiddenStreamed = 0;
    qint64 m_serial = 0;
};
