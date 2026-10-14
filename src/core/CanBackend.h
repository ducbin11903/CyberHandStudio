#pragma once

#include <QMetaType>
#include <QString>
#include <QVector>
#include <memory>

#include "CanProtocol.h"

// Adapter settings (CAN connection, Settings -> Communication)
struct CanConfig {
    enum Backend { Waveshare = 0, Udp = 1 };
    int backend = Waveshare;
    // Waveshare USB-CAN-FD
    int device = 0;          // device index
    int channel = 0;         // 0 = CAN1
    int arbBaud = 1000000;   // arbitration phase, bit/s
    int dataBaud = 2000000;  // data phase (BRS), bit/s
    bool termination = true; // adapter's own 120 ohm (ZCAN_SetResistanceEnable); with the MCB end = 60 ohm bus
    // virtual bus: fw_sim --can-udp <port>
    QString udpHost = QStringLiteral("127.0.0.1");
    int udpPort = 7470;
};
Q_DECLARE_METATYPE(CanConfig)

// error state of the CAN controller, when the adapter reports it
struct CanBusState {
    bool supported = false;
    quint32 errorCode = 0;
    int rxErrors = 0;   // REC
    int txErrors = 0;   // TEC
    bool warning = false;
    bool passive = false;
    bool busOff = false;
};
Q_DECLARE_METATYPE(CanBusState)

// One adapter driver. Created, used and destroyed in the CAN worker thread only.
class CanBackend
{
public:
    virtual ~CanBackend() = default;
    virtual bool open(const CanConfig &cfg, QString *error) = 0;
    virtual void close() = 0;
    virtual bool write(const CanV1::Frame &f, QString *error) = 0;
    // non-blocking: append what arrived since the last call
    virtual void read(QVector<CanV1::Frame> &out) = 0;
    // false when the adapter does not report error counters / bus-off
    virtual bool state(CanBusState *s) { Q_UNUSED(s); return false; }
    // "Waveshare USB-CAN-FD #0 CAN1 1M/2M", "UDP 127.0.0.1:7470"
    virtual QString description() const = 0;
};

std::unique_ptr<CanBackend> createCanBackend(int backend);

// Virtual bus over UDP (tools/fw_sim.py --can-udp): one datagram per frame,
// "CF" u8 flags (b0 FD, b1 BRS, b2 EXT) u8 len u32 id (LE) data[len]
namespace CanUdp {
QByteArray encode(const CanV1::Frame &f);
bool decode(const QByteArray &d, CanV1::Frame *f);
}
