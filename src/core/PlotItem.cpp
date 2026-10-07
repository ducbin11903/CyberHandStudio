#include "PlotItem.h"

#include <QPainter>
#include <QSet>
#include <QPainterPath>
#include <cmath>

static const QColor kFallback[] = {
    QColor("#9E6362"), QColor("#A8886C"), QColor("#A9AB6B"), QColor("#67A585"),
    QColor("#6383A1"), QColor("#8466A8"), QColor("#A5617F"), QColor("#5FA3A0"),
    QColor("#9C8F5E"), QColor("#7B9A5A"),
};

static QSet<PlotItem *> &allPlots()
{
    static QSet<PlotItem *> s;
    return s;
}
static double g_refreshHz = 30.0;

void PlotItem::setRefreshHz(double hz)
{
    g_refreshHz = qBound(1.0, hz, 144.0);
    for (PlotItem *p : allPlots())
        p->m_throttle.setInterval(qMax(1, int(1000.0 / g_refreshHz)));
}

double PlotItem::refreshHz()
{
    return g_refreshHz;
}

PlotItem::~PlotItem()
{
    allPlots().remove(this);
}

PlotItem::PlotItem(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    allPlots().insert(this);
    setAntialiasing(true);
    setOpaquePainting(true);
    m_throttle.setInterval(qMax(1, int(1000.0 / g_refreshHz)));
    m_throttle.setSingleShot(true);
    connect(&m_throttle, &QTimer::timeout, this, [this]() {
        if (m_dirty) {
            m_dirty = false;
            update();
        }
    });
}

void PlotItem::setSource(PlotSource *s)
{
    if (m_source == s)
        return;
    if (m_source)
        disconnect(m_source, nullptr, this, nullptr);
    m_source = s;
    if (m_source) {
        connect(m_source, &PlotSource::dataChanged, this, &PlotItem::onSourceData);
        connect(m_source, &PlotSource::channelsChanged, this, &PlotItem::onSourceData);
    }
    emit sourceChanged();
    onSourceData();
}

void PlotItem::setXMin(double v) { if (v != m_xMin) { m_xMin = v; emit rangeChanged(); scheduleUpdate(); } }
void PlotItem::setXMax(double v) { if (v != m_xMax) { m_xMax = v; emit rangeChanged(); scheduleUpdate(); } }
void PlotItem::setYMin(double v) { if (v != m_yMin) { m_yMin = v; emit rangeChanged(); scheduleUpdate(); } }
void PlotItem::setYMax(double v) { if (v != m_yMax) { m_yMax = v; emit rangeChanged(); scheduleUpdate(); } }

void PlotItem::setFollowLatest(bool f)
{
    m_follow = f;
    emit styleChanged();
    onSourceData();
}

void PlotItem::setWindow(double w)
{
    m_window = qMax(0.1, w);
    emit styleChanged();
    onSourceData();
}

void PlotItem::setAutoY(bool a)
{
    m_autoY = a;
    emit styleChanged();
    onSourceData();
}

void PlotItem::setColors(const QVariantList &c)
{
    m_colors = c;
    emit styleChanged();
    scheduleUpdate();
}

void PlotItem::setHiddenChannels(const QStringList &h)
{
    m_hidden = h;
    emit styleChanged();
    onSourceData();
}

void PlotItem::setOnlyChannels(const QStringList &o)
{
    m_only = o;
    emit styleChanged();
    onSourceData();
}

QColor PlotItem::channelColor(int ch) const
{
    if (ch >= 0 && ch < m_colors.size()) {
        const QColor c = m_colors[ch].value<QColor>();
        if (c.isValid())
            return c;
    }
    const int n = int(sizeof(kFallback) / sizeof(kFallback[0]));
    return kFallback[((ch % n) + n) % n];
}

