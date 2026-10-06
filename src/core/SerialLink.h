#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QObject>
#include <QSerialPort>
#include <QStringList>
#include <QTimer>

// Serial port with line framing and simple throughput statistics.
class SerialLink : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QStringList ports READ ports NOTIFY portsChanged)
    Q_PROPERTY(QStringList portLabels READ portLabels NOTIFY portsChanged)
    Q_PROPERTY(QString portName READ portName WRITE setPortName NOTIFY portNameChanged)
    Q_PROPERTY(int baudRate READ baudRate WRITE setBaudRate NOTIFY baudRateChanged)
    Q_PROPERTY(int lineEnding READ lineEnding WRITE setLineEnding NOTIFY lineEndingChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY errorStringChanged)
    Q_PROPERTY(double rxBytesPerSec READ rxBytesPerSec NOTIFY statsChanged)
    Q_PROPERTY(double txBytesPerSec READ txBytesPerSec NOTIFY statsChanged)
    Q_PROPERTY(double linesPerSec READ linesPerSec NOTIFY statsChanged)
    Q_PROPERTY(int lastLineAgeMs READ lastLineAgeMs NOTIFY statsChanged)
    Q_PROPERTY(qint64 totalRx READ totalRx NOTIFY statsChanged)
    Q_PROPERTY(qint64 totalTx READ totalTx NOTIFY statsChanged)

public:
    enum LineEnding { LF = 0, CR = 1, CRLF = 2 };
    Q_ENUM(LineEnding)

    explicit SerialLink(QObject *parent = nullptr);

    QStringList ports() const { return m_ports; }
    QStringList portLabels() const { return m_labels; }
    QString portName() const { return m_portName; }
    void setPortName(const QString &n);
    int baudRate() const { return m_baud; }
    void setBaudRate(int b);
    int lineEnding() const { return m_lineEnding; }
    void setLineEnding(int e);
    bool connected() const { return m_port.isOpen() || m_virtual; }
    QString errorString() const { return m_error; }
    double rxBytesPerSec() const { return m_rxRate; }
    double txBytesPerSec() const { return m_txRate; }
    double linesPerSec() const { return m_lineRate; }
    int lastLineAgeMs() const;
    qint64 totalRx() const { return m_totalRx; }
    qint64 totalTx() const { return m_totalTx; }

    // seconds since the port was opened (time base for telemetry)
    double elapsed() const { return m_clock.isValid() ? m_clock.nsecsElapsed() / 1e9 : 0.0; }

    Q_INVOKABLE void refreshPorts();
    Q_INVOKABLE bool open();
    Q_INVOKABLE void close();
    Q_INVOKABLE bool toggle();
    // Sends text + line ending. Returns false if not connected.
    Q_INVOKABLE bool sendLine(const QString &text, bool quiet = false);

    // Port without hardware (unit tests): sent lines are kept in virtualSent(),
    // injectLine() delivers a line as if the firmware had sent it.
    void openVirtual();
    void injectLine(const QString &line);
    QStringList virtualSent() const { return m_virtualSent; }
    void clearVirtualSent() { m_virtualSent.clear(); }

signals:
    void portsChanged();
    void portNameChanged();
    void baudRateChanged();
    void lineEndingChanged();
    void connectedChanged();
    void errorStringChanged();
    void statsChanged();
    void lineReceived(const QString &line, double t);
    void lineSent(const QString &line, bool quiet);

private:
    void onReadyRead();
    void onError(QSerialPort::SerialPortError e);
    void setError(const QString &e);
    void updateStats();

    QSerialPort m_port;
    QStringList m_ports;
    QStringList m_labels;
    QString m_portName;
    int m_baud = 115200;
    int m_lineEnding = LF;
    QString m_error;
    QByteArray m_rxBuf;
    QElapsedTimer m_clock;
    QElapsedTimer m_lastLine;
    QTimer m_statsTimer;
    QTimer m_portWatch;
    qint64 m_rxBytes = 0, m_txBytes = 0, m_lines = 0;
    qint64 m_totalRx = 0, m_totalTx = 0;
    double m_rxRate = 0, m_txRate = 0, m_lineRate = 0;
    bool m_lastWasCR = false;
    bool m_virtual = false;
    QStringList m_virtualSent;
};
