#include "CanBackend.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QHostAddress>
#include <QLibrary>
#include <QNetworkDatagram>
#include <QUdpSocket>
#include <QtEndian>
#include <cstring>

// ---------------------------------------------------------------------------
// UDP datagram format

namespace CanUdp {

QByteArray encode(const CanV1::Frame &f)
{
    QByteArray d;
    d.reserve(8 + f.data.size());
    d.append('C').append('F');
    d.append(char((f.fd ? 1 : 0) | (f.brs ? 2 : 0) | (f.ext ? 4 : 0)));
    d.append(char(qMin(f.data.size(), 64)));
    char id[4];
    qToLittleEndian<quint32>(f.id, id);
    d.append(id, 4);
    d.append(f.data.left(64));
    return d;
}

bool decode(const QByteArray &d, CanV1::Frame *f)
{
    if (d.size() < 8 || d[0] != 'C' || d[1] != 'F')
        return false;
    const int len = uchar(d[3]);
    if (len > 64 || d.size() < 8 + len)
        return false;
    CanV1::Frame r;
    const uchar fl = uchar(d[2]);
    r.fd = fl & 1;
    r.brs = fl & 2;
    r.ext = fl & 4;
    r.id = qFromLittleEndian<quint32>(d.constData() + 4);
    r.data = d.mid(8, len);
    *f = r;
    return true;
}

} // namespace CanUdp

namespace {

class UdpBackend : public CanBackend
{
public:
    bool open(const CanConfig &cfg, QString *error) override
    {
        m_host = QHostAddress(cfg.udpHost.isEmpty() ? QStringLiteral("127.0.0.1") : cfg.udpHost);
        m_port = quint16(cfg.udpPort);
        if (m_host.isNull() || m_port == 0) {
            *error = QStringLiteral("Địa chỉ UDP không hợp lệ: %1:%2").arg(cfg.udpHost).arg(cfg.udpPort);
            return false;
        }
        m_sock = std::make_unique<QUdpSocket>();
        if (!m_sock->bind(m_host.isLoopback() ? QHostAddress(QHostAddress::LocalHost) : QHostAddress(QHostAddress::AnyIPv4), 0)) {
            *error = m_sock->errorString();
            m_sock.reset();
            return false;
        }
        return true;
    }
    void close() override { m_sock.reset(); }
    bool write(const CanV1::Frame &f, QString *error) override
    {
        if (!m_sock)
            return false;
        if (m_sock->writeDatagram(CanUdp::encode(f), m_host, m_port) < 0) {
            *error = m_sock->errorString();
            return false;
        }
        return true;
    }
    void read(QVector<CanV1::Frame> &out) override
    {
        if (!m_sock)
            return;
        while (m_sock->hasPendingDatagrams()) {
            const QNetworkDatagram dg = m_sock->receiveDatagram();
            CanV1::Frame f;
            if (CanUdp::decode(dg.data(), &f))
                out << f;
        }
    }
    QString description() const override
    {
        return QStringLiteral("CAN ảo UDP %1:%2").arg(m_host.toString()).arg(m_port);
    }

private:
    std::unique_ptr<QUdpSocket> m_sock;
    QHostAddress m_host;
    quint16 m_port = 0;
};

// ---------------------------------------------------------------------------
// Waveshare USB-CAN-FD: ControlCANFD.dll (ZLG-style API), layouts from ControlCANFD.h

#if defined(_WIN32) && !defined(_WIN64)
#define WS_CALL __stdcall
#else
#define WS_CALL
#endif

namespace ws {
typedef unsigned int UINT;
typedef unsigned char BYTE;
typedef unsigned short USHORT;
typedef unsigned long long UINT64;
typedef void *HANDLE;

const UINT DeviceType = 41;   // ZCAN_USBCANFD_200U
const UINT StatusOk = 1;
const BYTE TypeCan = 0;
const BYTE TypeCanFd = 1;
const UINT EffFlag = 0x80000000U;
const UINT RtrFlag = 0x40000000U;
const UINT ErrFlag = 0x20000000U;
const BYTE CanFdBrs = 0x01;

struct can_frame {
    UINT can_id;
    BYTE can_dlc;
    BYTE pad;
    BYTE res0;
    BYTE res1;
    BYTE data[8];
};
struct canfd_frame {
    UINT can_id;
    BYTE len;
    BYTE flags;
    BYTE res0;
    BYTE res1;
    BYTE data[64];
};
struct ChannelInitConfig {
    UINT can_type;
    union {
        struct {
            UINT acc_code, acc_mask, reserved;
            BYTE filter, timing0, timing1, mode;
        } can;
        struct {
            UINT acc_code, acc_mask, abit_timing, dbit_timing, brp;
            BYTE filter, mode;
            USHORT pad;
            UINT reserved;
        } canfd;
    };
};
struct TransmitData {
    can_frame frame;
    UINT transmit_type;
};
struct ReceiveData {
    can_frame frame;
    alignas(8) UINT64 timestamp;
};
struct TransmitFDData {
    canfd_frame frame;
    UINT transmit_type;
};
struct ReceiveFDData {
    canfd_frame frame;
    alignas(8) UINT64 timestamp;
};
static_assert(sizeof(can_frame) == 16, "can_frame layout");
static_assert(sizeof(canfd_frame) == 72, "canfd_frame layout");
static_assert(sizeof(ChannelInitConfig) == 32, "ZCAN_CHANNEL_INIT_CONFIG layout");
static_assert(sizeof(TransmitFDData) == 76, "ZCAN_TransmitFD_Data layout");
static_assert(sizeof(ReceiveFDData) == 80, "ZCAN_ReceiveFD_Data layout");

typedef HANDLE(WS_CALL *OpenDevice)(UINT type, UINT index, UINT reserved);
typedef UINT(WS_CALL *CloseDevice)(HANDLE dev);
typedef HANDLE(WS_CALL *InitCAN)(HANDLE dev, UINT ch, ChannelInitConfig *cfg);
typedef UINT(WS_CALL *ChannelFn)(HANDLE ch);
typedef UINT(WS_CALL *GetReceiveNum)(HANDLE ch, BYTE type);
typedef UINT(WS_CALL *Transmit)(HANDLE ch, TransmitData *d, UINT len);
typedef UINT(WS_CALL *Receive)(HANDLE ch, ReceiveData *d, UINT len, int wait);
typedef UINT(WS_CALL *TransmitFD)(HANDLE ch, TransmitFDData *d, UINT len);
typedef UINT(WS_CALL *ReceiveFD)(HANDLE ch, ReceiveFDData *d, UINT len, int wait);
typedef UINT(WS_CALL *SetBaud)(HANDLE dev, UINT ch, UINT value);
// exported but not in ControlCANFD.h: ZLG zlgcan.h signatures (channel handle, struct *)
typedef UINT(WS_CALL *ReadChannelInfo)(HANDLE ch, void *info);
} // namespace ws

class WaveshareBackend : public CanBackend
{
public:
    ~WaveshareBackend() override { close(); }

