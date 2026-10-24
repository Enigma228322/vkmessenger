#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QQueue>
#include <QSettings>
#include <QUrl>

class QNetworkReply;

class MediaCache : public QObject {
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    Q_PROPERTY(qint64 bytes READ bytes NOTIFY changed)
    Q_PROPERTY(qint64 limit READ limit WRITE setLimit NOTIFY changed)
    Q_PROPERTY(int pending READ pending NOTIFY changed)
public:
    explicit MediaCache(const QString &directory = {}, QObject *parent = nullptr, QNetworkAccessManager *network = nullptr);
    ~MediaCache() override;
    void setAccount(const QString &account);
    int revision() const { return m_revision; }
    qint64 bytes() const { return m_bytes; }
    qint64 limit() const { return m_limit; }
    int pending() const { return m_queue.size() + m_active.size(); }
    void setLimit(qint64 bytes);
    Q_INVOKABLE QUrl source(const QUrl &remote);
    Q_INVOKABLE QUrl cached(const QUrl &remote) const;
    Q_INVOKABLE QString error(const QUrl &remote) const { return m_errors.value(remote); }
    Q_INVOKABLE void retry(const QUrl &remote);
    Q_INVOKABLE void clear();
signals:
    void changed();
private:
    struct Entry { QString path; qint64 size = 0, accessed = 0; };
    QString path(const QUrl &remote) const;
    void pump();
    void trim();
    void scan();
    void stop();
    QSettings m_settings;
    QString m_root, m_accountDirectory;
    QNetworkAccessManager m_defaultNetwork;
    QNetworkAccessManager *m_network;
    QQueue<QUrl> m_queue;
    QHash<QUrl, QPointer<QNetworkReply>> m_active;
    QSet<QUrl> m_waiting;
    QHash<QUrl, QString> m_errors;
    QHash<QString, Entry> m_entries;
    QSet<QString> m_accessed;
    qint64 m_bytes = 0, m_limit = 1073741824;
    quint64 m_generation = 0;
    int m_revision = 0;
};
