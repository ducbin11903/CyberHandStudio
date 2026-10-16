#include "TcpLink.h"

TcpLink::TcpLink(QObject *parent)
    : Transport(parent), m_sock(new QTcpSocket(this))
{
    connect(m_sock, &QTcpSocket::connected, this, &TcpLink::onConnected);
    connect(m_sock, &QTcpSocket::readyRead, this, &TcpLink::onReadyRead);
    connect(m_sock, &QTcpSocket::disconnected, this, [this]() { onDropped(tr("module đóng kết nối")); });
    connect(m_sock, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        onDropped(m_sock->errorString());
    });

    m_retry.setSingleShot(true);
    connect(&m_retry, &QTimer::timeout, this, &TcpLink::connectNow);
    // an unreachable 192.168.4.1 (PC not on the module's WiFi) can hang for ~20 s
    m_connectTimeout.setSingleShot(true);
    m_connectTimeout.setInterval(3000);
    connect(&m_connectTimeout, &QTimer::timeout, this, [this]() {
        if (!m_connected)
            onDropped(tr("không kết nối được %1 trong 3 s (máy đã vào WiFi CyberHand-XXXX chưa?)").arg(portName()));
    });
    m_statsTimer.setInterval(1000);
    connect(&m_statsTimer, &QTimer::timeout, this, &TcpLink::updateStats);
    m_statsTimer.start();
    m_watch.setInterval(250);
    connect(&m_watch, &QTimer::timeout, this, &TcpLink::checkSilence);
}

TcpLink::~TcpLink()
{
    m_wanted = false;
    m_sock->abort();
}

int TcpLink::lastLineAgeMs() const
{
    return m_lastLine.isValid() ? int(qMin<qint64>(m_lastLine.elapsed(), 1 << 30)) : -1;
}

void TcpLink::setHost(const QString &h)
{
    const QString v = h.trimmed();
    if (v.isEmpty() || v == m_host)
        return;
    m_host = v;
    emit settingsChanged();
    emit portNameChanged();
}

void TcpLink::setPort(int p)
{
    if (p <= 0 || p > 65535 || p == m_port)
        return;
    m_port = p;
    emit settingsChanged();
    emit portNameChanged();
}

void TcpLink::setSilenceTimeoutMs(int ms)
{
    ms = qMax(0, ms);
    if (ms == m_silenceMs)
        return;
    m_silenceMs = ms;
    emit settingsChanged();
}

QString TcpLink::stateText() const
{
    if (m_connected)
        return tr("đã kết nối");
    if (!m_wanted)
        return tr("chưa kết nối");
    return m_reconnects > 0 ? tr("đang kết nối lại…") : tr("đang kết nối…");
}

bool TcpLink::open()
{
    if (m_connected)
        return true;
    m_wanted = true;
    m_reconnects = 0;
    setError(QString());
    m_clock.start();
    connectNow();
    return true;
}

void TcpLink::close()
{
    const bool was = m_wanted || m_connected;
    m_wanted = false;
    m_retry.stop();
    m_connectTimeout.stop();
    m_watch.stop();
    if (m_sock->state() == QAbstractSocket::ConnectedState) {
        // lines written just before (stop on Disconnect) must reach the firmware
        m_sock->flush();
        if (m_sock->bytesToWrite() > 0)
            m_sock->waitForBytesWritten(300);
        m_sock->disconnectFromHost();
        if (m_sock->state() != QAbstractSocket::UnconnectedState)
            m_sock->waitForDisconnected(300);
    }
    m_sock->abort();
    m_rxBuf.clear();
    setConnected(false);
    if (was)
        emit stateChanged();
    updateStats();
}

void TcpLink::connectNow()
{
    if (!m_wanted)
        return;
    m_dropping = true;
    m_sock->abort();
    m_dropping = false;
    m_rxBuf.clear();
    m_lastWasCR = false;
    m_gotData = false;
    m_connectTimeout.start();
    emit stateChanged();
    m_sock->connectToHost(m_host, quint16(m_port));
}

void TcpLink::onConnected()
{
    m_connectTimeout.stop();
    // commands are short and must go out at once (no Nagle), keep-alive for a dead peer
    m_sock->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    m_sock->setSocketOption(QAbstractSocket::KeepAliveOption, 1);
    m_lastRx.start();
    m_watch.start();
    setError(QString());
    setConnected(true);
    emit stateChanged();
}

void TcpLink::onDropped(const QString &why)
{
    if (!m_wanted || m_dropping)
        return; // closed on purpose, or the abort() below reporting itself
    if (!m_connected && m_retry.isActive())
        return; // already handled (error + disconnected for the same drop)
    m_connectTimeout.stop();
    m_watch.stop();
    m_dropping = true;
    m_sock->abort();
    m_dropping = false;
    setError(why);
    setConnected(false);
    scheduleRetry();
}

void TcpLink::scheduleRetry()
{
    if (!m_wanted)
        return;
    ++m_reconnects;
    m_retry.start(m_retryMs);
    emit stateChanged();
    emit statsChanged();
}

void TcpLink::checkSilence()
{
    if (!m_connected || m_silenceMs <= 0 || !m_gotData)
        return;
    if (m_lastRx.elapsed() > m_silenceMs)
        onDropped(tr("mất liên lạc: không có dữ liệu %1 s").arg(m_silenceMs / 1000.0, 0, 'f', 1));
}

void TcpLink::setConnected(bool c)
{
    if (c == m_connected)
        return;
    m_connected = c;
    emit connectedChanged();
}

void TcpLink::setError(const QString &e)
{
    if (e == m_error)
        return;
    m_error = e;
    emit errorStringChanged();
}

bool TcpLink::sendLine(const QString &text, bool quiet)
{
    if (!m_connected)
        return false;
    const QByteArray data = text.toUtf8() + '\n';
    const qint64 n = m_sock->write(data);
    if (n < 0) {
        setError(m_sock->errorString());
        return false;
    }
    m_txBytes += n;
    emit lineSent(text, quiet);
    return true;
}

void TcpLink::onReadyRead()
{
    const QByteArray chunk = m_sock->readAll();
    if (chunk.isEmpty())
        return;
    m_rxBytes += chunk.size();
    m_gotData = true;
    m_lastRx.start();
    const double t = m_clock.isValid() ? m_clock.nsecsElapsed() / 1e9 : 0.0;
    // a line may arrive in pieces, several lines in one piece: rebuild them here
    for (char c : chunk) {
        if (c == '\r' || c == '\n') {
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

void TcpLink::updateStats()
{
    const double dt = m_statsTimer.interval() / 1000.0;
    m_rxRate = m_rxBytes / dt;
    m_txRate = m_txBytes / dt;
    m_lineRate = m_lines / dt;
    m_rxBytes = m_txBytes = m_lines = 0;
    emit statsChanged();
}