    bool open(const CanConfig &cfg, QString *error) override
    {
        m_cfg = cfg;
        if (!load(error))
            return false;
        m_dev = m_open(ws::DeviceType, ws::UINT(cfg.device), 0);
        if (!m_dev) {
            *error = QStringLiteral("Không mở được Waveshare USB-CAN-FD #%1 (chưa cắm, chưa cài driver, hoặc phần mềm khác đang dùng)")
                         .arg(cfg.device);
            return false;
        }
        const ws::UINT ch = ws::UINT(cfg.channel);
        // baud rates and the CAN FD standard go before ZCAN_InitCAN
        if (m_setAbit(m_dev, ch, ws::UINT(cfg.arbBaud)) != ws::StatusOk
            || m_setDbit(m_dev, ch, ws::UINT(cfg.dataBaud)) != ws::StatusOk) {
            *error = QStringLiteral("Adapter không nhận tốc độ %1 / %2 bit/s").arg(cfg.arbBaud).arg(cfg.dataBaud);
            closeDevice();
            return false;
        }
        if (m_setStd && m_setStd(m_dev, ch, 0) != ws::StatusOk) { // 0 = CAN FD ISO
            *error = QStringLiteral("Adapter không nhận chuẩn CAN FD ISO");
            closeDevice();
            return false;
        }
        if (m_setRes) // not fatal: some firmware has a jumper instead
            m_setRes(m_dev, ch, cfg.termination ? 1 : 0);
        ws::ChannelInitConfig init;
        std::memset(&init, 0, sizeof(init));
        init.can_type = ws::TypeCanFd;
        init.canfd.acc_code = 0;
        init.canfd.acc_mask = 0xFFFFFFFFu; // accept every ID
        init.canfd.filter = 1;
        init.canfd.mode = 0;               // normal node (1 = listen only)
        m_ch = m_init(m_dev, ch, &init);
        if (!m_ch) {
            *error = QStringLiteral("ZCAN_InitCAN thất bại (kênh CAN%1)").arg(cfg.channel + 1);
            closeDevice();
            return false;
        }
        if (m_start(m_ch) != ws::StatusOk) {
            *error = QStringLiteral("ZCAN_StartCAN thất bại");
            closeDevice();
            return false;
        }
        if (m_clear)
            m_clear(m_ch);
        return true;
    }

