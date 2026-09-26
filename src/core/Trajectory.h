#pragma once

#include <QStringList>
#include <QVector>

#include "PlotSource.h"

class HandModel;

// Joint trajectory: time (s) + 6 joint angles (deg). NaN = joint not controlled.
// CSV: header "t,T2,T1,I,M,R,P" (or q1..q6), degrees; "# unit=percent" for 0..100 of each joint range.
class Trajectory : public PlotSource
{
    Q_OBJECT
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged)
    Q_PROPERTY(QString filePath READ filePath NOTIFY nameChanged)
    Q_PROPERTY(double duration READ duration NOTIFY dataChanged)
    Q_PROPERTY(int sampleCount READ sampleCount NOTIFY dataChanged)
    Q_PROPERTY(bool empty READ empty NOTIFY dataChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(QStringList channelNames READ channelNames CONSTANT)

public:
    explicit Trajectory(HandModel *hand, QObject *parent = nullptr);

    int channelCount() const override { return m_values.size(); }
    QString channelName(int ch) const override { return m_names.value(ch); }
    int pointCount(int ch) const override;
    QPointF point(int ch, int i) const override;

    QString name() const { return m_name; }
    void setName(const QString &n);
    QString filePath() const { return m_path; }
    double duration() const { return m_times.isEmpty() ? 0.0 : m_times.last(); }
    int sampleCount() const { return m_times.size(); }
    bool empty() const { return m_times.isEmpty(); }
    QString lastError() const { return m_error; }
    QStringList channelNames() const { return m_names; }

    QVector<double> sample(double t) const;
    Q_INVOKABLE QVariantList sampleList(double t) const;
    void setData(const QVector<double> &times, const QVector<QVector<double>> &values);

    Q_INVOKABLE bool loadCsv(const QString &pathOrUrl);
    Q_INVOKABLE bool saveCsv(const QString &pathOrUrl);
    Q_INVOKABLE bool fromCsvText(const QString &text);
    Q_INVOKABLE QString toCsvText() const;
    Q_INVOKABLE void clear();
    Q_INVOKABLE void makeDemo(int kind);
    Q_INVOKABLE static QStringList demoNames();

    static QString localPath(const QString &pathOrUrl);

signals:
    void nameChanged();
    void lastErrorChanged();

private:
    void setError(const QString &e);

    HandModel *m_hand;
    QStringList m_names;
    QVector<double> m_times;
    QVector<QVector<double>> m_values; // [joint][sample]
    QString m_name;
    QString m_path;
    QString m_error;
};
