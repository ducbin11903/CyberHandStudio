#pragma once

#include <QAbstractListModel>
#include <QElapsedTimer>
#include <QObject>
#include <QThread>
#include <QTimer>
#include <memory>

#include "CanBackend.h"
#include "CanProtocol.h"
#include "Transport.h"

class CanBus;

// Runs the adapter in its own thread: open / close, polling for frames, HOST_HB every 100 ms
// (not held up by the UI), bus error state every 500 ms. Every transmit happens here.
class CanWorker : public QObject
{
    Q_OBJECT
public:
    CanWorker();
    ~CanWorker() override;

public slots:
    void start(const CanConfig &cfg);
    void stop();
    void send(const CanV1::Frame &f);

signals:
    void opened(bool ok, const QString &error, const QString &description);
    void closed();
    void received(const QVector<CanV1::Frame> &frames);
    void sent(const QVector<CanV1::Frame> &frames);
    void failed(const QString &error);
    void busState(const CanBusState &s);

private:
    void poll();
    void heartbeat();
    void flushSent();

    std::unique_ptr<CanBackend> m_be;
    QTimer *m_poll = nullptr;
    QTimer *m_hb = nullptr;
    QTimer *m_state = nullptr;
    QElapsedTimer m_clock;
    quint8 m_hbSeq = 0;
    QVector<CanV1::Frame> m_sentBatch;
    QString m_lastError;
};

// One MCU node on the bus as a text channel (node 1 = U6, node 2 = U7): its TEXT lines and
// its STATUS (as a USB-style log line) come in, lines go out as CMD_TEXT 0x200 + node.
class CanNodeLink : public Transport
{
    Q_OBJECT
    Q_PROPERTY(int node READ node CONSTANT)

public:
    CanNodeLink(CanBus *bus, int node);

    int node() const { return m_node; }
    QString kind() const override { return QStringLiteral("can"); }
    QString portName() const override { return QStringLiteral("CAN %1").arg(m_node); }
    bool connected() const override;
    QString errorString() const override;
    double rxBytesPerSec() const override { return m_rxRate; }
    double txBytesPerSec() const override { return m_txRate; }
    double linesPerSec() const override { return m_lineRate; }
    int lastLineAgeMs() const override;

    bool open() override;
    void close() override;
    bool sendLine(const QString &text, bool quiet = false) override;

    // from CanBus
    void deliver(const QString &line, double t, int bytes);
    void noteSent(const QString &line, bool quiet, int bytes);
    void noteRxBytes(int bytes) { m_rxBytes += bytes; }

private:
    void updateStats();

    CanBus *m_bus;
    int m_node;
    QTimer m_statsTimer;
    QElapsedTimer m_lastLine;
    qint64 m_rxBytes = 0, m_txBytes = 0, m_lines = 0;
    double m_rxRate = 0, m_txRate = 0, m_lineRate = 0;
};

// Raw frames for the CAN monitor: time, Rx/Tx, ID, length, FD/BRS, data, decoded hint.
class CanFrameLog : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)
    Q_PROPERTY(bool paused READ paused WRITE setPaused NOTIFY pausedChanged)

public:
    enum Roles { TimeRole = Qt::UserRole + 1, DirRole, IdRole, LenRole, FlagsRole, DataRole, InfoRole };

    explicit CanFrameLog(QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return m_rows.size(); }
    QString filter() const { return m_filterText; }
    void setFilter(const QString &f);
    bool paused() const { return m_paused; }
    void setPaused(bool p);
    void add(const QVector<CanV1::Frame> &frames);
    Q_INVOKABLE void clear();
    // "STATUS U6 #123", "TEXT U7 \"OK e all\"", "POS_ALL I=45.00", "HB 12"
    static QString describe(const CanV1::Frame &f);

signals:
    void countChanged();
    void filterChanged();
    void pausedChanged();

private:
    void rebuild();

    QVector<CanV1::Frame> m_all;
    QVector<int> m_rows;
    QString m_filterText;
    CanV1::IdFilter m_filter;
    bool m_paused = false;
    int m_max = 4000;
};

// CAN connection (Waveshare USB-CAN-FD or the UDP virtual bus) speaking MCB CAN v1.
class CanBus : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int backend READ backend WRITE setBackend NOTIFY settingsChanged)
    Q_PROPERTY(QStringList backendNames READ backendNames CONSTANT)
    Q_PROPERTY(int deviceIndex READ deviceIndex WRITE setDeviceIndex NOTIFY settingsChanged)
    Q_PROPERTY(int channel READ channel WRITE setChannel NOTIFY settingsChanged)
    Q_PROPERTY(int arbBaud READ arbBaud WRITE setArbBaud NOTIFY settingsChanged)
    Q_PROPERTY(int dataBaud READ dataBaud WRITE setDataBaud NOTIFY settingsChanged)
    Q_PROPERTY(bool termination READ termination WRITE setTermination NOTIFY settingsChanged)
    Q_PROPERTY(QString dllPath READ dllPath WRITE setDllPath NOTIFY settingsChanged)
    Q_PROPERTY(QString udpHost READ udpHost WRITE setUdpHost NOTIFY settingsChanged)
    Q_PROPERTY(int udpPort READ udpPort WRITE setUdpPort NOTIFY settingsChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY connectedChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY errorChanged)
    Q_PROPERTY(QString description READ description NOTIFY connectedChanged)
    Q_PROPERTY(double rxFramesPerSec READ rxFramesPerSec NOTIFY statsChanged)
    Q_PROPERTY(double txFramesPerSec READ txFramesPerSec NOTIFY statsChanged)
    Q_PROPERTY(QString statusLostText READ statusLostText NOTIFY statsChanged)
    Q_PROPERTY(qint64 statusLost READ statusLost NOTIFY statsChanged)
    Q_PROPERTY(QString busStateText READ busStateText NOTIFY statsChanged)
    Q_PROPERTY(bool busProblem READ busProblem NOTIFY statsChanged)
    Q_PROPERTY(Transport *node1 READ node1 CONSTANT)
    Q_PROPERTY(Transport *node2 READ node2 CONSTANT)
    Q_PROPERTY(CanFrameLog *monitor READ monitor CONSTANT)

