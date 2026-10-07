#include "Recorder.h"

#include "BoardMonitor.h"
#include "HandModel.h"
#include "Telemetry.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

Recorder::Recorder(HandModel *hand, Telemetry *tel, BoardMonitor *board, QObject *parent)
    : QObject(parent), m_hand(hand), m_tel(tel), m_board(board)
{
    connect(m_tel, &Telemetry::sample, this, [this](double t) { onSample(t); });
}

Recorder::~Recorder()
{
    stop();
}

QUrl Recorder::folder() const
{
    return QUrl::fromLocalFile(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
                               + QStringLiteral("/CyberHandStudio/recordings"));
}

QStringList Recorder::header() const
{
    QStringList h{QStringLiteral("t")};
    const QStringList keys = m_hand->keys();
    for (const QString &k : keys)
        h << k;
    for (const QString &k : keys)
        h << k + QStringLiteral("_tgt");
    for (const QString &k : keys)
        h << k + QStringLiteral("_mA");
    h << QStringLiteral("en") << QStringLiteral("flt") << QStringLiteral("mode")
      << QStringLiteral("temp") << QStringLiteral("rh") << QStringLiteral("vbat");
    return h;
}

QString Recorder::row(double t) const
{
    QStringList r{QString::number(t, 'f', 3)};
    const int n = m_hand->count();
    for (int i = 0; i < n; ++i) {
        const Joint *j = m_hand->joint(i);
        r << (j->hasFeedback() ? QString::number(j->measuredDeg(), 'f', 2) : QString());
    }
    for (int i = 0; i < n; ++i) {
        const Joint *j = m_hand->joint(i);
        r << (j->hasFeedback() ? QString::number(j->targetDeg(), 'f', 2) : QString());
    }
    for (int i = 0; i < n; ++i) {
        const Joint *j = m_hand->joint(i);
        r << (j->hasFeedback() ? QString::number(j->currentMa(), 'f', 0) : QString());
    }
    const bool live = m_tel->hasData();
    r << (live ? QString::number(m_tel->en()) : QString())
      << (live ? QString::number(m_tel->flt()) : QString())
      << (live ? QString::number(m_tel->mode()) : QString());
    r << (m_board->hasTemp() ? QString::number(m_board->temp(), 'f', 1) : QString())
      << (m_board->hasRh() ? QString::number(m_board->rh(), 'f', 1) : QString())
      << (m_board->hasVbat() ? QString::number(m_board->vbat(), 'f', 2) : QString());
    return r.join(QLatin1Char(','));
}

bool Recorder::start(const QString &pathOrUrl)
{
    if (recording())
        return true;
    QString path = pathOrUrl;
    const QUrl url(pathOrUrl);
    if (url.isLocalFile())
        path = url.toLocalFile();
    if (path.isEmpty()) {
        const QString dir = folder().toLocalFile();
        QDir().mkpath(dir);
        path = dir + QStringLiteral("/rec_") + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"))
               + QStringLiteral(".csv");
    }
    m_file.setFileName(path);
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        emit message(tr("Không ghi được %1: %2").arg(path, m_file.errorString()), true);
        return false;
    }
    m_path = path;
    m_out.setDevice(&m_file);
    m_out << "# CyberHand Studio recording " << QDateTime::currentDateTime().toString(Qt::ISODate)
          << " - deg, mA, temp C, rh %, vbat V\n";
    m_out << header().join(QLatin1Char(',')) << '\n';
    m_rows = 0;
    m_t0 = -1;
    m_last = 0;
    m_lastFlush = 0;
    emit stateChanged();
    emit progress();
    emit message(tr("Ghi CSV: %1").arg(path), false);
    return true;
}

void Recorder::stop()
{
    if (!recording())
        return;
    m_out.flush();
    m_file.close();
    emit stateChanged();
    emit message(tr("Đã lưu %1 dòng (%2 s): %3").arg(m_rows).arg(seconds(), 0, 'f', 1).arg(m_path), false);
}

bool Recorder::toggle()
{
    if (recording()) {
        stop();
        return false;
    }
    return start();
}

void Recorder::onSample(double t)
{
    if (!recording())
        return;
    if (m_t0 < 0)
        m_t0 = t;
    if (t < m_last && m_rows > 0)
        return; // time base restarted (reconnect)
    m_last = t;
    m_out << row(t - m_t0) << '\n';
    ++m_rows;
    if (t - m_lastFlush >= 1.0) {
        m_out.flush();
        m_lastFlush = t;
        emit progress();
    }
}
