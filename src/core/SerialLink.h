#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QSerialPort>
#include <QStringList>
#include <QTimer>

#include "Transport.h"

// Serial port (USB CDC) with line framing and simple throughput statistics.
class SerialLink : public Transport
{
    Q_OBJECT
    Q_PROPERTY(QStringList ports READ ports NOTIFY portsChanged)
    Q_PROPERTY(QStringList portLabels READ portLabels NOTIFY portsChanged)
    Q_PROPERTY(QString portName READ portName WRITE setPortName NOTIFY portNameChanged)
    Q_PROPERTY(int baudRate READ baudRate WRITE setBaudRate NOTIFY baudRateChanged)
    Q_PROPERTY(int lineEnding READ lineEnding WRITE setLineEnding NOTIFY lineEndingChanged)
    Q_PROPERTY(qint64 totalRx READ totalRx NOTIFY statsChanged)
    Q_PROPERTY(qint64 totalTx READ totalTx NOTIFY statsChanged)

public:
    enum LineEnding { LF = 0, CR = 1, CRLF = 2 };
    Q_ENUM(LineEnding)

    explicit SerialLink(QObject *parent = nullptr);

    QStringList ports() const { return m_ports; }
    QStringList portLabels() const { return m_labels; }
    QString kind() const override { return QStringLiteral("serial"); }
    QString portName() const override { return m_portName; }
    void setPortName(const QString &n);
    int baudRate() const { return m_baud; }
    void setBaudRate(int b);
    int lineEnding() const { return m_lineEnding; }
    void setLineEnding(int e);
    bool connected() const override { return m_port.isOpen() || m_virtual; }
    QString errorString() const override { return m_error; }
    double rxBytesPerSec() const override { return m_rxRate; }
    double txBytesPerSec() const override { return m_txRate; }
    double linesPerSec() const override { return m_lineRate; }
    int lastLineAgeMs() const override;
    qint64 totalRx() const { return m_totalRx; }
    qint64 totalTx() const { return m_totalTx; }

    // seconds since the port was opened (time base for telemetry)
    double elapsed() const { return m_clock.isValid() ? m_clock.nsecsElapsed() / 1e9 : 0.0; }

    // pick the first port when none is set (port 1); port 2 leaves the choice to the user
    void setAutoSelect(bool a) { m_autoSelect = a; }

    Q_INVOKABLE void refreshPorts();
    bool open() override;
    void close() override;
    Q_INVOKABLE bool toggle();
    // Sends text + line ending. Returns false if not connected.
    bool sendLine(const QString &text, bool quiet = false) override;

    // Port without hardware (unit tests): sent lines are kept in virtualSent(),
    // injectLine() delivers a line as if the firmware had sent it.
    void openVirtual();
    void injectLine(const QString &line);
    QStringList virtualSent() const { return m_virtualSent; }
    void clearVirtualSent() { m_virtualSent.clear(); }

signals:
    void portsChanged();
    void baudRateChanged();
    void lineEndingChanged();

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
    bool m_autoSelect = true;
    QStringList m_virtualSent;
};
