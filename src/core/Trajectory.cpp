#include "Trajectory.h"

#include "HandModel.h"
#include "LineParser.h"

#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextStream>
#include <QUrl>
#include <QtMath>

Trajectory::Trajectory(HandModel *hand, QObject *parent)
    : PlotSource(parent), m_hand(hand)
{
    m_names = hand->keys();
    m_values.resize(m_names.size());
}

int Trajectory::pointCount(int ch) const
{
    if (ch < 0 || ch >= m_values.size())
        return 0;
    return m_times.size();
}

QPointF Trajectory::point(int ch, int i) const
{
    return QPointF(m_times[i], m_values[ch][i]);
}

void Trajectory::setName(const QString &n)
{
    if (n == m_name)
        return;
    m_name = n;
    emit nameChanged();
}

void Trajectory::setError(const QString &e)
{
    m_error = e;
    emit lastErrorChanged();
}

QVector<double> Trajectory::sample(double t) const
{
    QVector<double> out(m_values.size(), qQNaN());
    const int n = m_times.size();
    if (n == 0)
        return out;
    if (t <= m_times.first()) {
        for (int c = 0; c < m_values.size(); ++c)
            out[c] = m_values[c][0];
        return out;
    }
    if (t >= m_times.last()) {
        for (int c = 0; c < m_values.size(); ++c)
            out[c] = m_values[c][n - 1];
        return out;
    }
    int lo = 0, hi = n - 1;
    while (hi - lo > 1) {
        const int mid = (lo + hi) / 2;
        if (m_times[mid] <= t)
            lo = mid;
        else
            hi = mid;
    }
    const double span = m_times[hi] - m_times[lo];
    const double a = span > 0 ? (t - m_times[lo]) / span : 0.0;
    for (int c = 0; c < m_values.size(); ++c) {
        const double v0 = m_values[c][lo], v1 = m_values[c][hi];
        if (qIsNaN(v0) || qIsNaN(v1))
            out[c] = qIsNaN(v0) ? v1 : v0;
        else
            out[c] = v0 + (v1 - v0) * a;
    }
    return out;
}

QVariantList Trajectory::sampleList(double t) const
{
    QVariantList l;
    for (double v : sample(t))
        l << v;
    return l;
}

void Trajectory::setData(const QVector<double> &times, const QVector<QVector<double>> &values)
{
    m_times = times;
    m_values = values;
    m_values.resize(m_names.size());
    for (auto &col : m_values)
        col.resize(m_times.size());
    emit dataChanged();
}

void Trajectory::clear()
{
    m_times.clear();
    for (auto &col : m_values)
        col.clear();
    m_path.clear();
    emit nameChanged();
    emit dataChanged();
}

QString Trajectory::localPath(const QString &pathOrUrl)
{
    if (pathOrUrl.startsWith(QLatin1String("file:")) || pathOrUrl.startsWith(QLatin1String("qrc:"))) {
        const QUrl url(pathOrUrl);
        if (url.scheme() == QLatin1String("qrc"))
            return QLatin1Char(':') + url.path();
        return url.toLocalFile();
    }
    return pathOrUrl;
}

bool Trajectory::loadCsv(const QString &pathOrUrl)
{
    const QString path = localPath(pathOrUrl);
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        setError(tr("Không mở được file: %1").arg(path));
        return false;
    }
    const QString text = QString::fromUtf8(f.readAll());
    if (!fromCsvText(text))
        return false;
    m_path = path;
    setName(QFileInfo(path).completeBaseName());
    return true;
}

bool Trajectory::saveCsv(const QString &pathOrUrl)
{
    QString path = localPath(pathOrUrl);
    if (!path.endsWith(QLatin1String(".csv"), Qt::CaseInsensitive))
        path += QStringLiteral(".csv");
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        setError(tr("Không ghi được file: %1").arg(path));
        return false;
    }
    f.write(toCsvText().toUtf8());
    m_path = path;
    setName(QFileInfo(path).completeBaseName());
    return true;
}

