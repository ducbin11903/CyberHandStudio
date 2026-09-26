#include "LiveSeries.h"

LiveSeries::LiveSeries(QObject *parent)
    : PlotSource(parent)
{
}

int LiveSeries::pointCount(int ch) const
{
    if (ch < 0 || ch >= m_rings.size())
        return 0;
    return m_rings[ch].count;
}

QPointF LiveSeries::point(int ch, int i) const
{
    const Ring &r = m_rings[ch];
    return r.buf[(r.start + i) % r.buf.size()];
}

void LiveSeries::setPaused(bool p)
{
    if (p == m_paused)
        return;
    m_paused = p;
    emit pausedChanged();
}

void LiveSeries::setCapacity(int c)
{
    c = qBound(100, c, 1000000);
    if (c == m_capacity)
        return;
    m_capacity = c;
    clear();
    emit capacityChanged();
}

double LiveSeries::lastValue(const QString &name) const
{
    const int ch = m_names.indexOf(name);
    if (ch < 0 || m_rings[ch].count == 0)
        return qQNaN();
    return point(ch, m_rings[ch].count - 1).y();
}

void LiveSeries::clear()
{
    for (Ring &r : m_rings) {
        r.start = 0;
        r.count = 0;
    }
    m_latest = 0;
    emit dataChanged();
}

int LiveSeries::ensureChannel(const QString &name)
{
    int ch = m_names.indexOf(name);
    if (ch >= 0)
        return ch;
    m_names.append(name);
    Ring r;
    r.buf.resize(m_capacity);
    m_rings.append(r);
    emit channelsChanged();
    return m_names.size() - 1;
}

void LiveSeries::addSample(double t, const QStringList &names, const QVector<double> &values)
{
    if (m_paused)
        return;
    const int n = qMin(names.size(), values.size());
    for (int k = 0; k < n; ++k) {
        const int ch = ensureChannel(names[k]);
        Ring &r = m_rings[ch];
        const int cap = r.buf.size();
        if (r.count < cap) {
            r.buf[(r.start + r.count) % cap] = QPointF(t, values[k]);
            ++r.count;
        } else {
            r.buf[r.start] = QPointF(t, values[k]);
            r.start = (r.start + 1) % cap;
        }
    }
    m_latest = t;
    emit dataChanged();
}
