#include "CanProtocol.h"

#include <QRegularExpression>
#include <QtEndian>
#include <QtMath>

namespace CanV1 {

int fdLength(int n)
{
    static const int lens[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64};
    for (int l : lens)
        if (n <= l)
            return l;
    return -1;
}

QByteArray padFd(const QByteArray &d)
{
    const int l = fdLength(d.size());
    if (l < 0)
        return d.left(64);
    QByteArray out = d;
    out.append(QByteArray(l - d.size(), '\0'));
    return out;
}

QByteArray encodePosAll(const QVector<double> &deg)
{
    QByteArray d(12, '\0');
    for (int i = 0; i < 6; ++i) {
        qint16 v = Keep;
        if (i < deg.size() && std::isfinite(deg[i])) {
            // 0.01 deg; 0x7FFF is reserved for "keep"
            const double c = qBound(-32768.0, std::round(deg[i] * 100.0), 32766.0);
            v = qint16(c);
        }
        qToLittleEndian<qint16>(v, d.data() + 2 * i);
    }
    return d;
}

QVector<double> decodePosAll(const QByteArray &d)
{
    QVector<double> q(6, qQNaN());
    for (int i = 0; i < 6 && 2 * i + 1 < d.size(); ++i) {
        const qint16 v = qFromLittleEndian<qint16>(d.constData() + 2 * i);
        if (v != Keep)
            q[i] = v / 100.0;
    }
    return q;
}

QVector<Frame> encodeText(int node, const QString &line)
{
    QVector<Frame> out;
    QByteArray bytes = line.toUtf8();
    bytes.replace('\r', QByteArray());
    bytes.replace('\n', QByteArray());
    const quint32 id = IdCmdText + quint32(qBound(0, node, 0x7F));
    if (bytes.isEmpty()) {
        Frame f;
        f.id = id;
        out << f;
        return out;
    }
    for (int i = 0; i < bytes.size(); i += 64) {
        Frame f;
        f.id = id;
        f.data = padFd(bytes.mid(i, 64));
        out << f;
    }
    return out;
}

bool decodeStatus(const QByteArray &d, Status *out)
{
    if (d.size() < 60)
        return false;
    const auto *p = reinterpret_cast<const uchar *>(d.constData());
    Status s;
    s.version = p[0];
    s.node = p[1];
    s.seq = qFromLittleEndian<quint16>(p + 2);
    if (s.version != 1)
        return false;
    for (int b = 0; b < 3; ++b) {
        const uchar *q = p + 4 + 14 * b;
        JointStatus &j = s.joints[b];
        j.joint = q[0];
        j.enabled = q[1] & 0x01;
        j.homed = q[1] & 0x02;
        j.mode = (q[1] >> 2) & 0x03;
        j.diagLow = q[1] & 0x10;
        j.flt = q[2];
        j.duty = qint8(q[3]);
        j.pos = qFromLittleEndian<qint32>(q + 4);
        j.target = qFromLittleEndian<qint32>(q + 8);
        j.currentMa = qFromLittleEndian<qint16>(q + 12);
    }
    const qint16 t = qFromLittleEndian<qint16>(p + 46);
    const quint16 rh = qFromLittleEndian<quint16>(p + 48);
    const quint16 mv = qFromLittleEndian<quint16>(p + 50);
    s.temp = t == 0x7FFF ? qQNaN() : t / 10.0;
    s.rh = rh == 0xFFFF ? qQNaN() : rh / 10.0;
    s.vbat = mv == 0 ? qQNaN() : mv / 1000.0;
    s.hot = p[52] & 0x01;
    s.low = p[52] & 0x02;
    s.linkOk = p[52] & 0x04;
    s.uptimeMs = qFromLittleEndian<quint32>(p + 56);
    *out = s;
    return true;
}

QByteArray encodeStatus(const Status &s)
{
    QByteArray d(64, '\0');
    auto *p = reinterpret_cast<uchar *>(d.data());
    p[0] = uchar(s.version ? s.version : 1);
    p[1] = uchar(s.node);
    qToLittleEndian<quint16>(s.seq, p + 2);
    for (int b = 0; b < 3; ++b) {
        uchar *q = p + 4 + 14 * b;
        const JointStatus &j = s.joints[b];
        q[0] = uchar(j.joint);
        q[1] = uchar((j.enabled ? 0x01 : 0) | (j.homed ? 0x02 : 0) | ((j.mode & 3) << 2) | (j.diagLow ? 0x10 : 0));
        q[2] = uchar(j.flt);
        q[3] = uchar(qint8(qBound(-128, j.duty, 127)));
        qToLittleEndian<qint32>(j.pos, q + 4);
        qToLittleEndian<qint32>(j.target, q + 8);
        qToLittleEndian<qint16>(qint16(qBound(-32768, j.currentMa, 32767)), q + 12);
    }
    qToLittleEndian<qint16>(qIsNaN(s.temp) ? qint16(0x7FFF) : qint16(std::round(s.temp * 10)), p + 46);
    qToLittleEndian<quint16>(qIsNaN(s.rh) ? quint16(0xFFFF) : quint16(std::round(s.rh * 10)), p + 48);
    qToLittleEndian<quint16>(qIsNaN(s.vbat) ? quint16(0) : quint16(std::round(s.vbat * 1000)), p + 50);
    p[52] = uchar((s.hot ? 1 : 0) | (s.low ? 2 : 0) | (s.linkOk ? 4 : 0));
    qToLittleEndian<quint32>(s.uptimeMs, p + 56);
    return d;
}

QString statusToLogLine(const Status &s, int headJoint)
{
    // head = the main joint when this node drives it, else the node's first joint
    int h = -1, first = -1, flt = 0;
    for (int b = 0; b < 3; ++b) {
        const JointStatus &j = s.joints[b];
        if (j.joint < 1 || j.joint > 6)
            continue;
        if (first < 0)
            first = b;
        if (j.joint == headJoint)
            h = b;
        flt |= j.flt; // a board-level fault (OTEMP, VBAT, ESTOP, LINK) shows on any joint
    }
    if (h < 0)
        h = first;
    QStringList f;
    if (h >= 0) {
        const JointStatus &j = s.joints[h];
        f << QStringLiteral("en:%1").arg(j.enabled ? 1 : 0)
          << QStringLiteral("duty:%1").arg(j.mode == 0 ? j.duty : 0)
          << QStringLiteral("out:%1").arg(j.duty)
          << QStringLiteral("i_mA:%1").arg(j.currentMa)
          << QStringLiteral("flt:%1").arg(flt)
          << QStringLiteral("diag:%1").arg(j.diagLow ? 0 : 1)
          << QStringLiteral("pos:%1").arg(j.pos)
          << QStringLiteral("tgt:%1").arg(j.mode == 1 ? j.target : 0)
          << QStringLiteral("mode:%1").arg(j.mode);
    }
    for (int b = 0; b < 3; ++b) {
        const JointStatus &j = s.joints[b];
        if (j.joint < 1 || j.joint > 6)
            continue;
        f << QStringLiteral("q%1:%2").arg(j.joint).arg(j.pos)
          << QStringLiteral("t%1:%2").arg(j.joint).arg(j.target)
          << QStringLiteral("i%1:%2").arg(j.joint).arg(j.currentMa);
    }
    if (!qIsNaN(s.temp))
        f << QStringLiteral("temp:") + QString::number(s.temp, 'f', 1);
    if (!qIsNaN(s.rh))
        f << QStringLiteral("rh:") + QString::number(s.rh, 'f', 1);
    if (!qIsNaN(s.vbat))
        f << QStringLiteral("vbat:") + QString::number(s.vbat, 'f', 2);
    return f.join(QLatin1Char(','));
}

QStringList TextAssembler::feed(const QByteArray &chunk)
{
    QStringList lines;
    for (char c : chunk) {
        if (c == '\0' || c == '\r')
            continue; // FD padding
        if (c == '\n') {
            if (!m_buf.isEmpty())
                lines << QString::fromUtf8(m_buf);
            m_buf.clear();
            continue;
        }
        m_buf.append(c);
        if (m_buf.size() > 4096) // runaway line without terminator
            m_buf.clear();
    }
    return lines;
}

void SeqCounter::feed(quint16 seq)
{
    ++m_received;
    if (m_last >= 0) {
        const quint16 gap = quint16(seq - quint16(m_last)); // wraps at 65536
        // gap 0 = repeated frame; a huge gap is a node restart, not a loss
        if (gap > 1 && gap < 1000)
            m_lost += gap - 1;
    }
    m_last = seq;
}

IdFilter::IdFilter(const QString &text)
{
    const QStringList toks = text.split(QRegularExpression(QStringLiteral("[,;\\s]+")), Qt::SkipEmptyParts);
    for (QString t : toks) {
        bool neg = false;
        if (t.startsWith(QLatin1Char('!'))) {
            neg = true;
            t = t.mid(1);
        }
        const QStringList ab = t.split(QLatin1Char('-'));
        bool ok1 = false, ok2 = true;
        auto num = [](QString s, bool *ok) {
            if (s.startsWith(QLatin1String("0x"), Qt::CaseInsensitive))
                s = s.mid(2);
            return s.toUInt(ok, 16);
        };
        const quint32 lo = num(ab.value(0), &ok1);
        const quint32 hi = ab.size() > 1 ? num(ab.value(1), &ok2) : lo;
        if (!ok1 || !ok2)
            continue;
        (neg ? m_out : m_in).append({qMin(lo, hi), qMax(lo, hi)});
    }
}

bool IdFilter::accepts(quint32 id) const
{
    for (const Range &r : m_out)
        if (id >= r.lo && id <= r.hi)
            return false;
    if (m_in.isEmpty())
        return true;
    for (const Range &r : m_in)
        if (id >= r.lo && id <= r.hi)
            return true;
    return false;
}

QString hex(const QByteArray &d)
{
    return QString::fromLatin1(d.toHex(' ')).toUpper();
}

QByteArray parseHex(const QString &s, bool *ok)
{
    QString t = s;
    t.remove(QRegularExpression(QStringLiteral("0x"), QRegularExpression::CaseInsensitiveOption));
    t.remove(QRegularExpression(QStringLiteral("[\\s,:;]")));
    const bool good = t.size() % 2 == 0 && QRegularExpression(QStringLiteral("^[0-9A-Fa-f]*$")).match(t).hasMatch();
    if (ok)
        *ok = good;
    return good ? QByteArray::fromHex(t.toLatin1()) : QByteArray();
}

} // namespace CanV1
