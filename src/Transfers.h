#pragma once
#include "Form.h"
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QQueue>
#include <QSaveFile>
#include <QVariantList>
#include <QTimer>
#include <functional>
#include <memory>
#include <atomic>

class Transfers : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(double progress MEMBER m_progress NOTIFY progressChanged)
    Q_PROPERTY(qint64 sentBytes MEMBER m_sentBytes NOTIFY progressChanged)
    Q_PROPERTY(qint64 totalBytes MEMBER m_totalBytes NOTIFY progressChanged)
    Q_PROPERTY(QString error READ error WRITE setError NOTIFY changed)
    Q_PROPERTY(QString uploadError MEMBER m_error NOTIFY changed)
    Q_PROPERTY(QString filename MEMBER m_filename NOTIFY changed)
    Q_PROPERTY(QString status MEMBER m_status NOTIFY changed)
    Q_PROPERTY(bool canRetry READ canRetry NOTIFY changed)
    Q_PROPERTY(QVariantList pending MEMBER m_pending NOTIFY pendingChanged)
    Q_PROPERTY(bool downloading READ downloading NOTIFY changed)
    Q_PROPERTY(int queuedCount READ queuedCount NOTIFY changed)
    Q_PROPERTY(bool uploadCanRetry READ uploadCanRetry NOTIFY changed)
    Q_PROPERTY(QString downloadError MEMBER m_downloadError NOTIFY changed)
    Q_PROPERTY(QString downloadFilename READ downloadFilename NOTIFY changed)
    Q_PROPERTY(bool downloadCanRetry MEMBER m_downloadFailed NOTIFY changed)
    Q_PROPERTY(double downloadProgress READ downloadProgress NOTIFY downloadProgressChanged)
    Q_PROPERTY(qint64 receivedBytes MEMBER m_receivedBytes NOTIFY downloadProgressChanged)
    Q_PROPERTY(qint64 downloadTotalBytes MEMBER m_downloadTotalBytes NOTIFY downloadProgressChanged)
public:
    using Result = std::function<void(const QJsonValue &, const QString &)>;
    using Api = std::function<void(const QString &, Form, Result)>;
    explicit Transfers(Api api, QObject *parent = nullptr, QNetworkAccessManager *network = nullptr);
    ~Transfers() override;
    bool busy() const { return m_busy; }
    bool downloading() const { return !m_downloadReply.isNull(); }
    QString error() const { return m_error.isEmpty() ? m_downloadError : m_error; }
    void setError(const QString &value) { if (m_error != value) { m_error = value; emit changed(); } }
    int queuedCount() const { return m_queue.size() + (m_busy ? 1 : 0); }
    bool uploadCanRetry() const { return m_paused && !m_queue.isEmpty(); }
    QString downloadFilename() const { return m_downloadDestination.fileName(); }
    double downloadProgress() const { return m_downloadTotalBytes > 0 ? qBound(0., double(m_receivedBytes) / m_downloadTotalBytes, 1.) : 0.; }
    bool canRetry() const { return (m_paused && !m_queue.isEmpty()) || m_downloadFailed; }
    Q_INVOKABLE void upload(const QUrl &file, const QString &peer, const QString &kind);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void remove(int index);
    Q_INVOKABLE void download(const QUrl &remote, const QUrl &destination);
    Q_INVOKABLE void cancelDownload();
    Q_INVOKABLE void resume();
    Q_INVOKABLE void retry();
    Q_INVOKABLE void retryUpload() { resume(); }
    Q_INVOKABLE void retryDownload();
    bool adoptRecording(const QUrl &file, const QString &peer);
    void pause();
    void setWorkspace(const QString &path) { m_workspace = path; }
    QJsonObject savedState() const;
    void restoreState(const QJsonObject &state);
    QStringList attachments(const QString &peer) const;
    void acknowledge(const QString &peer, const QStringList &attachments);
    void reset();
signals:
    void changed();
    void progressChanged();
    void downloadProgressChanged();
    void stateChanged();
    void pendingChanged();
    void chatPhotoChanged();
    void downloadFinished(const QUrl &file);
private:
    struct Job { QUrl file; QString peer, kind, name; bool managed = false; };
    void next();
    void stage();
    void requestServer();
    void post(const QJsonObject &server, quint64 generation);
    void save(const QJsonObject &object, quint64 generation);
    void finish(const QString &attachment);
    void fail(const QString &error);
    Api m_api;
    QNetworkAccessManager m_defaultNetwork;
    QNetworkAccessManager *m_network;
    QPointer<QNetworkReply> m_reply, m_downloadReply;
    std::unique_ptr<QSaveFile> m_downloadFile;
    QQueue<Job> m_queue;
    Job m_job;
    QJsonObject m_server;
    QVariantList m_pending;
    quint64 m_generation = 0, m_downloadGeneration = 0;
    bool m_busy = false;
    double m_progress = 0;
    qint64 m_sentBytes = 0, m_totalBytes = -1, m_receivedBytes = 0, m_downloadTotalBytes = -1;
    QTimer m_progressTimer, m_downloadProgressTimer;
    QString m_error, m_filename, m_status;
    int m_postRetries = 0;
    QUrl m_downloadRemote, m_downloadDestination;
    QString m_downloadError;
    bool m_downloadFailed = false;
    QString m_workspace;
    bool m_paused = false;
    std::shared_ptr<std::atomic_bool> m_stageCancelled;
};