    void close() override
    {
        if (m_ch && m_reset)
            m_reset(m_ch);
        m_ch = nullptr;
        closeDevice();
        if (m_lib.isLoaded())
            m_lib.unload();
    }

    bool write(const CanV1::Frame &f, QString *error) override
    {
        if (!m_ch)
            return false;
        const ws::UINT id = (f.id & (f.ext ? 0x1FFFFFFFu : 0x7FFu)) | (f.ext ? ws::EffFlag : 0);
        ws::UINT sent = 0;
        if (f.fd) {
            ws::TransmitFDData td;
            std::memset(&td, 0, sizeof(td));
            td.frame.can_id = id;
            td.frame.len = ws::BYTE(qMin(f.data.size(), 64));
            td.frame.flags = f.brs ? ws::CanFdBrs : 0;
            std::memcpy(td.frame.data, f.data.constData(), td.frame.len);
            td.transmit_type = 0; // normal: retransmit on arbitration loss / error
            sent = m_txFd(m_ch, &td, 1);
        } else {
            ws::TransmitData td;
            std::memset(&td, 0, sizeof(td));
            td.frame.can_id = id;
            td.frame.can_dlc = ws::BYTE(qMin(f.data.size(), 8));
            std::memcpy(td.frame.data, f.data.constData(), td.frame.can_dlc);
            sent = m_tx(m_ch, &td, 1);
        }
        if (sent != 1) {
            *error = QStringLiteral("Adapter không gửi được khung 0x%1").arg(f.id, 3, 16, QLatin1Char('0'));
            return false;
        }
        return true;
    }

    void read(QVector<CanV1::Frame> &out) override
    {
        if (!m_ch)
            return;
        ws::UINT n = m_rxNum(m_ch, ws::TypeCanFd);
        if (n > 0) {
            n = qMin<ws::UINT>(n, 256);
            m_fdBuf.resize(int(n));
            const ws::UINT got = m_rxFd(m_ch, m_fdBuf.data(), n, 0);
            for (ws::UINT i = 0; i < got && i < n; ++i) {
                const ws::canfd_frame &fr = m_fdBuf[int(i)].frame;
                if (fr.can_id & ws::ErrFlag)
                    continue;
                CanV1::Frame f;
                f.ext = fr.can_id & ws::EffFlag;
                f.id = fr.can_id & (f.ext ? 0x1FFFFFFFu : 0x7FFu);
                f.fd = true;
                f.brs = fr.flags & ws::CanFdBrs;
                f.data = QByteArray(reinterpret_cast<const char *>(fr.data), qMin<int>(fr.len, 64));
                out << f;
            }
        }
        n = m_rxNum(m_ch, ws::TypeCan);
        if (n > 0) {
            n = qMin<ws::UINT>(n, 256);
            m_buf.resize(int(n));
            const ws::UINT got = m_rx(m_ch, m_buf.data(), n, 0);
            for (ws::UINT i = 0; i < got && i < n; ++i) {
                const ws::can_frame &fr = m_buf[int(i)].frame;
                if (fr.can_id & (ws::ErrFlag | ws::RtrFlag))
                    continue;
                CanV1::Frame f;
                f.ext = fr.can_id & ws::EffFlag;
                f.id = fr.can_id & (f.ext ? 0x1FFFFFFFu : 0x7FFu);
                f.fd = false;
                f.brs = false;
                f.data = QByteArray(reinterpret_cast<const char *>(fr.data), qMin<int>(fr.can_dlc, 8));
                out << f;
            }
        }
    }

    bool state(CanBusState *s) override
    {
        if (!m_ch || (!m_errInfo && !m_status))
            return false;
        // generous buffers: the structs are 8 / 12 bytes in zlgcan.h
        alignas(8) unsigned char buf[64];
        s->supported = true;
        if (m_errInfo) {
            std::memset(buf, 0, sizeof(buf));
            if (m_errInfo(m_ch, buf) == ws::StatusOk) {
                std::memcpy(&s->errorCode, buf, 4);
                // ZLG error bits: 0x02 warning, 0x04 passive, 0x10 bus error, 0x20 bus off
                s->warning = s->errorCode & 0x02;
                s->passive = s->errorCode & 0x04;
                s->busOff = s->errorCode & 0x20;
            }
        }
        if (m_status) {
            std::memset(buf, 0, sizeof(buf));
            if (m_status(m_ch, buf) == ws::StatusOk) {
                s->rxErrors = buf[6]; // regRECounter
                s->txErrors = buf[7]; // regTECounter
                if (s->txErrors >= 255)
                    s->busOff = true;
            }
        }
        return true;
    }

