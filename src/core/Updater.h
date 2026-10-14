#pragma once

#include <QCryptographicHash>
#include <QFile>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QStringList>
#include <QVector>

class QNetworkAccessManager;
class QNetworkReply;

// What the CI publishes next to the zip on the "latest" release (tools/make_version_json.py)
struct UpdateInfo {
    QString version;      // "0.7.0"
    int build = 0;        // CI run number, grows with every main build
    QString commit;       // full sha
    QString date;         // ISO time of the build
    QString zipUrl;
    QString sha256;       // of the zip, lower-case hex
    qint64 size = 0;
    struct Entry { QString commit, subject; };
    QVector<Entry> log;   // newest first
    bool valid() const { return build > 0 && !zipUrl.isEmpty(); }
};

// In-app update from the GitHub "latest" pre-release (Windows zip build):
// check version.json -> download the zip (sha256) -> extract next to it -> a PowerShell script waits
// for the app to exit, copies the files over the install folder (robocopy) and starts the app again.
class Updater : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString currentText READ currentText CONSTANT)
    Q_PROPERTY(QString latestText READ latestText NOTIFY stateChanged)
    Q_PROPERTY(QStringList notes READ notes NOTIFY stateChanged)
    Q_PROPERTY(bool available READ available NOTIFY stateChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString progressText READ progressText NOTIFY progressChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY stateChanged)
    Q_PROPERTY(bool canInstall READ canInstall CONSTANT)
    Q_PROPERTY(QString installNote READ installNote CONSTANT)
    Q_PROPERTY(bool autoCheck READ autoCheck WRITE setAutoCheck NOTIFY autoCheckChanged)
    Q_PROPERTY(QString startMessage READ startMessage NOTIFY startMessageChanged)
    Q_PROPERTY(bool startFailed READ startFailed NOTIFY startMessageChanged)
    Q_PROPERTY(QString releasePage READ releasePage CONSTANT)

public:
    enum State { Idle, Checking, UpToDate, Available, Downloading, Extracting, Ready, Error };
    Q_ENUM(State)

    explicit Updater(QObject *parent = nullptr);
    ~Updater() override;

    int state() const { return m_state; }
    QString currentText() const;
    QString latestText() const;
    QStringList notes() const;
    bool available() const;
    double progress() const { return m_progress; }
    QString progressText() const { return m_progressText; }
    QString errorString() const { return m_error; }
    bool canInstall() const;
    QString installNote() const;
    bool autoCheck() const;
    void setAutoCheck(bool on);
    QString startMessage() const { return m_startMessage; }
    bool startFailed() const { return m_startFailed; }
    QString releasePage() const;

    static int currentBuild();
    static QString currentCommit();
    // tests: pretend to be this build (download + extract also allowed off Windows); build < 0 = off
    static void setTestBuild(int build, const QString &commit);
    // the info file / zip of the "latest" release (overridable for tests: CH_UPDATE_URL = folder URL)
    static QString baseUrl();

    Q_INVOKABLE void check(bool silent = false);
    Q_INVOKABLE void download();
    // starts the copy script and quits the app (motors go to duty 0 on quit)
    Q_INVOKABLE bool installAndRestart();
    Q_INVOKABLE void openReleasePage();
    Q_INVOKABLE void clearStartMessage();

    // after the script restarted the app: --updated / --update-failed
    void setStartedAfterUpdate(bool ok);
    // --install-update <zip>: extract, start the script, return (the caller exits)
    bool installZipNow(const QString &zip, QString *error);

    // pure helpers (tests)
    static bool parseInfo(const QByteArray &json, UpdateInfo *out, QString *error = nullptr);
    static bool isNewer(const UpdateInfo &info, int build, const QString &commit);
    // commit subjects newer than `commit` (all of them, capped, when it is not in the list)
    static QStringList notesSince(const UpdateInfo &info, const QString &commit, int max = 30);
    // the PowerShell copy script (ASCII)
    static QString scriptText();
    static QString appDir();
    static QString workDir();       // %TEMP%/CyberHandStudio-update
    static bool dirWritable(const QString &dir);

signals:
    void stateChanged();
    void progressChanged();
    void autoCheckChanged();
    void startMessageChanged();
    // for the console / event log
    void message(const QString &text, bool error);

private:
    void setState(State s, const QString &error = QString());
    void setProgress(double p, const QString &text);
    void onInfoFinished(bool silent);
    void onZipFinished();
    bool extract(const QString &zip, const QString &stage, bool wait, QString *error);
    void onExtracted(int exitCode);
    bool launchScript(const QString &stage, QString *error);

    QNetworkAccessManager *m_net = nullptr;
    QPointer<QNetworkReply> m_reply;
    QFile m_file;
    QCryptographicHash m_hash{QCryptographicHash::Sha256};
    QProcess *m_unzip = nullptr;
    State m_state = Idle;
    UpdateInfo m_info;
    QString m_error;
    double m_progress = 0;
    QString m_progressText;
    QString m_startMessage;
    bool m_startFailed = false;
};
