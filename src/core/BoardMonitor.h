#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QPointF>
#include <QTimer>
#include <QVariantList>
#include <QVector>

class HandController;

// MCB board sensors (fw 0.9-g474): SHT40 (temperature, humidity) and the VBAT divider, both on U7.
// U6 gets the values over the UART link, so every port reports them.
// Sources: log fields temp / rh / vbat (a missing field = no value, never 0), the `sense` answer
// (values, cut-off thresholds, hot / low flags) and the event lines OTEMP: / VBAT:.
// The firmware switches all 6 motors off itself; the app shows and warns.
class BoardMonitor : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool hasData READ hasData NOTIFY updated)
    Q_PROPERTY(bool hasTemp READ hasTemp NOTIFY updated)
    Q_PROPERTY(bool hasRh READ hasRh NOTIFY updated)
    Q_PROPERTY(bool hasVbat READ hasVbat NOTIFY updated)
    Q_PROPERTY(double temp READ temp NOTIFY updated)
    Q_PROPERTY(double rh READ rh NOTIFY updated)
    Q_PROPERTY(double vbat READ vbat NOTIFY updated)
    // VBAT < 3 V: no battery plugged (the firmware does not check it)
    Q_PROPERTY(bool batteryPresent READ batteryPresent NOTIFY updated)
    Q_PROPERTY(double batteryPct READ batteryPct NOTIFY updated)
    Q_PROPERTY(int cells READ cells WRITE setCells NOTIFY settingsChanged)
    // MCU the values come from (sensor on U7) and their age
    Q_PROPERTY(QString source READ source NOTIFY updated)
    Q_PROPERTY(int ageMs READ ageMs NOTIFY tick)
    Q_PROPERTY(bool stale READ stale NOTIFY tick)
    // firmware has the sensors (fw >= 0.9 G474, or the fields were seen)
    Q_PROPERTY(bool supported READ supported NOTIFY updated)
    // cut-off thresholds from `sense` / event lines (NaN until known)
    Q_PROPERTY(bool thresholdsKnown READ thresholdsKnown NOTIFY thresholdsChanged)
    Q_PROPERTY(double tOff READ tOff NOTIFY thresholdsChanged)
    Q_PROPERTY(double tOn READ tOn NOTIFY thresholdsChanged)
    Q_PROPERTY(double vOff READ vOff NOTIFY thresholdsChanged)
    Q_PROPERTY(double vOn READ vOn NOTIFY thresholdsChanged)
    Q_PROPERTY(int i2cErr READ i2cErr NOTIFY updated)
    Q_PROPERTY(QString senseError READ senseError NOTIFY updated)
    // 0 normal, 1 warning (T >= cut-off - 10 C, VBAT < re-enable level), 2 alarm (motors off)
    Q_PROPERTY(int tempLevel READ tempLevel NOTIFY updated)
    Q_PROPERTY(int vbatLevel READ vbatLevel NOTIFY updated)
    // banner per cause: 0 none, 2 motors off, 1 safe again (press Enable)
    Q_PROPERTY(int otempState READ otempState NOTIFY alarmChanged)
    Q_PROPERTY(int vbatState READ vbatState NOTIFY alarmChanged)
    Q_PROPERTY(QString otempText READ otempText NOTIFY alarmChanged)
    Q_PROPERTY(QString vbatText READ vbatText NOTIFY alarmChanged)
    Q_PROPERTY(bool enableBlocked READ enableBlocked NOTIFY alarmChanged)
    Q_PROPERTY(QString blockReason READ blockReason NOTIFY alarmChanged)
    // sparklines: last 10 min, about 1 point per second
    Q_PROPERTY(QVariantList tempHistory READ tempHistory NOTIFY historyChanged)
    Q_PROPERTY(QVariantList vbatHistory READ vbatHistory NOTIFY historyChanged)
    Q_PROPERTY(double historyNow READ historyNow NOTIFY historyChanged)

