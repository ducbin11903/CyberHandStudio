#pragma once

#include <QElapsedTimer>
#include <QJsonObject>
#include <QObject>
#include <QTimer>
#include <QUdpSocket>
#include <QVector>

class HandModel;

// Real-time joint input over UDP (from a script, a data glove, an AI agent...).
// Each datagram is one frame: JSON {"q":[..6]} / {"I":45,...}, CSV "a,b,c,d,e,f",
// or "T2:10,I:45". Frames are forwarded to the hand at `sendRate` when mirroring.
class StreamServer : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int port READ port WRITE setPort NOTIFY portChanged)
    Q_PROPERTY(bool listening READ listening NOTIFY listeningChanged)
    Q_PROPERTY(int unit READ unit WRITE setUnit NOTIFY settingsChanged)
    Q_PROPERTY(bool mirror READ mirror WRITE setMirror NOTIFY settingsChanged)
    Q_PROPERTY(double sendRate READ sendRate WRITE setSendRate NOTIFY settingsChanged)
    Q_PROPERTY(bool demoSource READ demoSource WRITE setDemoSource NOTIFY demoSourceChanged)
    Q_PROPERTY(double fps READ fps NOTIFY statsChanged)
    Q_PROPERTY(qint64 frames READ frames NOTIFY statsChanged)
    Q_PROPERTY(int ageMs READ ageMs NOTIFY statsChanged)
    Q_PROPERTY(QString lastSender READ lastSender NOTIFY statsChanged)
    Q_PROPERTY(QVariantList raw READ raw NOTIFY frameChanged)
    Q_PROPERTY(QVariantList output READ output NOTIFY frameChanged)
    Q_PROPERTY(bool calibrating READ calibrating NOTIFY calibrationChanged)
    Q_PROPERTY(QVariantList calMin READ calMin NOTIFY calibrationChanged)
    Q_PROPERTY(QVariantList calMax READ calMax NOTIFY calibrationChanged)
    Q_PROPERTY(QString calibratedAt READ calibratedAt NOTIFY calibrationChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(QStringList unitNames READ unitNames CONSTANT)

public:
    enum Unit { Degrees = 0, Percent = 1, Calibrated = 2 };
    Q_ENUM(Unit)

    explicit StreamServer(HandModel *hand, QObject *parent = nullptr);

    int port() const { return m_port; }
    void setPort(int p);
    bool listening() const { return m_sock.state() == QAbstractSocket::BoundState; }
    int unit() const { return m_unit; }
    void setUnit(int u);
    bool mirror() const { return m_mirror; }
    void setMirror(bool m);
    double sendRate() const { return m_rate; }
    void setSendRate(double r);
    bool demoSource() const { return m_demo; }
    void setDemoSource(bool d);
    double fps() const { return m_fps; }
    qint64 frames() const { return m_frames; }
    int ageMs() const;
    QString lastSender() const { return m_sender; }
    QVariantList raw() const;
    QVariantList output() const;
    bool calibrating() const { return m_calibrating; }
    QVariantList calMin() const;
    QVariantList calMax() const;
    QString calibratedAt() const { return m_calAt; }
    QString lastError() const { return m_error; }
    QStringList unitNames() const;

    Q_INVOKABLE bool start();
    Q_INVOKABLE void stop();
    Q_INVOKABLE bool toggle();
    Q_INVOKABLE void startCalibration();
    Q_INVOKABLE void finishCalibration();
    Q_INVOKABLE void clearCalibration();
    // Feed one frame by hand (tests, the built-in demo source).
    Q_INVOKABLE bool injectText(const QString &text, const QString &sender = QStringLiteral("local"));

    QJsonObject toJson() const;
    void fromJson(const QJsonObject &o);

signals:
    void portChanged();
    void listeningChanged();
    void settingsChanged();
    void demoSourceChanged();
    void statsChanged();
    void frameChanged();
    void calibrationChanged();
    void lastErrorChanged();
    // mapped frame (degrees, NaN = no value), throttled to sendRate
    void forward(const QVector<double> &deg);
    void preview(const QVector<double> &deg);

private:
    void onReadyRead();
    bool parseFrame(const QByteArray &data, QVector<double> *out) const;
    void handleFrame(const QVector<double> &raw, const QString &sender);
    double mapValue(int ch, double v) const;
    void onSendTick();
    void setError(const QString &e);

    HandModel *m_hand;
    QUdpSocket m_sock;
    int m_port = 7400;
    int m_unit = Percent;
    bool m_mirror = false;
    double m_rate = 50;
    bool m_demo = false;
    QTimer m_sendTimer;
    QTimer m_statsTimer;
    QTimer m_demoTimer;
    QElapsedTimer m_demoClock;
    QElapsedTimer m_lastFrame;
    qint64 m_frames = 0;
    qint64 m_framesWindow = 0;
    double m_fps = 0;
    QString m_sender;
    QVector<double> m_raw;
    QVector<double> m_out;
    bool m_pending = false;
    bool m_calibrating = false;
    QVector<double> m_calMin, m_calMax;
    QString m_calAt;
    QString m_error;
};
