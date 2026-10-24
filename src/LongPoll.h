#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QTimer>

// Transport only. getDiff owns credentials and the committed server_version.
class LongPoll : public QObject {
    Q_OBJECT
public:
    explicit LongPoll(QObject *parent = nullptr, QNetworkAccessManager *network = nullptr);
    ~LongPoll() override { stop(); }
    void start(const QJsonObject &credentials);
    void stop();
    static QString cursor(const QJsonValue &value);
signals:
    void batch(const QJsonObject &response);
    void resyncRequired();
    void statusChanged(const QString &status);
    void fatalError(const QString &error);
private:
    void poll();
    void recover(const QString &reason);
    QNetworkAccessManager m_defaultNetwork;
    QNetworkAccessManager *m_network;
    QPointer<QNetworkReply> m_reply;
    QTimer m_retry;
    QUrl m_server;
    QString m_key, m_ts;
    quint64 m_generation = 0;
    int m_failures = 0;
    bool m_running = false;
};
