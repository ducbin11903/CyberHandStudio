#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QVariantMap>
#include <QVector>

class HandModel;

// Latest firmware state decoded from log lines.
// Bench H562 log: en,duty,out,i_mA,flt,diag,pos,tgt,vel,mode (one joint).
// flt bits (MCB fw 0.9): 1 DIAG, 2 OVERI, 4 ESTOP, 8 LINK, 16 OTEMP, 32 VBAT.
// Proposed 6-joint log: q1..q6 (pos counts), t1..t6 (tgt counts), i1..i6 (mA), en, flt, mode.
class Telemetry : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool hasData READ hasData NOTIFY updated)
    Q_PROPERTY(int en READ en NOTIFY updated)
    Q_PROPERTY(double duty READ duty NOTIFY updated)
    Q_PROPERTY(double out READ out NOTIFY updated)
    Q_PROPERTY(double iMa READ iMa NOTIFY updated)
    Q_PROPERTY(int flt READ flt NOTIFY updated)
    Q_PROPERTY(int diag READ diag NOTIFY updated)
    Q_PROPERTY(double pos READ pos NOTIFY updated)
    Q_PROPERTY(double tgt READ tgt NOTIFY updated)
    Q_PROPERTY(double vel READ vel NOTIFY updated)
    Q_PROPERTY(int mode READ mode NOTIFY updated)
    Q_PROPERTY(double posDeg READ posDeg NOTIFY updated)
    Q_PROPERTY(double tgtDeg READ tgtDeg NOTIFY updated)
    Q_PROPERTY(double velDeg READ velDeg NOTIFY updated)
    Q_PROPERTY(double errorDeg READ errorDeg NOTIFY updated)
    Q_PROPERTY(int homed READ homed NOTIFY homedChanged)
    Q_PROPERTY(bool multiJoint READ multiJoint NOTIFY updated)
    Q_PROPERTY(double sampleRate READ sampleRate NOTIFY rateChanged)
    Q_PROPERTY(QVariantMap fields READ fields NOTIFY updated)
    Q_PROPERTY(QString firmwareId READ firmwareId NOTIFY firmwareIdChanged)
    // MCB G474: "mcu=U6 ... keys=I,T1,T2" = this port's MCU and the joints of its motors 1..3
    Q_PROPERTY(QString firmwareMcu READ firmwareMcu NOTIFY firmwareIdChanged)
    Q_PROPERTY(QStringList firmwareKeys READ firmwareKeys NOTIFY firmwareIdChanged)
    Q_PROPERTY(bool firmwareG474 READ firmwareG474 NOTIFY firmwareIdChanged)
    // fw 0.8 bridge: "link=U7" = MCU reached through the UART link, "-" link down, "" not a bridge
    Q_PROPERTY(QString firmwareLink READ firmwareLink NOTIFY firmwareIdChanged)
    Q_PROPERTY(double peakMa READ peakMa NOTIFY updated)
    Q_PROPERTY(double velCps READ velCps NOTIFY updated)
    Q_PROPERTY(double outputRpm READ outputRpm NOTIFY updated)
    Q_PROPERTY(bool diagLow READ diagLow NOTIFY updated)
    Q_PROPERTY(bool noMotion READ noMotion NOTIFY updated)
    Q_PROPERTY(double currentLimitMa READ currentLimitMa WRITE setCurrentLimitMa NOTIFY limitChanged)
    Q_PROPERTY(bool autoDisable READ autoDisable WRITE setAutoDisable NOTIFY limitChanged)
    Q_PROPERTY(int overResponse READ overResponse WRITE setOverResponse NOTIFY limitChanged)
    Q_PROPERTY(int overSource READ overSource WRITE setOverSource NOTIFY limitChanged)
    Q_PROPERTY(double retractDeg READ retractDeg WRITE setRetractDeg NOTIFY limitChanged)
    Q_PROPERTY(double tripMs READ tripMs WRITE setTripMs NOTIFY limitChanged)
    Q_PROPERTY(QStringList responseNames READ responseNames CONSTANT)
    Q_PROPERTY(bool overLimit READ overLimit NOTIFY updated)
    Q_PROPERTY(double totalMa READ totalMa NOTIFY updated)
    Q_PROPERTY(QString recentEvent READ recentEvent NOTIFY updated)
    Q_PROPERTY(QString lastEventText READ lastEventText NOTIFY eventChanged)

