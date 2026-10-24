#pragma once
#include "Form.h"
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QQueue>
#include <QSaveFile>
#include <QVariantList>
#include <functional>
#include <memory>
#include <atomic>

class Transfers : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(double progress MEMBER m_progress NOTIFY changed)
    Q_PROPERTY(QString error MEMBER m_error NOTIFY changed)
    Q_PROPERTY(QString filename MEMBER m_filename NOTIFY changed)
    Q_PROPERTY(QVariantList pending MEMBER m_pending NOTIFY changed)
    Q_PROPERTY(bool downloading READ downloading NOTIFY changed)
public:
    using Result = std::function<void(const QJsonValue &, const QString &)>;
    using Api = std::function<void(const QString &, Form, Result)>;
    explicit Transfers(Api api, QObject *parent = nullptr, QNetworkAccessManager *network = nullptr);
    ~Transfers() override;
    bool busy() const { return m_busy; }
    bool downloading() const { return !m_downloadReply.isNull(); }
    Q_INVOKABLE void upload(const QUrl &file, const QString &peer, const QString &kind);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void remove(int index);
    Q_INVOKABLE void download(const QUrl &remote, const QUrl &destination);
    Q_INVOKABLE void cancelDownload();
    Q_INVOKABLE void resume();
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
    QString m_error, m_filename;
    QString m_workspace;
    bool m_paused = false;
    std::shared_ptr<std::atomic_bool> m_stageCancelled;
};
