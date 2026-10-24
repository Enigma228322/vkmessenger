#pragma once
#include "Form.h"
#include <QObject>
#include <QNetworkAccessManager>
#include <QTcpServer>
#include <QTimer>
#include <QUrlQuery>
#include <QJsonObject>

class OAuth : public QObject {
    Q_OBJECT
public:
    explicit OAuth(QObject *parent = nullptr);
    void start(const QString &clientId, const QString &redirect, const QString &scope);
    void complete(const QString &callback);
    void refresh(const QJsonObject &session);
    void cancel();
    static QString randomString();
    static QString challenge(const QString &verifier);
    static bool sameRedirect(const QUrl &actual, const QUrl &expected);
signals:
    void authenticated(const QJsonObject &session);
    void failed(const QString &message);
private:
    void exchange(Form parameters, const QString &expectedState);
    QNetworkAccessManager m_network;
    QTcpServer m_server;
    QTimer m_timeout;
    QString m_clientId, m_redirect, m_state, m_verifier;
    quint64 m_generation = 0;
};