void PlotItem::setRightAxes(const QVariantList &a)
{
    m_rightAxesVar = a;
    m_axes.clear();
    for (const QVariant &v : a) {
        const QVariantMap m = v.toMap();
        Axis ax;
        ax.channels = m.value(QStringLiteral("channels")).toStringList();
        ax.label = m.value(QStringLiteral("label")).toString();
        ax.minSpan = qMax(1e-6, m.value(QStringLiteral("minSpan"), 1.0).toDouble());
        m_axes.append(ax);
    }
    m_right = 12 + 46 * m_axes.size();
    emit styleChanged();
    emit layoutChanged();
    onSourceData();
}

int PlotItem::axisOf(int ch) const
{
    if (m_axes.isEmpty() || !m_source)
        return -1;
    const QString name = m_source->channelName(ch);
    for (int i = 0; i < m_axes.size(); ++i)
        if (m_axes[i].channels.contains(name))
            return i;
    return -1;
}

double PlotItem::yToPixelOn(int axis, double y) const
{
    if (axis < 0)
        return yToPixel(y);
    const Axis &a = m_axes[axis];
    const double span = a.hi - a.lo;
    if (span <= 0)
        return m_top + plotHeight();
    return m_top + plotHeight() - (y - a.lo) / span * plotHeight();
}

void PlotItem::autoRange(int axis, double *outLo, double *outHi) const
{
    double lo = qInf(), hi = -qInf();
    for (int ch = 0; ch < m_source->channelCount(); ++ch) {
        if (!channelShown(ch) || axisOf(ch) != axis)
            continue;
        const int n = m_source->pointCount(ch);
        for (int i = m_source->lowerBound(ch, m_xMin); i < n; ++i) {
            const QPointF p = m_source->point(ch, i);
            if (p.x() > m_xMax)
                break;
            if (!std::isfinite(p.y()))
                continue;
            lo = qMin(lo, p.y());
            hi = qMax(hi, p.y());
        }
    }
    *outLo = lo;
    *outHi = hi;
}

bool PlotItem::channelShown(int ch) const
{
    if (!m_source)
        return false;
    const QString name = m_source->channelName(ch);
    if (!m_only.isEmpty() && !m_only.contains(name))
        return false;
    return !m_hidden.contains(name);
}

void PlotItem::fitX()
{
    if (!m_source)
        return;
    double lo = qInf(), hi = -qInf();
    for (int ch = 0; ch < m_source->channelCount(); ++ch) {
        const int n = m_source->pointCount(ch);
        if (n == 0)
            continue;
        lo = qMin(lo, m_source->point(ch, 0).x());
        hi = qMax(hi, m_source->point(ch, n - 1).x());
    }
    if (std::isfinite(lo) && std::isfinite(hi) && hi > lo) {
        m_xMin = lo;
        m_xMax = hi;
        emit rangeChanged();
        scheduleUpdate();
    }
}

void PlotItem::onSourceData()
{
    if (m_source && m_follow) {
        double latest = -qInf();
        for (int ch = 0; ch < m_source->channelCount(); ++ch) {
            const int n = m_source->pointCount(ch);
            if (n > 0)
                latest = qMax(latest, m_source->point(ch, n - 1).x());
        }
        if (std::isfinite(latest)) {
            const double hi = qMax(latest, m_window);
            const double lo = hi - m_window;
            if (hi != m_xMax || lo != m_xMin) {
                m_xMax = hi;
                m_xMin = lo;
                emit rangeChanged();
            }
        }
    }
    if (m_source && m_autoY) {
        double lo, hi;
        autoRange(-1, &lo, &hi);
        if (std::isfinite(lo) && std::isfinite(hi)) {
            if (hi - lo < 1e-9) {
                lo -= 1;
                hi += 1;
            }
            const double pad = (hi - lo) * 0.08;
            if (lo - pad != m_yMin || hi + pad != m_yMax) {
                m_yMin = lo - pad;
                m_yMax = hi + pad;
                emit rangeChanged();
            }
        }
    }
    // secondary axes always follow their data (at least minSpan, centred)
    if (m_source) {
        for (int a = 0; a < m_axes.size(); ++a) {
            double lo, hi;
            autoRange(a, &lo, &hi);
            if (!std::isfinite(lo) || !std::isfinite(hi))
                continue;
            const double span = qMax(hi - lo, m_axes[a].minSpan);
            const double mid = (lo + hi) / 2;
            m_axes[a].lo = mid - span / 2 - span * 0.08;
            m_axes[a].hi = mid + span / 2 + span * 0.08;
        }
    }
    scheduleUpdate();
}

