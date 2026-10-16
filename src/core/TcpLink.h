#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QTcpSocket>
#include <QTimer>

#include "Transport.h"

// WiFi link: TCP client to the ESP32-S2 bridge of the MainBoard (SoftAP CyberHand-XXXX,
// 192.168.4.1:8888, transparent TCP <-> UART). Used like a COM port: the same text lines.
// TCP is a byte stream, so lines are rebuilt from whatever recv() hands over (split or merged).
// Reconnects by itself while it is meant to be open; a link that goes silent is dropped and
// reconnected (TCP itself may take seconds to notice a lost WiFi).
class TcpLink : public Transport
{
    Q_OBJECT
    Q_PROPERTY(QString host READ host WRITE setHost NOTIFY settingsChanged)
    Q_PROPERTY(int port READ port WRITE setPort NOTIFY settingsChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(QString stateText READ stateText NOTIFY stateChanged)
    Q_PROPERTY(int reconnects READ reconnects NOTIFY statsChanged)
    Q_PROPERTY(int silenceTimeoutMs READ silenceTimeoutMs WRITE setSilenceTimeoutMs NOTIFY settingsChanged)

public:
    explicit TcpLink(QObject *parent = nullptr);
    ~TcpLink() override;

    QString kind() const override { return QStringLiteral("wifi"); }
    QString portName() const override { return QStringLiteral("%1:%2").arg(m_host).arg(m_port); }
    bool connected() const override { return m_connected; }
    QString errorString() const override { return m_error; }
    double rxBytesPerSec() const override { return m_rxRate; }
    double txBytesPerSec() const override { return m_txRate; }
    double linesPerSec() const override { return m_lineRate; }
    int lastLineAgeMs() const override;

    QString host() const { return m_host; }
    void setHost(const QString &h);
    int port() const { return m_port; }
    void setPort(int p);
    // meant to be open but not connected (connecting / reconnecting)
    bool busy() const { return m_wanted && !m_connected; }
    QString stateText() const;
    int reconnects() const { return m_reconnects; }
    // no byte for this long while lines were coming in -> drop and reconnect (0 = off)
    int silenceTimeoutMs() const { return m_silenceMs; }
    void setSilenceTimeoutMs(int ms);
    // tests
    void setRetryIntervalMs(int ms) { m_retryMs = ms; }
    bool noDelay() const { return m_sock->socketOption(QAbstractSocket::LowDelayOption).toInt() == 1; }

    bool open() override;
    void close() override;
    bool sendLine(const QString &text, bool quiet = false) override;

signals:
    void settingsChanged();
    void stateChanged();

private:
    void connectNow();
    void onConnected();
    void onDropped(const QString &why);
    void onReadyRead();
    void scheduleRetry();
    void setConnected(bool c);
    void setError(const QString &e);
    void updateStats();
    void checkSilence();

    QTcpSocket *m_sock;
    QString m_host = QStringLiteral("192.168.4.1");
    int m_port = 8888;
    bool m_wanted = false;
    bool m_connected = false;
    bool m_gotData = false;
    bool m_dropping = false;
    int m_retryMs = 1000;
    int m_silenceMs = 3000;
    int m_reconnects = 0;
    QString m_error;
    QByteArray m_rxBuf;
    bool m_lastWasCR = false;
    QTimer m_retry;
    QTimer m_connectTimeout;
    QTimer m_statsTimer;
    QTimer m_watch;
    QElapsedTimer m_clock;
    QElapsedTimer m_lastLine;
    QElapsedTimer m_lastRx;
    qint64 m_rxBytes = 0, m_txBytes = 0, m_lines = 0;
    double m_rxRate = 0, m_txRate = 0, m_lineRate = 0;
};