    QString description() const override
    {
        auto rate = [](int b) {
            return b >= 1000000 ? QStringLiteral("%1M").arg(b / 1000000.0) : QStringLiteral("%1k").arg(b / 1000);
        };
        return QStringLiteral("Waveshare USB-CAN-FD #%1 CAN%2 %3/%4")
            .arg(m_cfg.device).arg(m_cfg.channel + 1).arg(rate(m_cfg.arbBaud), rate(m_cfg.dataBaud));
    }

private:
    bool load(QString *error)
    {
        if (m_lib.isLoaded())
            return true;
        // shipped next to CyberHandStudio.exe (third_party/waveshare, x64)
        m_lib.setFileName(QCoreApplication::applicationDirPath() + QStringLiteral("/ControlCANFD.dll"));
        if (!m_lib.load()) {
            *error = QStringLiteral("Không nạp được ControlCANFD.dll cạnh CyberHandStudio.exe "
                                    "(bản Windows x64 có sẵn file này; giải nén lại đủ file). %1").arg(m_lib.errorString());
            return false;
        }
        auto get = [this](const char *name) { return m_lib.resolve(name); };
        m_open = reinterpret_cast<ws::OpenDevice>(get("ZCAN_OpenDevice"));
        m_close = reinterpret_cast<ws::CloseDevice>(get("ZCAN_CloseDevice"));
        m_init = reinterpret_cast<ws::InitCAN>(get("ZCAN_InitCAN"));
        m_start = reinterpret_cast<ws::ChannelFn>(get("ZCAN_StartCAN"));
        m_reset = reinterpret_cast<ws::ChannelFn>(get("ZCAN_ResetCAN"));
        m_clear = reinterpret_cast<ws::ChannelFn>(get("ZCAN_ClearBuffer"));
        m_rxNum = reinterpret_cast<ws::GetReceiveNum>(get("ZCAN_GetReceiveNum"));
        m_tx = reinterpret_cast<ws::Transmit>(get("ZCAN_Transmit"));
        m_rx = reinterpret_cast<ws::Receive>(get("ZCAN_Receive"));
        m_txFd = reinterpret_cast<ws::TransmitFD>(get("ZCAN_TransmitFD"));
        m_rxFd = reinterpret_cast<ws::ReceiveFD>(get("ZCAN_ReceiveFD"));
        m_setAbit = reinterpret_cast<ws::SetBaud>(get("ZCAN_SetAbitBaud"));
        m_setDbit = reinterpret_cast<ws::SetBaud>(get("ZCAN_SetDbitBaud"));
        m_setStd = reinterpret_cast<ws::SetBaud>(get("ZCAN_SetCANFDStandard"));
        m_setRes = reinterpret_cast<ws::SetBaud>(get("ZCAN_SetResistanceEnable"));
        m_errInfo = reinterpret_cast<ws::ReadChannelInfo>(get("ZCAN_ReadChannelErrInfo"));
        m_status = reinterpret_cast<ws::ReadChannelInfo>(get("ZCAN_ReadChannelStatus"));
        if (!m_open || !m_close || !m_init || !m_start || !m_rxNum || !m_tx || !m_rx || !m_txFd || !m_rxFd
            || !m_setAbit || !m_setDbit) {
            *error = QStringLiteral("%1 thiếu hàm ZCAN_* (không phải thư viện USB-CAN-FD?)").arg(m_lib.fileName());
            m_lib.unload();
            return false;
        }
        return true;
    }

    void closeDevice()
    {
        if (m_dev && m_close)
            m_close(m_dev);
        m_dev = nullptr;
    }

    CanConfig m_cfg;
    QLibrary m_lib;
    ws::HANDLE m_dev = nullptr;
    ws::HANDLE m_ch = nullptr;
    QVector<ws::ReceiveFDData> m_fdBuf;
    QVector<ws::ReceiveData> m_buf;
    ws::OpenDevice m_open = nullptr;
    ws::CloseDevice m_close = nullptr;
    ws::InitCAN m_init = nullptr;
    ws::ChannelFn m_start = nullptr, m_reset = nullptr, m_clear = nullptr;
    ws::GetReceiveNum m_rxNum = nullptr;
    ws::Transmit m_tx = nullptr;
    ws::Receive m_rx = nullptr;
    ws::TransmitFD m_txFd = nullptr;
    ws::ReceiveFD m_rxFd = nullptr;
    ws::SetBaud m_setAbit = nullptr, m_setDbit = nullptr, m_setStd = nullptr, m_setRes = nullptr;
    ws::ReadChannelInfo m_errInfo = nullptr, m_status = nullptr;
};

} // namespace

std::unique_ptr<CanBackend> createCanBackend(int backend)
{
    if (backend == CanConfig::Udp)
        return std::make_unique<UdpBackend>();
    return std::make_unique<WaveshareBackend>();
}
