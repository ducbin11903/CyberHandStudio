#pragma once

#include <QColor>
#include <QPointer>
#include <QQuickPaintedItem>
#include <QTimer>
#include <QVariantList>

#include "PlotSource.h"

// Time-series plot drawn with QPainter (antialiased), styled by properties set from QML.
class PlotItem : public QQuickPaintedItem
{
    Q_OBJECT
    Q_PROPERTY(PlotSource *source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(double xMin READ xMin WRITE setXMin NOTIFY rangeChanged)
    Q_PROPERTY(double xMax READ xMax WRITE setXMax NOTIFY rangeChanged)
    Q_PROPERTY(double yMin READ yMin WRITE setYMin NOTIFY rangeChanged)
    Q_PROPERTY(double yMax READ yMax WRITE setYMax NOTIFY rangeChanged)
    Q_PROPERTY(bool followLatest READ followLatest WRITE setFollowLatest NOTIFY styleChanged)
    Q_PROPERTY(double window READ window WRITE setWindow NOTIFY styleChanged)
    Q_PROPERTY(bool autoY READ autoY WRITE setAutoY NOTIFY styleChanged)
    Q_PROPERTY(QVariantList colors READ colors WRITE setColors NOTIFY styleChanged)
    Q_PROPERTY(QStringList hiddenChannels READ hiddenChannels WRITE setHiddenChannels NOTIFY styleChanged)
    Q_PROPERTY(QStringList onlyChannels READ onlyChannels WRITE setOnlyChannels NOTIFY styleChanged)
    Q_PROPERTY(QColor backgroundColor MEMBER m_bg WRITE setBackgroundColor NOTIFY styleChanged)
    Q_PROPERTY(QColor gridColor MEMBER m_grid WRITE setGridColor NOTIFY styleChanged)
    Q_PROPERTY(QColor textColor MEMBER m_text WRITE setTextColor NOTIFY styleChanged)
    Q_PROPERTY(QString xLabel MEMBER m_xLabel WRITE setXLabel NOTIFY styleChanged)
    Q_PROPERTY(QString yLabel MEMBER m_yLabel WRITE setYLabel NOTIFY styleChanged)
    Q_PROPERTY(int xDivisions MEMBER m_xDiv WRITE setXDivisions NOTIFY styleChanged)
    Q_PROPERTY(int yDivisions MEMBER m_yDiv WRITE setYDivisions NOTIFY styleChanged)
    Q_PROPERTY(double lineWidth MEMBER m_lineWidth WRITE setLineWidth NOTIFY styleChanged)
    // secondary Y axes on the right, each auto-scaled over its own channels:
    // [{ channels: ["temp", "rh"], label: "°C · %RH", minSpan: 10 }, { channels: ["vbat"], label: "V", minSpan: 1 }]
    Q_PROPERTY(QVariantList rightAxes READ rightAxes WRITE setRightAxes NOTIFY styleChanged)
    Q_PROPERTY(double plotLeft READ plotLeft NOTIFY layoutChanged)
    Q_PROPERTY(double plotTop READ plotTop NOTIFY layoutChanged)
    Q_PROPERTY(double plotWidth READ plotWidth NOTIFY layoutChanged)
    Q_PROPERTY(double plotHeight READ plotHeight NOTIFY layoutChanged)

public:
    explicit PlotItem(QQuickItem *parent = nullptr);
    ~PlotItem() override;
    // repaint rate of every plot (Visual Refresh Rate), Hz
    static void setRefreshHz(double hz);
    static double refreshHz();

    void paint(QPainter *p) override;

    PlotSource *source() const { return m_source; }
    void setSource(PlotSource *s);
    double xMin() const { return m_xMin; }
    double xMax() const { return m_xMax; }
    double yMin() const { return m_yMin; }
    double yMax() const { return m_yMax; }
    void setXMin(double v);
    void setXMax(double v);
    void setYMin(double v);
    void setYMax(double v);
    bool followLatest() const { return m_follow; }
    void setFollowLatest(bool f);
    double window() const { return m_window; }
    void setWindow(double w);
    bool autoY() const { return m_autoY; }
    void setAutoY(bool a);
    QVariantList colors() const { return m_colors; }
    void setColors(const QVariantList &c);
    QStringList hiddenChannels() const { return m_hidden; }
    void setHiddenChannels(const QStringList &h);
    QStringList onlyChannels() const { return m_only; }
    void setOnlyChannels(const QStringList &o);
    void setBackgroundColor(const QColor &c) { m_bg = c; emit styleChanged(); scheduleUpdate(); }
    void setGridColor(const QColor &c) { m_grid = c; emit styleChanged(); scheduleUpdate(); }
    void setTextColor(const QColor &c) { m_text = c; emit styleChanged(); scheduleUpdate(); }
    void setXLabel(const QString &s) { m_xLabel = s; emit styleChanged(); scheduleUpdate(); }
    void setYLabel(const QString &s) { m_yLabel = s; emit styleChanged(); scheduleUpdate(); }
    void setXDivisions(int d) { m_xDiv = qMax(1, d); emit styleChanged(); scheduleUpdate(); }
    void setYDivisions(int d) { m_yDiv = qMax(1, d); emit styleChanged(); scheduleUpdate(); }
    void setLineWidth(double w) { m_lineWidth = w; emit styleChanged(); scheduleUpdate(); }
    QVariantList rightAxes() const { return m_rightAxesVar; }
    void setRightAxes(const QVariantList &a);

    double plotLeft() const { return m_left; }
    double plotTop() const { return m_top; }
    double plotWidth() const { return qMax(1.0, width() - m_left - m_right); }
    double plotHeight() const { return qMax(1.0, height() - m_top - m_bottom); }

    Q_INVOKABLE double xToPixel(double x) const;
    Q_INVOKABLE double pixelToX(double px) const;
    Q_INVOKABLE double yToPixel(double y) const;
    Q_INVOKABLE QColor channelColor(int ch) const;
    Q_INVOKABLE void fitX();

signals:
    void sourceChanged();
    void rangeChanged();
    void styleChanged();
    void layoutChanged();

protected:
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private:
    void scheduleUpdate();
    void onSourceData();
    bool channelShown(int ch) const;
    // -1 = left axis, else index into m_axes
    int axisOf(int ch) const;
    double yToPixelOn(int axis, double y) const;
    void autoRange(int axis, double *lo, double *hi) const;

    struct Axis {
        QStringList channels;
        QString label;
        double minSpan = 1;
        double lo = 0, hi = 1;
    };
    QVector<Axis> m_axes;
    QVariantList m_rightAxesVar;

    QPointer<PlotSource> m_source;
    double m_xMin = 0, m_xMax = 10, m_yMin = 0, m_yMax = 100;
    bool m_follow = false;
    double m_window = 10;
    bool m_autoY = false;
    QVariantList m_colors;
    QStringList m_hidden;
    QStringList m_only;
    QColor m_bg = QColor(0x2D, 0x2D, 0x2D);
    QColor m_grid = QColor(0x5B, 0x5B, 0x5B);
    QColor m_text = QColor(0xAC, 0xAC, 0xAC);
    QString m_xLabel = QStringLiteral("Time");
    QString m_yLabel;
    int m_xDiv = 11;
    int m_yDiv = 8;
    double m_lineWidth = 1.6;
    double m_left = 48, m_top = 10, m_right = 12, m_bottom = 34;
    QTimer m_throttle;
    bool m_dirty = false;
};