void PlotItem::scheduleUpdate()
{
    m_dirty = true;
    if (!m_throttle.isActive())
        m_throttle.start();
}

void PlotItem::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    emit layoutChanged();
    update();
}

double PlotItem::xToPixel(double x) const
{
    const double span = m_xMax - m_xMin;
    if (span <= 0)
        return m_left;
    return m_left + (x - m_xMin) / span * plotWidth();
}

double PlotItem::pixelToX(double px) const
{
    return m_xMin + (px - m_left) / plotWidth() * (m_xMax - m_xMin);
}

double PlotItem::yToPixel(double y) const
{
    const double span = m_yMax - m_yMin;
    if (span <= 0)
        return m_top + plotHeight();
    return m_top + plotHeight() - (y - m_yMin) / span * plotHeight();
}

static QString fmtTick(double v, double step)
{
    int decimals = 0;
    while (decimals < 2 && std::fabs(step * std::pow(10.0, decimals) - std::round(step * std::pow(10.0, decimals))) > 1e-6)
        ++decimals;
    QString s = QString::number(v, 'f', decimals);
    if (s.contains(QLatin1Char('.'))) {
        while (s.endsWith(QLatin1Char('0')))
            s.chop(1);
        if (s.endsWith(QLatin1Char('.')))
            s.chop(1);
    }
    if (s == QLatin1String("-0"))
        s = QStringLiteral("0");
    return s;
}