QString Trajectory::toCsvText() const
{
    QString s;
    QTextStream ts(&s);
    ts << "# CyberHand Studio trajectory, degrees\n";
    ts << "t," << m_names.join(QLatin1Char(',')) << '\n';
    for (int i = 0; i < m_times.size(); ++i) {
        ts << LineParser::num(m_times[i], 3);
        for (int c = 0; c < m_values.size(); ++c) {
            ts << ',';
            if (!qIsNaN(m_values[c][i]))
                ts << LineParser::num(m_values[c][i], 2);
        }
        ts << '\n';
    }
    return s;
}

bool Trajectory::fromCsvText(const QString &text)
{
    const QStringList lines = text.split(QRegularExpression(QStringLiteral("[\r\n]+")), Qt::SkipEmptyParts);
    bool percent = false;
    QChar delim = QLatin1Char(',');
    int timeCol = 0;
    QVector<int> colToJoint; // column -> joint index (-1 = ignore)
    bool haveHeader = false;
    QVector<double> times;
    QVector<QVector<double>> vals(m_names.size());

    auto splitLine = [&](const QString &l) {
        QStringList parts = l.split(delim);
        for (QString &p : parts)
            p = p.trimmed();
        return parts;
    };

    for (const QString &rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1String("```")))
            continue;
        if (line.startsWith(QLatin1Char('#'))) {
            if (line.contains(QLatin1String("unit=percent"), Qt::CaseInsensitive)
                || line.contains(QLatin1String("unit=pct"), Qt::CaseInsensitive))
                percent = true;
            continue;
        }
        if (!haveHeader && times.isEmpty()) {
            // pick delimiter from the first data/header line
            if (line.count(QLatin1Char(';')) > line.count(QLatin1Char(',')))
                delim = QLatin1Char(';');
            else if (line.count(QLatin1Char('\t')) > line.count(QLatin1Char(',')))
                delim = QLatin1Char('\t');
            const QStringList parts = splitLine(line);
            bool firstNumeric = false;
            parts.value(0).toDouble(&firstNumeric);
            if (!firstNumeric) {
                haveHeader = true;
                colToJoint.fill(-1, parts.size());
                timeCol = -1;
                for (int c = 0; c < parts.size(); ++c) {
                    const QString h = parts[c].toLower();
                    if (h == QLatin1String("t") || h.startsWith(QLatin1String("time")) || h == QLatin1String("s")) {
                        timeCol = c;
                        continue;
                    }
                    int j = m_hand->indexOfKey(parts[c]);
                    if (j < 0 && h.size() == 2 && h[0] == QLatin1Char('q') && h[1].isDigit())
                        j = h[1].digitValue() - 1;
                    if (j < 0) {
                        const QStringList names = m_hand->names();
                        for (int k = 0; k < names.size(); ++k)
                            if (names[k].compare(parts[c], Qt::CaseInsensitive) == 0)
                                j = k;
                    }
                    if (j >= 0 && j < m_names.size())
                        colToJoint[c] = j;
                }
                if (timeCol < 0) {
                    setError(tr("CSV thiếu cột thời gian (t)"));
                    return false;
                }
                continue;
            }
            // no header: t, q1..q6
            colToJoint.fill(-1, parts.size());
            timeCol = 0;
            for (int c = 1; c < parts.size() && c <= m_names.size(); ++c)
                colToJoint[c] = c - 1;
        }
        const QStringList parts = splitLine(line);
        bool ok = false;
        const double t = parts.value(timeCol).toDouble(&ok);
        if (!ok)
            continue; // skip junk lines (e.g. LLM prose)
        if (!times.isEmpty() && t < times.last()) {
            setError(tr("Thời gian không tăng dần ở t=%1").arg(t));
            return false;
        }
        times << t;
        for (auto &col : vals)
            col << qQNaN();
        for (int c = 0; c < parts.size() && c < colToJoint.size(); ++c) {
            const int j = colToJoint[c];
            if (j < 0 || parts[c].isEmpty())
                continue;
            bool okv = false;
            double v = parts[c].toDouble(&okv);
            if (!okv)
                continue;
            const Joint *jt = m_hand->joint(j);
            if (percent && jt)
                v = jt->minDeg() + (jt->maxDeg() - jt->minDeg()) * v / 100.0;
            vals[j].last() = v;
        }
    }
    if (times.isEmpty()) {
        setError(tr("CSV không có dòng dữ liệu"));
        return false;
    }
    // shift so the trajectory starts at t=0
    const double t0 = times.first();
    for (double &t : times)
        t -= t0;
    setData(times, vals);
    setError(QString());
    return true;
}

