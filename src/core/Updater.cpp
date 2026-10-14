#include "Updater.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QTimer>
#include <QUrl>

#ifndef CH_VERSION
#define CH_VERSION "0.0.0"
#endif
#ifndef CH_BUILD
#define CH_BUILD 0
#endif
#ifndef CH_COMMIT
#define CH_COMMIT ""
#endif

namespace {
const char *ExeName = "CyberHandStudio.exe";

QString shortSha(const QString &s) { return s.left(7); }

bool sameCommit(const QString &a, const QString &b)
{
    const int n = qMin(a.size(), b.size());
    return n >= 7 && a.left(n).compare(b.left(n), Qt::CaseInsensitive) == 0;
}

QString mb(qint64 bytes) { return QString::number(bytes / 1048576.0, 'f', 1) + QStringLiteral(" MB"); }

bool s_test = false;
int s_testBuild = 0;
QString s_testCommit;
} // namespace

Updater::Updater(QObject *parent)
    : QObject(parent)
{
}

Updater::~Updater()
{
    if (m_reply)
        m_reply->abort();
    if (m_unzip && m_unzip->state() != QProcess::NotRunning)
        m_unzip->kill();
}

int Updater::currentBuild() { return s_test ? s_testBuild : CH_BUILD; }
QString Updater::currentCommit() { return s_test ? s_testCommit : QStringLiteral(CH_COMMIT); }

void Updater::setTestBuild(int build, const QString &commit)
{
    s_test = build >= 0; // < 0: back to the real build
    s_testBuild = build;
    s_testCommit = commit;
}

QString Updater::baseUrl()
{
    QString u = qEnvironmentVariable("CH_UPDATE_URL");
    if (u.isEmpty())
        u = QStringLiteral("https://github.com/ducbin11903/CyberHandStudio/releases/download/latest/");
    if (!u.endsWith(QLatin1Char('/')))
        u += QLatin1Char('/');
    return u;
}

QString Updater::releasePage() const
{
    return QStringLiteral("https://github.com/ducbin11903/CyberHandStudio/releases/tag/latest");
}

QString Updater::appDir() { return QDir::cleanPath(QCoreApplication::applicationDirPath()); }

QString Updater::workDir()
{
    return QDir::cleanPath(QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                           + QStringLiteral("/CyberHandStudio-update"));
}

bool Updater::dirWritable(const QString &dir)
{
    QTemporaryFile f(dir + QStringLiteral("/.ch_write_test_XXXXXX"));
    return f.open();
}

QString Updater::currentText() const
{
    QString s = QStringLiteral(CH_VERSION);
    if (currentBuild() > 0)
        s += QStringLiteral(" · build %1 · %2").arg(currentBuild()).arg(shortSha(currentCommit()));
    else
        s += QStringLiteral(" · bản dev");
    return s;
}

QString Updater::latestText() const
{
    if (!m_info.valid())
        return QString();
    QString d = m_info.date;
    const QDateTime t = QDateTime::fromString(m_info.date, Qt::ISODate);
    if (t.isValid())
        d = t.toLocalTime().toString(QStringLiteral("dd/MM/yyyy HH:mm"));
    return QStringLiteral("%1 · build %2 · %3 · %4").arg(m_info.version).arg(m_info.build).arg(shortSha(m_info.commit), d);
}

QStringList Updater::notes() const
{
    return m_info.valid() ? notesSince(m_info, currentCommit()) : QStringList();
}

bool Updater::available() const { return isNewer(m_info, currentBuild(), currentCommit()); }

bool Updater::canInstall() const
{
    if (s_test)
        return true;
#ifdef Q_OS_WIN
    return currentBuild() > 0;
#else
    return false;
#endif
}

QString Updater::installNote() const
{
    if (s_test)
        return QString();
#ifndef Q_OS_WIN
    return tr("Tự cập nhật chỉ có trên bản Windows (zip từ GitHub).");
#else
    if (currentBuild() <= 0)
        return tr("Bản tự build (dev): cập nhật bằng git pull và build lại.");
    if (!dirWritable(appDir()))
        return tr("Thư mục app không ghi được (%1): giải nén app vào thư mục của bạn, vd Documents.")
            .arg(QDir::toNativeSeparators(appDir()));
    return QString();
#endif
}

bool Updater::autoCheck() const
{
    return QSettings().value(QStringLiteral("update/autoCheck"), true).toBool();
}

void Updater::setAutoCheck(bool on)
{
    if (on == autoCheck())
        return;
    QSettings().setValue(QStringLiteral("update/autoCheck"), on);
    emit autoCheckChanged();
}

void Updater::setState(State s, const QString &error)
{
    m_state = s;
    m_error = error;
    emit stateChanged();
}

void Updater::setProgress(double p, const QString &text)
{
    m_progress = p;
    m_progressText = text;
    emit progressChanged();
}

