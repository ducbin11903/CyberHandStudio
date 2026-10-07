#pragma once

#include <QFile>
#include <QObject>
#include <QStringList>
#include <QTextStream>
#include <QUrl>

class HandModel;
class Telemetry;
class BoardMonitor;

// Records the live state to CSV, one row per log sample:
// t, joint angles (T2 .. P, replayable in Text mode), targets, currents, en, flt, mode, temp, rh, vbat.
// Missing values are left empty (not 0).
class Recorder : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool recording READ recording NOTIFY stateChanged)
    Q_PROPERTY(QString file READ file NOTIFY stateChanged)
    Q_PROPERTY(int rows READ rows NOTIFY progress)
    Q_PROPERTY(double seconds READ seconds NOTIFY progress)
    Q_PROPERTY(QUrl folder READ folder CONSTANT)

public:
    Recorder(HandModel *hand, Telemetry *tel, BoardMonitor *board, QObject *parent = nullptr);
    ~Recorder() override;

    bool recording() const { return m_file.isOpen(); }
    QString file() const { return m_path; }
    int rows() const { return m_rows; }
    double seconds() const { return m_last - m_t0; }
    QUrl folder() const;

    QStringList header() const;
    // one CSV row at log time t (seconds since the start of the recording)
    QString row(double t) const;

    // "" = <Documents>/CyberHandStudio/recordings/rec_<date>_<time>.csv
    Q_INVOKABLE bool start(const QString &pathOrUrl = QString());
    Q_INVOKABLE void stop();
    Q_INVOKABLE bool toggle();
    void onSample(double t);

signals:
    void stateChanged();
    void progress();
    void message(const QString &text, bool error);

private:
    HandModel *m_hand;
    Telemetry *m_tel;
    BoardMonitor *m_board;
    QFile m_file;
    QTextStream m_out;
    QString m_path;
    int m_rows = 0;
    double m_t0 = -1;
    double m_last = 0;
    double m_lastFlush = 0;
};
