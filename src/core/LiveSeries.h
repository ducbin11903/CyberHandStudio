#pragma once

#include <QHash>
#include <QStringList>
#include <QVector>

#include "PlotSource.h"

// Ring buffers of live telemetry, one per channel name.
class LiveSeries : public PlotSource
{
    Q_OBJECT
    Q_PROPERTY(QStringList channelNames READ channelNames NOTIFY channelsChanged)
    Q_PROPERTY(double latestTime READ latestTime NOTIFY dataChanged)
    Q_PROPERTY(bool paused READ paused WRITE setPaused NOTIFY pausedChanged)
    Q_PROPERTY(int capacity READ capacity WRITE setCapacity NOTIFY capacityChanged)

public:
    explicit LiveSeries(QObject *parent = nullptr);

    int channelCount() const override { return m_names.size(); }
    QString channelName(int ch) const override { return m_names.value(ch); }
    int pointCount(int ch) const override;
    QPointF point(int ch, int i) const override;

    QStringList channelNames() const { return m_names; }
    double latestTime() const { return m_latest; }
    bool paused() const { return m_paused; }
    void setPaused(bool p);
    int capacity() const { return m_capacity; }
    void setCapacity(int c);

    Q_INVOKABLE int indexOf(const QString &name) const { return m_names.indexOf(name); }
    Q_INVOKABLE double lastValue(const QString &name) const;
    Q_INVOKABLE void clear();

public slots:
    void addSample(double t, const QStringList &names, const QVector<double> &values);

signals:
    void pausedChanged();
    void capacityChanged();

private:
    struct Ring {
        QVector<QPointF> buf;
        int start = 0;
        int count = 0;
    };
    int ensureChannel(const QString &name);

    QStringList m_names;
    QVector<Ring> m_rings;
    double m_latest = 0;
    bool m_paused = false;
    int m_capacity = 30000;
};