// ---------------------------------------------------------------------------
// helpers

bool Updater::parseInfo(const QByteArray &json, UpdateInfo *out, QString *error)
{
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &pe);
    if (!doc.isObject()) {
        if (error)
            *error = QStringLiteral("version.json không hợp lệ: %1").arg(pe.errorString());
        return false;
    }
    const QJsonObject o = doc.object();
    UpdateInfo i;
    i.version = o.value(QStringLiteral("version")).toString();
    i.build = o.value(QStringLiteral("build")).toInt();
    i.commit = o.value(QStringLiteral("commit")).toString();
    i.date = o.value(QStringLiteral("date")).toString();
    i.sha256 = o.value(QStringLiteral("sha256")).toString().toLower();
    i.size = qint64(o.value(QStringLiteral("size")).toDouble());
    const QString zip = o.value(QStringLiteral("zip")).toString();
    if (!zip.isEmpty())
        i.zipUrl = QUrl(baseUrl()).resolved(QUrl(zip)).toString();
    const QJsonArray log = o.value(QStringLiteral("log")).toArray();
    for (const QJsonValue &v : log) {
        const QJsonObject e = v.toObject();
        i.log.append({e.value(QStringLiteral("commit")).toString(), e.value(QStringLiteral("subject")).toString()});
    }
    if (!i.valid()) {
        if (error)
            *error = QStringLiteral("version.json thiếu build / zip");
        return false;
    }
    *out = i;
    return true;
}

bool Updater::isNewer(const UpdateInfo &info, int build, const QString &commit)
{
    if (!info.valid() || build <= 0)
        return false;
    if (!commit.isEmpty() && sameCommit(info.commit, commit))
        return false;
    return info.build > build;
}

QStringList Updater::notesSince(const UpdateInfo &info, const QString &commit, int max)
{
    QStringList out;
    bool found = false;
    for (const UpdateInfo::Entry &e : info.log) {
        if (!commit.isEmpty() && sameCommit(e.commit, commit)) {
            found = true;
            break;
        }
        if (out.size() < max)
            out << QStringLiteral("%1  %2").arg(shortSha(e.commit), e.subject);
    }
    if (!found && out.size() >= max)
        out << QStringLiteral("…");
    return out;
}

QString Updater::scriptText()
{
    // waits for the app, copies the new files over it (robocopy retries locked files), restarts it
    return QStringLiteral(R"PS(param([int]$ProcId, [string]$Stage, [string]$Dest, [string]$Log)
function Log([string]$m) { Add-Content -LiteralPath $Log -Value ((Get-Date -Format 'HH:mm:ss') + '  ' + $m) }
Log "script: waiting for the app (pid $ProcId) to close"
$p = Get-Process -Id $ProcId -ErrorAction SilentlyContinue
if ($p) { [void]$p.WaitForExit(60000) }
Start-Sleep -Milliseconds 500
Log "copy $Stage -> $Dest"
$out = & robocopy.exe $Stage $Dest /E /IS /IT /R:20 /W:1 /NP /NFL /NDL /NJH
$code = $LASTEXITCODE
foreach ($l in $out) { if ($l.Trim().Length -gt 0) { Log ('  ' + $l.Trim()) } }
$exe = Join-Path $Dest 'CyberHandStudio.exe'
if ($code -lt 8) {
  Log "done (robocopy $code), starting the app"
  Remove-Item -LiteralPath $Stage -Recurse -Force -ErrorAction SilentlyContinue
  Start-Process -FilePath $exe -WorkingDirectory $Dest -ArgumentList '--updated'
} else {
  Log "FAILED (robocopy $code), the old files that were not replaced stay; starting the app"
  Start-Process -FilePath $exe -WorkingDirectory $Dest -ArgumentList '--update-failed'
}
)PS");
}

// ---------------------------------------------------------------------------
// check / download / extract / install

void Updater::check(bool silent)
{
    if (m_state == Checking || m_state == Downloading || m_state == Extracting)
        return;
    if (!m_net)
        m_net = new QNetworkAccessManager(this);
    QNetworkRequest req(QUrl(baseUrl() + QStringLiteral("version.json")));
    req.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    req.setRawHeader("Cache-Control", "no-cache");
    req.setTransferTimeout(20000);
    setState(Checking);
    m_reply = m_net->get(req);
    connect(m_reply, &QNetworkReply::finished, this, [this, silent]() { onInfoFinished(silent); });
}

