#include "SerialLink.h"

#include <QSerialPortInfo>

SerialLink::SerialLink(QObject *parent)
    : QObject(parent)
{
    connect(&m_port, &QSerialPort::readyRead, this, &SerialLink::onReadyRead);
    connect(&m_port, &QSerialPort::errorOccurred, this, &SerialLink::onError);
    m_statsTimer.setInterval(500);
    connect(&m_statsTimer, &QTimer::timeout, this, &SerialLink::updateStats);
    m_statsTimer.start();
    // keep the port list fresh while disconnected
    m_portWatch.setInterval(2000);
    connect(&m_portWatch, &QTimer::timeout, this, [this]() {
        if (!connected())
            refreshPorts();
    });
    m_portWatch.start();
    refreshPorts();
}

void SerialLink::setPortName(const QString &n)
{
    if (n == m_portName)
        return;
    m_portName = n;
    emit portNameChanged();
}

void SerialLink::setBaudRate(int b)
{
    if (b <= 0 || b == m_baud)
        return;
    m_baud = b;
    if (m_port.isOpen())
        m_port.setBaudRate(b);
    emit baudRateChanged();
}

void SerialLink::setLineEnding(int e)
{
    e = qBound(0, e, 2);
    if (e == m_lineEnding)
        return;
    m_lineEnding = e;
    emit lineEndingChanged();
}

int SerialLink::lastLineAgeMs() const
{
    if (!m_lastLine.isValid())
        return -1;
    return int(qMin<qint64>(m_lastLine.elapsed(), 99999999));
}

void SerialLink::refreshPorts()
{
    QStringList names, labels;
    const auto infos = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo &info : infos) {
        names << info.portName();
        QString label = info.portName();
        if (!info.description().isEmpty())
            label += QStringLiteral(" — ") + info.description();
        labels << label;
    }
    if (names != m_ports || labels != m_labels) {
        m_ports = names;
        m_labels = labels;
        emit portsChanged();
    }
    if (m_portName.isEmpty() && !m_ports.isEmpty())
        setPortName(m_ports.first());
}

bool SerialLink::open()
{
    if (m_port.isOpen())
        return true;
    if (m_portName.isEmpty()) {
        setError(tr("Chưa chọn cổng"));
        return false;
    }
    m_port.setPortName(m_portName);
    m_port.setBaudRate(m_baud);
    m_port.setDataBits(QSerialPort::Data8);
    m_port.setParity(QSerialPort::NoParity);
    m_port.setStopBits(QSerialPort::OneStop);
    m_port.setFlowControl(QSerialPort::NoFlowControl);
    if (!m_port.open(QIODevice::ReadWrite)) {
        setError(m_port.errorString());
        return false;
    }
    m_port.setDataTerminalReady(true);
    m_rxBuf.clear();
    m_clock.start();
    m_lastLine.invalidate();
    m_totalRx = m_totalTx = 0;
    setError(QString());
    emit connectedChanged();
    return true;
}

void SerialLink::close()
{
    if (m_virtual) {
        m_virtual = false;
        emit connectedChanged();
        return;
    }
    if (!m_port.isOpen())
        return;
    m_port.close();
    m_rxBuf.clear();
    emit connectedChanged();
    updateStats();
}

bool SerialLink::toggle()
{
    if (connected()) {
        close();
        return false;
    }
    return open();
}

void SerialLink::openVirtual()
{
    if (connected())
        return;
    m_virtual = true;
    m_virtualSent.clear();
    m_clock.start();
    emit connectedChanged();
}

void SerialLink::injectLine(const QString &line)
{
    m_lastLine.start();
    emit lineReceived(line, elapsed());
}

bool SerialLink::sendLine(const QString &text, bool quiet)
{
    if (m_virtual) {
        m_virtualSent << text;
        emit lineSent(text, quiet);
        return true;
    }
    if (!m_port.isOpen())
        return false;
    QByteArray data = text.toUtf8();
    switch (m_lineEnding) {
    case CR: data.append('\r'); break;
    case CRLF: data.append("\r\n"); break;
    default: data.append('\n'); break;
    }
    const qint64 n = m_port.write(data);
    if (n < 0) {
        setError(m_port.errorString());
        return false;
    }
    m_txBytes += n;
    m_totalTx += n;
    emit lineSent(text, quiet);
    return true;
}

void SerialLink::onReadyRead()
{
    const QByteArray chunk = m_port.readAll();
    m_rxBytes += chunk.size();
    m_totalRx += chunk.size();
    const double t = elapsed();
    for (char c : chunk) {
        if (c == '\r' || c == '\n') {
            // treat "\r\n" as a single terminator
            if (c == '\n' && m_lastWasCR) {
                m_lastWasCR = false;
                continue;
            }
            m_lastWasCR = (c == '\r');
            if (!m_rxBuf.isEmpty()) {
                const QString line = QString::fromUtf8(m_rxBuf);
                m_rxBuf.clear();
                ++m_lines;
                m_lastLine.start();
                emit lineReceived(line, t);
            }
        } else {
            m_lastWasCR = false;
            m_rxBuf.append(c);
            if (m_rxBuf.size() > 4096) // runaway line without terminator
                m_rxBuf.clear();
        }
    }
}

void SerialLink::onError(QSerialPort::SerialPortError e)
{
    if (e == QSerialPort::NoError)
        return;
    setError(m_port.errorString());
    if (e == QSerialPort::ResourceError || e == QSerialPort::PermissionError) {
        // device unplugged or taken by another program
        if (m_port.isOpen()) {
            m_port.close();
            emit connectedChanged();
        }
    }
}

void SerialLink::setError(const QString &e)
{
    if (e == m_error)
        return;
    m_error = e;
    emit errorStringChanged();
}

void SerialLink::updateStats()
{
    const double dt = m_statsTimer.interval() / 1000.0;
    m_rxRate = m_rxBytes / dt;
    m_txRate = m_txBytes / dt;
    m_lineRate = m_lines / dt;
    m_rxBytes = m_txBytes = m_lines = 0;
    emit statsChanged();
}