QStringList Trajectory::demoNames()
{
    return {QStringLiteral("hand_wave_staggered"), QStringLiteral("grasp_release"), QStringLiteral("finger_count")};
}

void Trajectory::makeDemo(int kind)
{
    const int nJ = m_names.size();
    QVector<double> times;
    QVector<QVector<double>> vals(nJ);
    const double rate = 20.0;
    auto maxOf = [&](int j) { return m_hand->joint(j)->maxDeg(); };

    if (kind == 1) {
        // grasp: all close together with thumb leading, hold, release
        const double dur = 6.0;
        for (int i = 0; i <= int(dur * rate); ++i) {
            const double t = i / rate;
            times << t;
            for (int j = 0; j < nJ; ++j) {
                const double lead = (j <= 1) ? 0.0 : 0.4;
                double a;
                if (t < lead) a = 0;
                else if (t < lead + 1.6) a = 0.5 - 0.5 * qCos(M_PI * (t - lead) / 1.6);
                else if (t < 4.0) a = 1;
                else if (t < 5.6) a = 0.5 + 0.5 * qCos(M_PI * (t - 4.0) / 1.6);
                else a = 0;
                const double target = (j == 1) ? maxOf(j) * 0.7 : maxOf(j); // T1 thumb rotation
                vals[j] << a * target;
            }
        }
        setName(demoNames()[1]);
    } else if (kind == 2) {
        // count 1..4 then open, one finger at a time
        const double seg = 1.2;
        const QVector<int> order = {2, 3, 4, 5, 1};
        const double dur = seg * (order.size() + 2);
        for (int i = 0; i <= int(dur * rate); ++i) {
            const double t = i / rate;
            times << t;
            for (int j = 0; j < nJ; ++j) {
                const int k = order.indexOf(j);
                double a = 1.0; // start closed (fist)
                if (j == 0)
                    a = 0.5;
                if (k >= 0) {
                    const double s = k * seg;
                    if (t > s) a = qMax(0.0, 1.0 - (t - s) / (seg * 0.6));
                }
                if (t > dur - seg) {
                    const double r = (t - (dur - seg)) / seg;
                    a = qMin(1.0, r) * 1.0 + (1.0 - qMin(1.0, r)) * a;
                }
                vals[j] << a * maxOf(j);
            }
        }
        setName(demoNames()[2]);
    } else {
        // staggered close/open wave (like the reference app example)
        const double dur = 10.0;
        for (int i = 0; i <= int(dur * rate); ++i) {
            const double t = i / rate;
            times << t;
            for (int j = 0; j < nJ; ++j) {
                const double d = j * 0.35;
                double a;
                if (t < d) a = 0;
                else if (t < d + 4.5) a = (t - d) / 4.5;
                else if (t < d + 5.5) a = 1;
                else a = qMax(0.0, 1.0 - (t - d - 5.5) / 2.8);
                vals[j] << a * maxOf(j);
            }
        }
        setName(demoNames()[0]);
    }
    m_path.clear();
    setData(times, vals);
    emit nameChanged();
}