void Updater::onInfoFinished(bool silent)
{
    QNetworkReply *r = m_reply;
    m_reply = nullptr;
    if (!r)
        return;
    r->deleteLater();
    if (r->error() != QNetworkReply::NoError) {
        const int http = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString e = http == 404 ? tr("chưa có bản build trên GitHub (đang build lại?), thử lại sau vài phút")
                                      : r->errorString();
        if (silent)
            setState(Idle);
        else
            setState(Error, tr("Không kiểm tra được: %1").arg(e));
        return;
    }
    UpdateInfo info;
    QString err;
    if (!parseInfo(r->readAll(), &info, &err)) {
        silent ? setState(Idle) : setState(Error, err);
        return;
    }
    m_info = info;
    setState(available() ? Available : UpToDate);
    if (available())
        emit message(tr("Có bản mới %1").arg(latestText()), false);
}

void Updater::download()
{
    if (!available() || m_state == Downloading || m_state == Extracting)
        return;
    const QString note = installNote();
    if (!note.isEmpty()) {
        setState(Error, note);
        return;
    }
    QDir().mkpath(workDir());
    m_file.close();
    m_file.setFileName(workDir() + QStringLiteral("/CyberHandStudio-win64.zip"));
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        setState(Error, tr("Không ghi được %1").arg(m_file.fileName()));
        return;
    }
    m_hash.reset();
    if (!m_net)
        m_net = new QNetworkAccessManager(this);
    QNetworkRequest req{QUrl(m_info.zipUrl)};
    req.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    setState(Downloading);
    setProgress(0, tr("Đang tải…"));
    m_reply = m_net->get(req);
    connect(m_reply, &QNetworkReply::readyRead, this, [this]() {
        if (!m_reply)
            return;
        const QByteArray d = m_reply->readAll();
        m_hash.addData(d);
        m_file.write(d);
    });
    connect(m_reply, &QNetworkReply::downloadProgress, this, [this](qint64 got, qint64 total) {
        if (total <= 0)
            total = m_info.size;
        setProgress(total > 0 ? double(got) / total : 0,
                    total > 0 ? tr("Đang tải %1 / %2").arg(mb(got), mb(total)) : tr("Đang tải %1").arg(mb(got)));
    });
    connect(m_reply, &QNetworkReply::finished, this, &Updater::onZipFinished);
}

void Updater::onZipFinished()
{
    QNetworkReply *r = m_reply;
    m_reply = nullptr;
    if (!r)
        return;
    r->deleteLater();
    const QByteArray rest = r->readAll();
    m_hash.addData(rest);
    m_file.write(rest);
    const qint64 size = m_file.size();
    m_file.close();
    if (r->error() != QNetworkReply::NoError) {
        setState(Error, tr("Tải lỗi: %1").arg(r->errorString()));
        return;
    }
    const QString sha = QString::fromLatin1(m_hash.result().toHex());
    if ((m_info.size > 0 && size != m_info.size) || (!m_info.sha256.isEmpty() && sha != m_info.sha256)) {
        QFile::remove(m_file.fileName());
        setState(Error, tr("File tải về không khớp (kích thước / sha256), thử lại"));
        return;
    }
    QString err;
    if (!extract(m_file.fileName(), workDir() + QStringLiteral("/stage"), false, &err))
        setState(Error, err);
}

bool Updater::extract(const QString &zip, const QString &stage, bool wait, QString *error)
{
    QDir(stage).removeRecursively();
    QDir().mkpath(stage);
    QString program;
    QStringList args;
#ifdef Q_OS_WIN
    // Windows 10+ tar (bsdtar) reads zip; not the GNU tar of Git for Windows
    const QString sysTar = qEnvironmentVariable("SystemRoot", QStringLiteral("C:\\Windows")) + QStringLiteral("\\System32\\tar.exe");
    if (QFileInfo::exists(sysTar)) {
        program = sysTar;
        args << QStringLiteral("-xf") << QDir::toNativeSeparators(zip) << QStringLiteral("-C") << QDir::toNativeSeparators(stage);
    } else {
        auto q = [](QString s) { return QLatin1Char('\'') + QDir::toNativeSeparators(s).replace(QLatin1Char('\''), QStringLiteral("''")) + QLatin1Char('\''); };
        program = QStringLiteral("powershell.exe");
        args << QStringLiteral("-NoProfile") << QStringLiteral("-Command")
             << QStringLiteral("Expand-Archive -LiteralPath %1 -DestinationPath %2 -Force").arg(q(zip), q(stage));
    }
#else
    program = QStringLiteral("unzip");
    args << QStringLiteral("-q") << QStringLiteral("-o") << zip << QStringLiteral("-d") << stage;
#endif
    delete m_unzip;
    m_unzip = new QProcess(this);
    m_unzip->setProgram(program);
    m_unzip->setArguments(args);
    m_unzip->setProcessChannelMode(QProcess::MergedChannels);
    if (wait) {
        m_unzip->start();
        if (!m_unzip->waitForStarted(10000) || !m_unzip->waitForFinished(600000)
            || m_unzip->exitStatus() != QProcess::NormalExit || m_unzip->exitCode() != 0
            || !QFileInfo::exists(stage + QLatin1Char('/') + QLatin1String(ExeName))) {
            *error = QStringLiteral("Giải nén lỗi (%1): %2").arg(program, QString::fromLocal8Bit(m_unzip->readAll()).trimmed());
            return false;
        }
        return true;
    }
    setState(Extracting);
    setProgress(1, tr("Đang giải nén…"));
    connect(m_unzip, &QProcess::finished, this, [this](int code, QProcess::ExitStatus st) {
        onExtracted(st == QProcess::NormalExit ? code : -1);
    });
    connect(m_unzip, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            onExtracted(-1);
    });
    m_unzip->start();
    return true;
}

