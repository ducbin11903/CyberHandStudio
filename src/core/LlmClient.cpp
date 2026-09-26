#include "LlmClient.h"

#include "HandModel.h"
#include "LineParser.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>

LlmClient::LlmClient(HandModel *hand, QObject *parent)
    : QObject(parent), m_hand(hand)
{
}

void LlmClient::setProvider(int p)
{
    p = qBound(0, p, 1);
    if (p == m_provider)
        return;
    m_provider = p;
    if (p == Anthropic && !m_endpoint.contains(QLatin1String("anthropic")))
        m_endpoint = QStringLiteral("https://api.anthropic.com/v1/messages");
    if (p == OpenAICompatible && m_endpoint.contains(QLatin1String("anthropic")))
        m_endpoint = QStringLiteral("https://api.openai.com/v1/chat/completions");
    emit settingsChanged();
}

QStringList LlmClient::providerNames() const
{
    return {QStringLiteral("Anthropic (Claude)"), QStringLiteral("OpenAI-compatible")};
}

void LlmClient::setError(const QString &e)
{
    m_error = e;
    emit lastErrorChanged();
}

QString LlmClient::systemPrompt() const
{
    QString joints;
    for (int i = 0; i < m_hand->count(); ++i) {
        const Joint *j = m_hand->joint(i);
        joints += QStringLiteral("- %1 (%2): %3..%4 deg\n")
                      .arg(j->key(), j->name(), LineParser::num(j->minDeg()), LineParser::num(j->maxDeg()));
    }
    return QStringLiteral(
               "You write motion trajectories for CyberHand, a right robotic hand with 6 actuated joints.\n"
               "Joints (0 deg = fully extended/open, larger = more flexed/closed):\n%1"
               "T2 rotates the thumb across the palm (opposition), T1 flexes the thumb.\n"
               "Motors are slow geared DC motors: keep each joint below about 60 deg/s and avoid instant jumps.\n\n"
               "Reply with ONLY a CSV block, no prose:\n"
               "```csv\nt,T2,T1,I,M,R,P\n0,0,0,0,0,0,0\n...\n```\n"
               "Rules: t in seconds starting at 0, strictly increasing, 10-80 rows; one keyframe every 0.1-0.5 s;"
               " every value inside its joint range; start and end in the open pose unless asked otherwise.")
        .arg(joints);
}

bool LlmClient::generate(const QString &prompt, double durationHint)
{
    if (busy())
        return false;
    if (m_key.isEmpty() && !m_endpoint.startsWith(QLatin1String("http://localhost"))
        && !m_endpoint.startsWith(QLatin1String("http://127.0.0.1"))) {
        setError(tr("Chưa nhập API key"));
        return false;
    }
    QString user = prompt.trimmed();
    if (user.isEmpty()) {
        setError(tr("Chưa nhập mô tả chuyển động"));
        return false;
    }
    if (durationHint > 0)
        user += QStringLiteral("\nTotal duration about %1 s.").arg(LineParser::num(durationHint));
    m_prompt = prompt;

    QNetworkRequest req{QUrl(m_endpoint)};
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setTransferTimeout(90000);
    QJsonObject body;
    if (m_provider == Anthropic) {
        req.setRawHeader("x-api-key", m_key.toUtf8());
        req.setRawHeader("anthropic-version", "2023-06-01");
        body = {{QStringLiteral("model"), m_model},
                {QStringLiteral("max_tokens"), 4000},
                {QStringLiteral("system"), systemPrompt()},
                {QStringLiteral("messages"), QJsonArray{QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                                                                    {QStringLiteral("content"), user}}}}};
    } else {
        if (!m_key.isEmpty())
            req.setRawHeader("Authorization", "Bearer " + m_key.toUtf8());
        body = {{QStringLiteral("model"), m_model},
                {QStringLiteral("messages"), QJsonArray{
                     QJsonObject{{QStringLiteral("role"), QStringLiteral("system")}, {QStringLiteral("content"), systemPrompt()}},
                     QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), user}}}}};
    }
    setError(QString());
    m_reply = m_net.post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(m_reply, &QNetworkReply::finished, this, &LlmClient::onFinished);
    emit busyChanged();
    return true;
}

void LlmClient::cancel()
{
    if (m_reply)
        m_reply->abort();
}

void LlmClient::onFinished()
{
    QNetworkReply *reply = m_reply;
    if (!reply)
        return;
    reply->deleteLater();
    m_reply = nullptr;
    emit busyChanged();

    const QByteArray data = reply->readAll();
    const QJsonDocument doc = QJsonDocument::fromJson(data);
    if (reply->error() != QNetworkReply::NoError) {
        QString msg = reply->errorString();
        const QString apiMsg = doc.object().value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString();
        if (!apiMsg.isEmpty())
            msg = apiMsg;
        setError(msg);
        return;
    }
    QString text;
    const QJsonObject root = doc.object();
    if (root.contains(QStringLiteral("content"))) {
        for (const QJsonValue &c : root.value(QStringLiteral("content")).toArray())
            if (c.toObject().value(QStringLiteral("type")).toString() == QLatin1String("text"))
                text += c.toObject().value(QStringLiteral("text")).toString();
    } else if (root.contains(QStringLiteral("choices"))) {
        text = root.value(QStringLiteral("choices")).toArray().at(0).toObject()
                   .value(QStringLiteral("message")).toObject().value(QStringLiteral("content")).toString();
    }
    m_lastReply = text;
    emit lastReplyChanged();
    const QString csv = extractCsv(text);
    if (csv.isEmpty()) {
        setError(tr("Phản hồi không có CSV quỹ đạo"));
        return;
    }
    emit trajectoryReady(csv, m_prompt);
}

QString LlmClient::extractCsv(const QString &reply)
{
    static const QRegularExpression fence(QStringLiteral("```(?:csv)?\\s*\\n([\\s\\S]*?)```"));
    const auto m = fence.match(reply);
    QString body = m.hasMatch() ? m.captured(1) : reply;
    QStringList keep;
    for (const QString &line : body.split(QLatin1Char('\n'))) {
        const QString t = line.trimmed();
        if (t.isEmpty())
            continue;
        if (t.startsWith(QLatin1Char('t'), Qt::CaseInsensitive) && t.contains(QLatin1Char(','))) {
            keep << t; // header
            continue;
        }
        if (t.at(0).isDigit() && t.contains(QLatin1Char(',')))
            keep << t;
    }
    return keep.size() >= 2 ? keep.join(QLatin1Char('\n')) : QString();
}

QJsonObject LlmClient::toJson() const
{
    return {{QStringLiteral("provider"), m_provider}, {QStringLiteral("endpoint"), m_endpoint},
            {QStringLiteral("model"), m_model}, {QStringLiteral("apiKey"), m_key}};
}

void LlmClient::fromJson(const QJsonObject &o)
{
    m_provider = o.value(QStringLiteral("provider")).toInt(m_provider);
    m_endpoint = o.value(QStringLiteral("endpoint")).toString(m_endpoint);
    m_model = o.value(QStringLiteral("model")).toString(m_model);
    m_key = o.value(QStringLiteral("apiKey")).toString(m_key);
    emit settingsChanged();
}