public:
    explicit CanBus(QObject *parent = nullptr);
    ~CanBus() override;

    CanConfig config() const { return m_cfg; }
    void setConfig(const CanConfig &c);
    int backend() const { return m_cfg.backend; }
    void setBackend(int b);
    QStringList backendNames() const;
    int deviceIndex() const { return m_cfg.device; }
    void setDeviceIndex(int d);
    int channel() const { return m_cfg.channel; }
    void setChannel(int c);
    int arbBaud() const { return m_cfg.arbBaud; }
    void setArbBaud(int b);
    int dataBaud() const { return m_cfg.dataBaud; }
    void setDataBaud(int b);
    bool termination() const { return m_cfg.termination; }
    void setTermination(bool on);
    QString dllPath() const { return m_cfg.dllPath; }
    void setDllPath(const QString &p);
    QString udpHost() const { return m_cfg.udpHost; }
    void setUdpHost(const QString &h);
    int udpPort() const { return m_cfg.udpPort; }
    void setUdpPort(int p);

    bool connected() const { return m_connected; }
    bool busy() const { return m_opening; }
    QString errorString() const { return m_error; }
    QString description() const { return m_desc; }
    double rxFramesPerSec() const { return m_rxRate; }
    double txFramesPerSec() const { return m_txRate; }
    qint64 statusLost() const;
    QString statusLostText() const;
    QString busStateText() const;
    bool busProblem() const { return m_state.supported && (m_state.busOff || m_state.passive); }
    Transport *node1() const;
    Transport *node2() const;
    CanNodeLink *node(int n) const { return (n == 1 || n == 2) ? m_nodes[n - 1] : nullptr; }
    CanFrameLog *monitor() const { return m_monitor; }
    // main joint number 1..6: its block becomes the 10 "head" channels of the status line
    void setHeadJoint(int j) { m_headJoint = j; }

    Q_INVOKABLE bool open();
    Q_INVOKABLE void close();
    // monitor: one frame typed by hand (hex data)
    Q_INVOKABLE bool sendRaw(const QString &idHex, const QString &dataHex, bool fd, bool brs);
    // CMD_TEXT: node 1 / 2, 0 = both MCUs (0x200)
    bool sendText(int node, const QString &line);
    // POS_ALL, NaN = keep; at most one frame per 10 ms (later values win)
    void sendPosAll(const QVector<double> &deg);
    // 0x000: every motor off now
    Q_INVOKABLE bool estop();

    // tests: a bus without a worker; frames sent are kept, injectFrame() acts as received
    void openVirtual();
    QVector<CanV1::Frame> virtualSent() const { return m_virtualSent; }
    void clearVirtualSent() { m_virtualSent.clear(); }
    void injectFrame(const CanV1::Frame &f);

signals:
    void settingsChanged();
    void connectedChanged();
    void errorChanged();
    void statsChanged();
    void startWorker(const CanConfig &cfg);
    void stopWorker();
    void transmit(const CanV1::Frame &f);

private:
    void onOpened(bool ok, const QString &error, const QString &description);
    void onClosed();
    void onReceived(const QVector<CanV1::Frame> &frames);
    void onSent(const QVector<CanV1::Frame> &frames);
    void dispatch(const CanV1::Frame &f);
    bool queue(const CanV1::Frame &f);
    void flushPos();
    void setConnected(bool c);
    void setError(const QString &e);
    void updateStats();
    double now() const { return m_clock.isValid() ? m_clock.nsecsElapsed() / 1e9 : 0.0; }

    CanConfig m_cfg;
    QThread m_thread;
    CanWorker *m_worker = nullptr;
    CanNodeLink *m_nodes[2];
    CanFrameLog *m_monitor;
    CanV1::TextAssembler m_text[2];
    CanV1::SeqCounter m_seq[2];
    CanBusState m_state;
    bool m_connected = false;
    bool m_opening = false;
    bool m_virtual = false;
    QVector<CanV1::Frame> m_virtualSent;
    QString m_error;
    QString m_desc;
    int m_headJoint = 3;
    QElapsedTimer m_clock;
    // POS_ALL rate limit
    QVector<double> m_pendingPos;
    bool m_posPending = false;
    QElapsedTimer m_lastPos;
    QTimer m_posTimer;
    // monitor batching
    QVector<CanV1::Frame> m_monitorBatch;
    QTimer m_monitorTimer;
    // stats
    QTimer m_statsTimer;
    qint64 m_rxFrames = 0, m_txFrames = 0;
    double m_rxRate = 0, m_txRate = 0;
};
