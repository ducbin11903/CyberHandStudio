#pragma once

#include <QObject>
#include <QPointF>
#include <QString>

// Abstract data source for PlotItem: N channels of (x, y) points, x ascending.
class PlotSource : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;

    virtual int channelCount() const = 0;
    virtual QString channelName(int ch) const = 0;
    virtual int pointCount(int ch) const = 0;
    virtual QPointF point(int ch, int i) const = 0;

    // Index of the first point with x >= value (binary search on ascending x).
    int lowerBound(int ch, double x) const
    {
        int lo = 0, hi = pointCount(ch);
        while (lo < hi) {
            const int mid = (lo + hi) / 2;
            if (point(ch, mid).x() < x)
                lo = mid + 1;
            else
                hi = mid;
        }
        return lo;
    }

signals:
    void dataChanged();
    void channelsChanged();
};