void Updater::onExtracted(int exitCode)
{
    if (m_state != Extracting)
        return;
    const QString stage = workDir() + QStringLiteral("/stage");
    if (exitCode != 0 || !QFileInfo::exists(stage + QLatin1Char('/') + QLatin1String(ExeName))) {
        const QString out = m_unzip ? QString::fromLocal8Bit(m_unzip->readAll()).trimmed() : QString();
        setState(Error, tr("Giải nén lỗi: %1").arg(out.isEmpty() ? m_unzip->program() : out.right(300)));
        return;
    }
    QFile::remove(m_file.fileName());
    setProgress(1, tr("Sẵn sàng"));
    setState(Ready);
}

bool Updater::launchScript(const QString &stage, QString *error)
{
#ifndef Q_OS_WIN
    Q_UNUSED(stage);
    *error = tr("Tự cập nhật chỉ có trên Windows");
    return false;
#else
    const QString dest = appDir();
    if (!dirWritable(dest)) {
        *error = tr("Thư mục app không ghi được: %1").arg(QDir::toNativeSeparators(dest));
        return false;
    }
    QDir().mkpath(workDir());
    const QString script = workDir() + QStringLiteral("/update.ps1");
    QFile f(script);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        *error = tr("Không ghi được %1").arg(script);
        return false;
    }
    f.write(scriptText().toLatin1());
    f.close();
    const QString log = dest + QStringLiteral("/update.log");
    {
        QFile lf(log);
        if (lf.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
            lf.write(QStringLiteral("%1  app %2 -> %3\n")
                         .arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")), currentText(),
                              m_info.valid() ? latestText() : QStringLiteral("zip"))
                         .toUtf8());
    }
    QProcess p;
    p.setProgram(QStringLiteral("powershell.exe"));
    p.setArguments({QStringLiteral("-NoProfile"), QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
                    QStringLiteral("-WindowStyle"), QStringLiteral("Hidden"), QStringLiteral("-File"),
                    QDir::toNativeSeparators(script), QStringLiteral("-ProcId"),
                    QString::number(QCoreApplication::applicationPid()), QStringLiteral("-Stage"),
                    QDir::toNativeSeparators(stage), QStringLiteral("-Dest"), QDir::toNativeSeparators(dest),
                    QStringLiteral("-Log"), QDir::toNativeSeparators(log)});
    p.setWorkingDirectory(workDir());
    p.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *a) {
        a->flags &= ~0x00000010UL; // CREATE_NEW_CONSOLE
        a->flags |= 0x08000000UL;  // CREATE_NO_WINDOW: no console flash
    });
    if (!p.startDetached()) {
        *error = tr("Không chạy được PowerShell: %1").arg(p.errorString());
        return false;
    }
    return true;
#endif
}

bool Updater::installAndRestart()
{
    if (m_state != Ready)
        return false;
    QString err;
    if (!launchScript(workDir() + QStringLiteral("/stage"), &err)) {
        setState(Error, err);
        return false;
    }
    emit message(tr("Đang cập nhật: app đóng, chép file mới rồi tự mở lại"), false);
    // aboutToQuit: motors to duty 0, config saved
    QTimer::singleShot(300, qApp, &QCoreApplication::quit);
    return true;
}

bool Updater::installZipNow(const QString &zip, QString *error)
{
    if (!QFileInfo::exists(zip)) {
        *error = QStringLiteral("Không có file %1").arg(zip);
        return false;
    }
    const QString stage = workDir() + QStringLiteral("/stage");
    if (!extract(zip, stage, true, error))
        return false;
    return launchScript(stage, error);
}

void Updater::openReleasePage()
{
    QDesktopServices::openUrl(QUrl(releasePage()));
}

void Updater::setStartedAfterUpdate(bool ok)
{
    m_startFailed = !ok;
    m_startMessage = ok ? tr("Đã cập nhật: %1").arg(currentText())
                        : tr("Cập nhật chưa xong, xem update.log trong thư mục app (%1)").arg(currentText());
    emit startMessageChanged();
}

void Updater::clearStartMessage()
{
    if (m_startMessage.isEmpty())
        return;
    m_startMessage.clear();
    emit startMessageChanged();
}
