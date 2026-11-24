#pragma once
#include "platform/IdleSleepInhibitor.h"
#include <QHash>
#include <QJsonArray>
#include <QMap>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QVariantMap>

class Messenger;
class QNetworkReply;

class AutoReplyController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool sleepProtected READ sleepProtected NOTIFY settingsChanged)
    Q_PROPERTY(bool checkingModel READ checkingModel NOTIFY checkingModelChanged)
public:
    AutoReplyController(Messenger *messenger, QNetworkAccessManager *network);
    ~AutoReplyController() override;
    Q_INVOKABLE QVariantMap settings(const QString &peer) const;
    Q_INVOKABLE QStringList modelChoices() const;
    Q_INVOKABLE bool hasSavedToken();
    Q_INVOKABLE void editToken(const QString &value);
    Q_INVOKABLE QString saveEditedToken();
    Q_INVOKABLE void checkModel(const QString &model, const QString &key, bool generateSample);
    Q_INVOKABLE void cancelModelCheck();
    Q_INVOKABLE QString enable(const QString &peer, const QString &model, const QString &key);
    Q_INVOKABLE void disable(const QString &peer);
    Q_INVOKABLE void saveNotes(const QString &peer, const QString &notes);
    bool checkingModel() const { return !m_modelCheck.isNull(); }
    bool sleepProtected() const { return bool(m_inhibitor); }
    void setAccount(const QString &account);
    void cancelPendingRequests();
    void observe(const QString &peer, qint64 cmid);
    void cancel(const QString &peer);
    void invalidateNewMessage(const QString &peer, qint64 id, bool isCmid);
signals:
    void settingsChanged();
    void checkingModelChanged();
    void accountChanged();
    void modelChecked(const QString &error, const QString &sample = {});
private:
    struct Job {
        quint64 serial;
        qint64 cmid;
        QPointer<QNetworkReply> reply;
        bool dispatched = false;
        QString status;
    };
    using History = QMap<qint64, QJsonObject>;
    void persist(const QString &peer);
    void updateInhibitor();
    bool writable(const QString &peer) const;
    QByteArray token();
    QString rememberToken(const QByteArray &value);
    QByteArray keySlot() const;
    bool current(const QString &peer, quint64 serial) const;
    void finish(const QString &peer, quint64 serial, const QString &error = {});
    void fetchHistory(const QString &peer, quint64 serial, qint64 cmid);
    void generate(const QString &peer, quint64 serial, const History &history);
    void send(const QString &peer, quint64 serial, const QString &text);

    Messenger *m_messenger;
    QNetworkAccessManager m_defaultNetwork;
    QNetworkAccessManager *m_network;
    QString m_account;
    QHash<QString, QVariantMap> m_settings;
    QByteArray m_key;
    QByteArray m_editedKey;
    QHash<QString, QString> m_errors;
    QHash<QString, Job> m_jobs;
    QPointer<QNetworkReply> m_modelCheck;
    quint64 m_serial = 0;
    std::unique_ptr<IdleSleepInhibitor> m_inhibitor;
};
