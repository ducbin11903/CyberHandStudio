#pragma once

#include <QByteArray>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVector>

// MCB CAN protocol v1 (fw 0.11): CAN FD ISO + BRS, 11-bit IDs, little-endian.
// Node 1 = U6 (I, T1, T2), node 2 = U7 (M, R, P). Hand joint numbers: 1 T2, 2 T1, 3 I, 4 M, 5 R, 6 P.
namespace CanV1 {

enum Id : quint32 {
    IdEStop = 0x000,   // host -> all, len 0-1: every motor off now
    IdHostHb = 0x080,  // host -> all, len 1 (seq), every 100 ms; lost > 300 ms -> MCB motors off (flt 8)
    IdPosAll = 0x100,  // host -> all, len 12: int16 q[6] in 0.01 deg, 0x7FFF = keep
    IdCmdText = 0x200, // host -> both MCUs; 0x200 + n -> MCU n: one CLI line, no CR/LF
    IdText = 0x280,    // 0x280 + n, MCU n -> host: reply / event character stream, lines end with '\n'
    IdStatus = 0x300,  // 0x300 + n, MCU n -> host: 64-byte status, `canlog <hz>` (default 50 Hz)
    IdPing = 0x700,    // 0x700 + n / 0x780 + n: ping / pong test, ignored
    IdPong = 0x780,
};

constexpr qint16 Keep = 0x7FFF;

struct Frame {
    quint32 id = 0;
    QByteArray data;
    bool fd = true;
    bool brs = true;
    bool ext = false;
    bool tx = false;     // sent by us (monitor)
    double t = 0;        // seconds (monitor)
};

// next valid CAN FD payload length >= n (0..8, 12, 16, 20, 24, 32, 48, 64); -1 when n > 64
int fdLength(int n);
// zero padding up to a valid CAN FD length
QByteArray padFd(const QByteArray &d);

// POS_ALL: degrees per joint 1..6, NaN = keep (0x7FFF)
QByteArray encodePosAll(const QVector<double> &deg);
QVector<double> decodePosAll(const QByteArray &d);

// CMD_TEXT frames for one line (split into 64-byte chunks, each padded to a valid length)
QVector<Frame> encodeText(int node, const QString &line);

struct JointStatus {
    int joint = 0;        // 1..6, 0 = unused block
    bool enabled = false;
    bool homed = false;
    int mode = 0;         // 0 open loop, 1 PID, 2 home
    bool diagLow = false;
    int flt = 0;          // 1 DIAG, 2 OVERI, 4 ESTOP, 8 LINK, 16 OTEMP, 32 VBAT
    int duty = 0;         // %
    qint32 pos = 0;       // counts
    qint32 target = 0;    // counts (= pos when not in PID)
    int currentMa = 0;
};

struct Status {
    int version = 0;
    int node = 0;
    quint16 seq = 0;
    JointStatus joints[3];
    double temp = qQNaN();  // C, NaN = no value
    double rh = qQNaN();    // %RH
    double vbat = qQNaN();  // V
    bool hot = false;
    bool low = false;
    bool linkOk = false;    // UART link U6-U7 alive
    quint32 uptimeMs = 0;
};

bool decodeStatus(const QByteArray &d, Status *out);
QByteArray encodeStatus(const Status &s);

// The status as a USB log line, so the existing parser / telemetry / board path takes it:
// en,duty,out,i_mA,flt,diag,pos,tgt,mode of the head joint (or the node's first joint),
// qN,tN,iN of its 3 joints, temp / rh / vbat when present.
QString statusToLogLine(const Status &s, int headJoint);

// TEXT reassembly for one node: chunks in, complete lines out (NUL padding and '\r' dropped)
class TextAssembler
{
public:
    QStringList feed(const QByteArray &chunk);
    void clear() { m_buf.clear(); }

private:
    QByteArray m_buf;
};

// STATUS sequence numbers -> frames lost (u16 wrap handled)
class SeqCounter
{
public:
    void feed(quint16 seq);
    void reset() { m_last = -1; m_lost = 0; m_received = 0; }
    quint64 lost() const { return m_lost; }
    quint64 received() const { return m_received; }

private:
    int m_last = -1;
    quint64 m_lost = 0;
    quint64 m_received = 0;
};

// "0x301", "301", "280-2FF", "!080" lists (comma / space separated) for the monitor filter
class IdFilter
{
public:
    explicit IdFilter(const QString &text = QString());
    bool accepts(quint32 id) const;
    bool isEmpty() const { return m_in.isEmpty() && m_out.isEmpty(); }

private:
    struct Range { quint32 lo, hi; };
    QVector<Range> m_in, m_out;
};

QString hex(const QByteArray &d);
QByteArray parseHex(const QString &s, bool *ok = nullptr);

} // namespace CanV1

Q_DECLARE_METATYPE(CanV1::Frame)