public:
    // what happens when a joint current stays above its limit
    enum Response { Warn = 0, Disable = 1, Retract = 2, Hold = 3 };
    Q_ENUM(Response)
    // who reacts: the app (serial, ~50 Hz) or the firmware (1 kHz, command `I`)
    enum Source { App = 0, Firmware = 1 };
    Q_ENUM(Source)

    explicit Telemetry(HandModel *hand, QObject *parent = nullptr);

    bool hasData() const { return m_hasData; }
    int en() const { return m_en; }
    double duty() const { return m_duty; }
    double out() const { return m_out; }
    double iMa() const { return m_iMa; }
    int flt() const { return m_flt; }
    int diag() const { return m_diag; }
    double pos() const { return m_pos; }
    double tgt() const { return m_tgt; }
    double vel() const { return m_vel; }
    int mode() const { return m_mode; }
    double posDeg() const;
    double tgtDeg() const;
    double velDeg() const;
    // velocity from position change over ~0.5 s (the firmware `vel` is a 1 kHz
    // filtered difference and jumps by ~100 cnt/s on a single-count jitter)
    double velCps() const { return m_velPos; }
    // gearbox output shaft speed from the same position-based velocity
    double outputRpm() const;
    // DIAG pin low while the driver is enabled (firmware may ignore it: USE_DIAG 0)
    bool diagLow() const { return m_hasDiag && m_en && m_diag == 0; }
    // duty applied but the encoder does not move
    bool noMotion() const { return m_noMotion; }
    double errorDeg() const { return m_mode == 1 ? tgtDeg() - posDeg() : 0.0; }
    int homed() const { return m_homed; }
    bool multiJoint() const { return m_multi; }
    double sampleRate() const { return m_rate; }
    QVariantMap fields() const { return m_fields; }
    QString firmwareId() const { return m_fwId; }
    QString firmwareMcu() const { return m_fwMcu; }
    QStringList firmwareKeys() const { return m_fwKeys; }
    bool firmwareG474() const { return m_fwG474; }
    QString firmwareLink() const { return m_fwLink; }
    // "fw 0.9-g474" -> (0, 9); -1 unknown
    int firmwareMajor() const { return m_fwMajor; }
    int firmwareMinor() const { return m_fwMinor; }
    // MCB G474 fw >= 0.9: board sensors (temp / rh / vbat, `sense`)
    bool firmwareHasBoardSensors() const { return m_fwG474 && (m_fwMajor > 0 || m_fwMinor >= 9); }
    double peakMa() const { return m_peak; }
    double currentLimitMa() const;
    void setCurrentLimitMa(double l);
    bool autoDisable() const { return m_response == Disable; }
    void setAutoDisable(bool a);
    int overResponse() const { return m_response; }
    void setOverResponse(int r);
    int overSource() const { return m_source; }
    void setOverSource(int s);
    double retractDeg() const { return m_retractDeg; }
    void setRetractDeg(double d);
    double tripMs() const { return m_tripMs; }
    void setTripMs(double ms);
    QStringList responseNames() const;
    bool overLimit() const { return m_overLimit; }
    // sum of all joint currents (multi-joint log) or the bench current
    double totalMa() const { return m_totalMa; }
    // "RETR" / "HOLD" / "TRIP" / "WARN" for 3 s after an over-current reaction
    QString recentEvent() const;
    QString lastEventText() const { return m_eventText; }
    // alarm = something was switched off / refused (event log colour)
    Q_INVOKABLE void noteEvent(const QString &code, const QString &text, bool alarm = true);

    Q_INVOKABLE void resetPeak();
    Q_INVOKABLE void reset();
    Q_INVOKABLE static QString faultText(int flt);

public slots:
    void processTelemetry(double t, const QStringList &names, const QVector<double> &values);
    void processText(const QString &line);

signals:
    void updated();
    void homedChanged();
    // every HOME line (also when Homed was already Yes): 1 done, 0 failed
    void homeResult(int ok);
    void rateChanged();
    void firmwareIdChanged();
    void limitChanged();
    void eventChanged();
    // every noteEvent (event log)
    void eventNoted(const QString &code, const QString &text, bool alarm);
    // names/values after adding derived channels (deg), for the live plot
    void sample(double t, const QStringList &names, const QVector<double> &values);
    void overCurrent(int joint, double ma);
    void configFromFirmware(int joints, double cnt90);

private:
    HandModel *m_hand;
    bool m_hasData = false;
    int m_en = 0;
    double m_duty = 0, m_out = 0, m_iMa = 0;
    int m_flt = 0, m_diag = 0;
    double m_pos = 0, m_tgt = 0, m_vel = 0;
    int m_mode = 0;
    int m_homed = -1;
    bool m_multi = false;
    double m_rate = 0;
    QVariantMap m_fields;
    QString m_fwId;
    QString m_fwMcu;
    QStringList m_fwKeys;
    QString m_fwLink;
    int m_fwMajor = -1, m_fwMinor = -1;
    bool m_fwG474 = false;
    double m_peak = 0;
    int m_response = Warn;
    int m_source = App;
    double m_retractDeg = 10;
    double m_tripMs = 300;
    bool m_overLimit = false;
    quint32 m_overMask = 0;
    double m_totalMa = 0;
    QString m_event;
    QString m_eventText;
    QElapsedTimer m_eventClock;
    int m_count = 0;
    struct PosSample { double t; double pos; };
    QVector<PosSample> m_posHist;
    double m_velPos = 0;
    bool m_hasDiag = false;
    bool m_noMotion = false;
    double m_lastMultiT = -1;
    QElapsedTimer m_rateClock;
};