public:
    explicit BoardMonitor(HandController *ctrl, QObject *parent = nullptr);

    struct Sense {
        bool ok = false;
        QString mcu;
        double t = qQNaN(), rh = qQNaN(), vbat = qQNaN();
        double tOff = qQNaN(), tOn = qQNaN(), vOff = qQNaN(), vOn = qQNaN();
        int hot = -1, low = -1, i2cErr = -1;
    };
    // "OK sense U7 T=25.0 C RH=45.0 % VBAT=7.99 V | off at T>=70.0 C (on <60.0) VBAT<6.40 V (on >=6.80) | hot=0 low=0 i2c_err=0"
    static bool parseSense(const QString &line, Sense *out);
    // 2S LiPo: 6.4 V = 0 %, 8.4 V = 100 %, LiPo discharge curve per cell
    static double lipoPercent(double voltsPerCell);

    bool hasData() const { return m_hasTemp || m_hasRh || m_hasVbat; }
    bool hasTemp() const { return m_hasTemp; }
    bool hasRh() const { return m_hasRh; }
    bool hasVbat() const { return m_hasVbat; }
    double temp() const { return m_hasTemp ? m_temp : qQNaN(); }
    double rh() const { return m_hasRh ? m_rh : qQNaN(); }
    double vbat() const { return m_hasVbat ? m_vbat : qQNaN(); }
    bool batteryPresent() const { return m_hasVbat && m_vbat >= 3.0; }
    double batteryPct() const;
    int cells() const { return m_cells; }
    void setCells(int c);
    QString source() const { return m_source; }
    int ageMs() const;
    bool stale() const { return !m_age.isValid() || m_age.elapsed() > 3000; }
    bool supported() const { return m_supported || m_seen; }
    void setSupported(bool s);
    bool thresholdsKnown() const { return !qIsNaN(m_tOff) && !qIsNaN(m_vOff); }
    double tOff() const { return m_tOff; }
    double tOn() const { return m_tOn; }
    double vOff() const { return m_vOff; }
    double vOn() const { return m_vOn; }
    int i2cErr() const { return m_i2cErr; }
    QString senseError() const { return m_senseError; }
    int tempLevel() const;
    int vbatLevel() const;
    int otempState() const { return m_otemp; }
    int vbatState() const { return m_vbatSt; }
    QString otempText() const;
    QString vbatText() const;
    bool enableBlocked() const { return m_otemp == 2 || m_vbatSt == 2; }
    QString blockReason() const;
    QVariantList tempHistory() const { return toList(m_tempHist); }
    QVariantList vbatHistory() const { return toList(m_vbatHist); }
    double historyNow() const;

    // one log line of a port (src = its MCU); fields absent = no value
    void updateFromLog(const QStringList &names, const QVector<double> &values, const QString &src);
    // flt of the log: 16 OTEMP, 32 VBAT
    void updateFault(int flt);
    // text line without the "[U7] " prefix; true for sense / OTEMP / VBAT / board ERR lines
    bool processText(const QString &line);
    // an `e` went out: a "safe again" banner is acknowledged
    void onEnableSent();
    // `sense` to the port of U7 (else the main port)
    Q_INVOKABLE bool refresh();
    Q_INVOKABLE void reset();

signals:
    void updated();
    void tick();
    void thresholdsChanged();
    void alarmChanged();
    void settingsChanged();
    void historyChanged();
    // once per event (the same line from U6 and "[U7]" within 1.5 s counts once);
    // alarm = motors switched off, else cleared
    void event(const QString &code, const QString &text, bool alarm);

private:
    static QVariantList toList(const QVector<QPointF> &h);
    void setThresholds(double tOff, double tOn, double vOff, double vOn);
    // cause 0 OTEMP / 1 VBAT; state 2 off, 1 safe, 0 none; emits event when the state changes
    void setState(int cause, int state, const QString &eventText, bool fromEvent);
    void addHistory(QVector<QPointF> &h, double v);
    QString otempTextFor(double t) const;
    QString vbatTextFor(double v) const;

    HandController *m_ctrl;
    bool m_hasTemp = false, m_hasRh = false, m_hasVbat = false;
    double m_temp = 0, m_rh = 0, m_vbat = 0;
    int m_cells = 2;
    QString m_source;
    QElapsedTimer m_age;
    bool m_supported = false;
    bool m_seen = false;
    double m_tOff = qQNaN(), m_tOn = qQNaN(), m_vOff = qQNaN(), m_vOn = qQNaN();
    int m_i2cErr = 0;
    QString m_senseError;
    bool m_hot = false, m_low = false;
    int m_flt = 0;
    int m_otemp = 0, m_vbatSt = 0;
    double m_tripTemp = qQNaN(), m_tripVbat = qQNaN();
    QHash<QString, qint64> m_lastEvent; // "<cause>:<state>" -> ms, de-duplication
    QElapsedTimer m_ackClock;           // last `e` sent
    QElapsedTimer m_clock;
    QVector<QPointF> m_tempHist, m_vbatHist;
    QTimer m_tickTimer;
};
