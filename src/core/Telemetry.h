#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QVariantMap>
#include <QVector>

class HandModel;

// Latest firmware state decoded from log lines.
// Bench H562 log: en,duty,out,i_mA,flt,diag,pos,tgt,vel,mode (one joint).
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
    Q_PROPERTY(double peakMa READ peakMa NOTIFY updated)
    Q_PROPERTY(double currentLimitMa READ currentLimitMa WRITE setCurrentLimitMa NOTIFY limitChanged)
    Q_PROPERTY(bool autoDisable READ autoDisable WRITE setAutoDisable NOTIFY limitChanged)
    Q_PROPERTY(bool overLimit READ overLimit NOTIFY updated)

public:
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
    double errorDeg() const { return m_mode == 1 ? tgtDeg() - posDeg() : 0.0; }
    int homed() const { return m_homed; }
    bool multiJoint() const { return m_multi; }
    double sampleRate() const { return m_rate; }
    QVariantMap fields() const { return m_fields; }
    QString firmwareId() const { return m_fwId; }
    double peakMa() const { return m_peak; }
    double currentLimitMa() const { return m_limit; }
    void setCurrentLimitMa(double l);
    bool autoDisable() const { return m_autoDisable; }
    void setAutoDisable(bool a);
    bool overLimit() const { return m_overLimit; }

    Q_INVOKABLE void resetPeak();
    Q_INVOKABLE void reset();
    Q_INVOKABLE static QString faultText(int flt);

public slots:
    void processTelemetry(double t, const QStringList &names, const QVector<double> &values);
    void processText(const QString &line);

signals:
    void updated();
    void homedChanged();
    void rateChanged();
    void firmwareIdChanged();
    void limitChanged();
    // names/values after adding derived channels (deg), for the live plot
    void sample(double t, const QStringList &names, const QVector<double> &values);
    void overCurrent(double ma);
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
    double m_peak = 0;
    double m_limit = 300;
    bool m_autoDisable = false;
    bool m_overLimit = false;
    int m_count = 0;
    QElapsedTimer m_rateClock;
};
