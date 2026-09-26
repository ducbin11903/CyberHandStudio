#pragma once

#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>

class HandModel;
#include <QNetworkReply>

// Asks an LLM to write a joint trajectory as CSV (t,T2,T1,I,M,R,P in degrees).
class LlmClient : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int provider READ provider WRITE setProvider NOTIFY settingsChanged)
    Q_PROPERTY(QString endpoint READ endpoint WRITE setEndpoint NOTIFY settingsChanged)
    Q_PROPERTY(QString model READ model WRITE setModel NOTIFY settingsChanged)
    Q_PROPERTY(QString apiKey READ apiKey WRITE setApiKey NOTIFY settingsChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(QString lastReply READ lastReply NOTIFY lastReplyChanged)
    Q_PROPERTY(QStringList providerNames READ providerNames CONSTANT)

public:
    enum Provider { Anthropic = 0, OpenAICompatible = 1 };
    Q_ENUM(Provider)

    explicit LlmClient(HandModel *hand, QObject *parent = nullptr);

    int provider() const { return m_provider; }
    void setProvider(int p);
    QString endpoint() const { return m_endpoint; }
    void setEndpoint(const QString &e) { m_endpoint = e.trimmed(); emit settingsChanged(); }
    QString model() const { return m_model; }
    void setModel(const QString &m) { m_model = m.trimmed(); emit settingsChanged(); }
    QString apiKey() const { return m_key; }
    void setApiKey(const QString &k) { m_key = k.trimmed(); emit settingsChanged(); }
    bool busy() const { return m_reply != nullptr; }
    QString lastError() const { return m_error; }
    QString lastReply() const { return m_lastReply; }
    QStringList providerNames() const;

    Q_INVOKABLE bool generate(const QString &prompt, double durationHint = 0);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE QString systemPrompt() const;

    QJsonObject toJson() const;
    void fromJson(const QJsonObject &o);

    // Pulls the CSV block out of a model reply (code fence or raw lines).
    static QString extractCsv(const QString &reply);

signals:
    void settingsChanged();
    void busyChanged();
    void lastErrorChanged();
    void lastReplyChanged();
    void trajectoryReady(const QString &csv, const QString &prompt);

private:
    void onFinished();
    void setError(const QString &e);

    HandModel *m_hand;
    QNetworkAccessManager m_net;
    QPointer<QNetworkReply> m_reply;
    int m_provider = Anthropic;
    QString m_endpoint = QStringLiteral("https://api.anthropic.com/v1/messages");
    QString m_model = QStringLiteral("claude-sonnet-5");
    QString m_key;
    QString m_error;
    QString m_lastReply;
    QString m_prompt;
};
