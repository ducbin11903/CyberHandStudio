#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QVector>

// Console lines: received text, sent commands, app info/errors.
// Each line can carry a source tag (the MCU: U6 / U7) for 2 ports or the fw 0.8 link;
// sourceFilter hides the lines of the other MCU (untagged app lines always show).
class ConsoleModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool showTelemetry READ showTelemetry WRITE setShowTelemetry NOTIFY showTelemetryChanged)
    Q_PROPERTY(bool showStreamed READ showStreamed WRITE setShowStreamed NOTIFY showStreamedChanged)
    Q_PROPERTY(int maxLines READ maxLines WRITE setMaxLines NOTIFY maxLinesChanged)
    Q_PROPERTY(qint64 hiddenStreamed READ hiddenStreamed NOTIFY hiddenStreamedChanged)
    Q_PROPERTY(QString sourceFilter READ sourceFilter WRITE setSourceFilter NOTIFY sourceFilterChanged)
    Q_PROPERTY(QStringList sources READ sources NOTIFY sourcesChanged)

public:
    enum Kind { Rx = 0, Tx = 1, Info = 2, Error = 3, Telemetry = 4 };
    Q_ENUM(Kind)
    enum Roles { TimeRole = Qt::UserRole + 1, TextRole, KindRole, SrcRole };

    explicit ConsoleModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return m_rows.size(); }
    bool showTelemetry() const { return m_showTelemetry; }
    void setShowTelemetry(bool s);
    bool showStreamed() const { return m_showStreamed; }
    void setShowStreamed(bool s);
    int maxLines() const { return m_max; }
    void setMaxLines(int m);
    qint64 hiddenStreamed() const { return m_hiddenStreamed; }
    QString sourceFilter() const { return m_filter; }
    void setSourceFilter(const QString &f);
    QStringList sources() const { return m_sources; }

    Q_INVOKABLE void append(int kind, const QString &text, const QString &src = QString());
    Q_INVOKABLE void info(const QString &text) { append(Info, text); }
    Q_INVOKABLE void error(const QString &text) { append(Error, text); }
    Q_INVOKABLE void clear();
    Q_INVOKABLE void copyAll() const;
    Q_INVOKABLE bool saveToFile(const QString &pathOrUrl) const;
    // Latest Rx text line that contains `needle` since `sinceSerial` (for macros).
    qint64 serial() const { return m_serial; }

public slots:
    void onRx(const QString &line, const QString &src = QString()) { append(Rx, line, src); }
    void onTelemetryLine(const QString &line, const QString &src = QString());
    void onSent(const QString &line, bool quiet, const QString &src = QString());

signals:
    void countChanged();
    void showTelemetryChanged();
    void showStreamedChanged();
    void maxLinesChanged();
    void hiddenStreamedChanged();
    void sourceFilterChanged();
    void sourcesChanged();
    void lineAdded(int kind, const QString &text);

private:
    struct Line {
        QString time;
        QString text;
        int kind;
        QString src;
    };
    bool matches(const Line &l) const { return m_filter.isEmpty() || l.src.isEmpty() || l.src == m_filter; }
    void rebuildRows();
    QVector<Line> m_lines;
    QVector<int> m_rows; // indices of m_lines that pass the filter
    QString m_filter;
    QStringList m_sources;
    bool m_showTelemetry = false;
    bool m_showStreamed = false;
    int m_max = 3000;
    qint64 m_hiddenStreamed = 0;
    qint64 m_serial = 0;
};
