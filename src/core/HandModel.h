#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QVector>

// One actuated joint of the hand. Angles in degrees (0 = extended).
class Joint : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int index READ index CONSTANT)
    Q_PROPERTY(QString key READ key CONSTANT)
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY configChanged)
    Q_PROPERTY(double countsPer90 READ countsPer90 WRITE setCountsPer90 NOTIFY configChanged)
    Q_PROPERTY(double countsPerRev READ countsPerRev WRITE setCountsPerRev NOTIFY configChanged)
    Q_PROPERTY(double minDeg READ minDeg WRITE setMinDeg NOTIFY configChanged)
    Q_PROPERTY(double maxDeg READ maxDeg WRITE setMaxDeg NOTIFY configChanged)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY configChanged)
    Q_PROPERTY(double measuredDeg READ measuredDeg NOTIFY stateChanged)
    Q_PROPERTY(double targetDeg READ targetDeg NOTIFY stateChanged)
    Q_PROPERTY(bool hasFeedback READ hasFeedback NOTIFY stateChanged)
    Q_PROPERTY(double currentMa READ currentMa NOTIFY stateChanged)
    Q_PROPERTY(double peakMa READ peakMa NOTIFY stateChanged)
    Q_PROPERTY(double commandDeg READ commandDeg WRITE setCommandDeg NOTIFY commandChanged)
    Q_PROPERTY(double previewDeg READ previewDeg NOTIFY previewChanged)

public:
    Joint(int index, const QString &key, const QString &name, QObject *parent = nullptr);

    int index() const { return m_index; }
    QString key() const { return m_key; }
    QString name() const { return m_name; }
    void setName(const QString &n);
    double countsPer90() const { return m_cnt90; }
    void setCountsPer90(double c);
    // encoder counts per revolution of the gearbox output shaft (PPR x 4 x ratio)
    double countsPerRev() const { return m_cpr; }
    void setCountsPerRev(double c);
    double minDeg() const { return m_min; }
    void setMinDeg(double v);
    double maxDeg() const { return m_max; }
    void setMaxDeg(double v);
    bool enabled() const { return m_enabled; }
    void setEnabled(bool e);

    double measuredDeg() const { return m_measured; }
    double targetDeg() const { return m_target; }
    bool hasFeedback() const { return m_hasFeedback; }
    double currentMa() const { return m_current; }
    double peakMa() const { return m_peak; }
    double commandDeg() const { return m_command; }
    void setCommandDeg(double d);
    // What the 3D view shows: measured if we have feedback, else the last command.
    double previewDeg() const { return m_hasFeedback ? m_measured : m_command; }

    double countsToDeg(double c) const { return c * 90.0 / m_cnt90; }
    double degToCounts(double d) const { return d * m_cnt90 / 90.0; }
    double clamp(double d) const { return qBound(m_min, d, m_max); }

    void setFeedback(double measuredDeg, double targetDeg);
    void setCurrent(double ma);
    void clearFeedback();
    Q_INVOKABLE void resetPeak();

    QJsonObject toJson() const;
    void fromJson(const QJsonObject &o);

signals:
    void configChanged();
    void stateChanged();
    void commandChanged();
    void previewChanged();

private:
    int m_index;
    QString m_key;
    QString m_name;
    double m_cnt90 = 7000; // 28000 cnt/rev output, joint 1:1 -> 90 deg = 7000
    double m_cpr = 28000;
    double m_min = 0;
    double m_max = 90;
    bool m_enabled = true;
    double m_measured = 0;
    double m_target = 0;
    bool m_hasFeedback = false;
    double m_current = 0;
    double m_peak = 0;
    double m_command = 0;
};

// The 6 joints of CyberHand, ordered as on the board: T2, T1, I, M, R, P.
class HandModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QList<QObject *> joints READ jointObjects CONSTANT)
    Q_PROPERTY(int count READ count CONSTANT)
    Q_PROPERTY(int benchJoint READ benchJoint WRITE setBenchJoint NOTIFY benchJointChanged)
    Q_PROPERTY(QStringList names READ names NOTIFY namesChanged)
    Q_PROPERTY(QStringList keys READ keys CONSTANT)

public:
    explicit HandModel(QObject *parent = nullptr);

    int count() const { return m_joints.size(); }
    QList<QObject *> jointObjects() const;
    Joint *joint(int i) const { return (i >= 0 && i < m_joints.size()) ? m_joints[i] : nullptr; }
    Q_INVOKABLE QObject *at(int i) const { return joint(i); }
    int benchJoint() const { return m_bench; }
    void setBenchJoint(int j);
    QStringList names() const;
    QStringList keys() const;
    Q_INVOKABLE int indexOfKey(const QString &key) const;

    QVector<double> commandVector() const;
    QVector<double> previewVector() const;
    QVector<double> measuredVector() const;
    Q_INVOKABLE QVariantList commands() const;
    Q_INVOKABLE QVariantList previews() const;
    void setCommands(const QVector<double> &deg);
    Q_INVOKABLE void setCommandList(const QVariantList &deg);
    Q_INVOKABLE void resetPeaks();
    void clearFeedback();

    QJsonArray toJson() const;
    void fromJson(const QJsonArray &a);

signals:
    void benchJointChanged();
    void namesChanged();
    void commandsChanged();

private:
    QVector<Joint *> m_joints;
    int m_bench = 2; // Index finger on the H562 bench
};
