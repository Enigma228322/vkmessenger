#pragma once
#include "Form.h"
#include <QObject>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QTimer>
#include <functional>

class QNetworkReply;

struct ApiResult {
    QJsonValue value;
    QString error;
    int code = 0;
    bool networkFailure = false;
};

class VkApiClient : public QObject {
    Q_OBJECT
public:
    using Callback = std::function<void(const ApiResult &)>;
    explicit VkApiClient(QNetworkAccessManager *network = nullptr, QObject *parent = nullptr);
    ~VkApiClient() override { cancel(); }
    void setToken(const QString &token) { m_token = token; }
    void request(const QString &method, Form params, Callback callback);
    void cancel();
private:
    struct Request { QString method; Form params; Callback callback; int retries = 0; int priority = 1; };
    void pump();
    void perform(Request request);
    QNetworkAccessManager m_defaultNetwork;
    QNetworkAccessManager *m_network;
    QList<Request> m_queue;
    QList<QPointer<QNetworkReply>> m_active;
    QTimer m_pace;
    QString m_token;
    quint64 m_generation = 0;
    int m_userBurst = 0;
};
