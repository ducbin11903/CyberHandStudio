#pragma once

#include <QObject>
#include <QString>

// One text channel to a firmware: a USB serial port (SerialLink) or one MCU node on the
// CAN bus (CanNodeLink). Lines go out with sendLine(), come back with lineReceived().
// HandController, AppCore (telemetry / console) and the QML status rows work on this interface.
class Transport : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString kind READ kind CONSTANT)
    Q_PROPERTY(QString portName READ portName NOTIFY portNameChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY errorStringChanged)
    Q_PROPERTY(double rxBytesPerSec READ rxBytesPerSec NOTIFY statsChanged)
    Q_PROPERTY(double txBytesPerSec READ txBytesPerSec NOTIFY statsChanged)
    Q_PROPERTY(double linesPerSec READ linesPerSec NOTIFY statsChanged)
    Q_PROPERTY(int lastLineAgeMs READ lastLineAgeMs NOTIFY statsChanged)

public:
    using QObject::QObject;

    // "serial" / "can"
    virtual QString kind() const = 0;
    // COM5, /dev/ttyACM0, "CAN 1" ...
    virtual QString portName() const = 0;
    virtual bool connected() const = 0;
    virtual QString errorString() const = 0;
    virtual double rxBytesPerSec() const { return 0; }
    virtual double txBytesPerSec() const { return 0; }
    virtual double linesPerSec() const { return 0; }
    virtual int lastLineAgeMs() const { return -1; }

    Q_INVOKABLE virtual bool open() = 0;
    Q_INVOKABLE virtual void close() = 0;
    // one command line (no line ending); false if not connected
    Q_INVOKABLE virtual bool sendLine(const QString &text, bool quiet = false) = 0;

signals:
    void portNameChanged();
    void connectedChanged();
    void errorStringChanged();
    void statsChanged();
    void lineReceived(const QString &line, double t);
    void lineSent(const QString &line, bool quiet);
};
