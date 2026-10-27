#pragma once
#include "Form.h"
#include <QObject>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QDeadlineTimer>
#include <QTcpServer>
#include <QTimer>
#include <QUrlQuery>
#include <QJsonObject>

class OAuth : public QObject {
    Q_OBJECT
public:
    explicit OAuth(QObject *parent = nullptr, QNetworkAccessManager *network = nullptr);
    ~OAuth() override { cancel(); }
    void start(const QString &clientId, const QString &redirect, const QString &scope);
    bool complete(const QString &callback);
    bool completeAppCallback(const QUrl &callback);
    void refresh(const QJsonObject &session);
    void cancel();
    bool awaitingCallback() const { return !m_state.isEmpty(); }
    static QString randomString();
    static QString challenge(const QString &verifier);
    static bool sameRedirect(const QUrl &actual, const QUrl &expected);
    static bool isAppCallback(const QUrl &url);
signals:
    void authenticated(const QJsonObject &session);
    void failed(const QString &message);
    void callbackRejected(const QString &message);
private slots:
    void expire();
private:
    void exchange(Form parameters, const QString &expectedState);
    QNetworkAccessManager m_defaultNetwork;
    QNetworkAccessManager *m_network;
    QPointer<QNetworkReply> m_reply;
    QTcpServer m_server;
    QTimer m_timeout;
    QDeadlineTimer m_deadline;
    QString m_clientId, m_redirect, m_state, m_verifier;
    QString m_consumedState;
    bool m_exchanging = false;
    quint64 m_generation = 0;
};