void PlotItem::paint(QPainter *p)
{
    const QRectF all(0, 0, width(), height());
    p->fillRect(all, m_bg);
    p->setRenderHint(QPainter::Antialiasing, true);
    p->setRenderHint(QPainter::TextAntialiasing, true);

    QFont f(QStringLiteral("Noto Sans"));
    f.setPixelSize(10);
    p->setFont(f);

    const QRectF area(m_left, m_top, plotWidth(), plotHeight());

    // grid + tick labels
    QColor gridC = m_grid;
    gridC.setAlphaF(0.45);
    QPen gridPen(gridC, 1);
    p->setPen(gridPen);
    for (int i = 0; i <= m_yDiv; ++i) {
        const double y = m_yMin + (m_yMax - m_yMin) * i / m_yDiv;
        const double py = std::round(yToPixel(y)) + 0.5;
        p->setPen(gridPen);
        p->drawLine(QPointF(area.left(), py), QPointF(area.right(), py));
        p->setPen(m_text);
        p->drawText(QRectF(0, py - 8, m_left - 8, 16), Qt::AlignRight | Qt::AlignVCenter,
                    fmtTick(y, (m_yMax - m_yMin) / m_yDiv));
    }
    for (int i = 0; i <= m_xDiv; ++i) {
        const double x = m_xMin + (m_xMax - m_xMin) * i / m_xDiv;
        const double px = std::round(xToPixel(x)) + 0.5;
        p->setPen(gridPen);
        p->drawLine(QPointF(px, area.top()), QPointF(px, area.bottom()));
        p->setPen(m_text);
        p->drawText(QRectF(px - 30, area.bottom() + 4, 60, 14), Qt::AlignHCenter | Qt::AlignTop,
                    fmtTick(x, (m_xMax - m_xMin) / m_xDiv));
    }
    if (!m_xLabel.isEmpty()) {
        p->setPen(m_text);
        p->drawText(QRectF(area.left(), height() - 14, area.width(), 14), Qt::AlignHCenter | Qt::AlignVCenter, m_xLabel);
    }
    if (!m_yLabel.isEmpty()) {
        p->save();
        p->translate(9, area.center().y());
        p->rotate(-90);
        p->setPen(m_text);
        p->drawText(QRectF(-60, -7, 120, 14), Qt::AlignCenter, m_yLabel);
        p->restore();
    }

    // secondary axes: tick labels at the grid lines, axis label rotated
    for (int a = 0; a < m_axes.size(); ++a) {
        const Axis &ax = m_axes[a];
        const double x0 = area.right() + 4 + a * 46;
        QColor c = m_text;
        if (m_source)
            for (int ch = 0; ch < m_source->channelCount(); ++ch)
                if (axisOf(ch) == a && channelShown(ch)) {
                    c = channelColor(ch); // tint with the first channel on this axis
                    break;
                }
        p->setPen(c);
        for (int i = 0; i <= m_yDiv; ++i) {
            const double y = ax.lo + (ax.hi - ax.lo) * i / m_yDiv;
            const double py = std::round(yToPixelOn(a, y)) + 0.5;
            p->drawText(QRectF(x0, py - 8, 32, 16), Qt::AlignLeft | Qt::AlignVCenter, fmtTick(y, (ax.hi - ax.lo) / m_yDiv));
        }
        // unit under its tick column (short: "°C", "V")
        p->drawText(QRectF(x0, height() - 14, 44, 14), Qt::AlignLeft | Qt::AlignVCenter, ax.label);
    }

    if (!m_source)
        return;

    p->setClipRect(area.adjusted(-1, -2, 1, 2));
    const double w = area.width();
    for (int ch = 0; ch < m_source->channelCount(); ++ch) {
        if (!channelShown(ch))
            continue;
        const int n = m_source->pointCount(ch);
        if (n == 0)
            continue;
        int i0 = qMax(0, m_source->lowerBound(ch, m_xMin) - 1);
        const int axis = axisOf(ch);
        auto yPix = [this, axis](double y) { return yToPixelOn(axis, y); };
        QPen pen(channelColor(ch), m_lineWidth);
        if (axis >= 0)
            pen.setStyle(Qt::DashLine); // secondary-axis channels read apart from the joints
        pen.setJoinStyle(Qt::RoundJoin);
        pen.setCapStyle(Qt::RoundCap);
        p->setPen(pen);

        QPainterPath path;
        bool penDown = false;
        const int visible = n - i0;
        const bool decimate = visible > int(w * 3);
        if (!decimate) {
            for (int i = i0; i < n; ++i) {
                const QPointF d = m_source->point(ch, i);
                if (!std::isfinite(d.y())) {
                    penDown = false;
                    continue;
                }
                const QPointF px(xToPixel(d.x()), yPix(d.y()));
                if (!penDown) {
                    path.moveTo(px);
                    penDown = true;
                } else {
                    path.lineTo(px);
                }
                if (d.x() > m_xMax)
                    break;
            }
        } else {
            // min/max per pixel column
            int col = -1;
            double cmin = 0, cmax = 0, clast = 0;
            auto flush = [&](int c) {
                if (c < 0)
                    return;
                const double px = area.left() + c + 0.5;
                if (!penDown) {
                    path.moveTo(px, yPix(cmax));
                    penDown = true;
                } else {
                    path.lineTo(px, yPix(cmax));
                }
                path.lineTo(px, yPix(cmin));
                path.lineTo(px, yPix(clast));
            };
            for (int i = i0; i < n; ++i) {
                const QPointF d = m_source->point(ch, i);
                if (!std::isfinite(d.y()))
                    continue;
                const int c = int(xToPixel(d.x()) - area.left());
                if (c != col) {
                    flush(col);
                    col = c;
                    cmin = cmax = clast = d.y();
                } else {
                    cmin = qMin(cmin, d.y());
                    cmax = qMax(cmax, d.y());
                    clast = d.y();
                }
                if (d.x() > m_xMax)
                    break;
            }
            flush(col);
        }
        p->drawPath(path);
    }
}
