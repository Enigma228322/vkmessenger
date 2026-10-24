#pragma once
#include <QObject>
#include <QJsonObject>
#include <QThread>
#include <functional>

class StoreWorker;

// All SQLite connections and serialization live on the worker thread. Callbacks
// are delivered on this object's thread, in the same order as queued writes.
class LocalStore : public QObject {
    Q_OBJECT
public:
    using Result = std::function<void(const QJsonObject &, const QString &)>;
    explicit LocalStore(const QString &directory = {}, QObject *parent = nullptr);
    ~LocalStore() override;
    QString directory() const;
    void loadAccount(const QString &account, Result result);
    void commitDiff(const QString &account, const QList<QJsonObject> &pages,
                    const QString &version, bool full, Result result);
    void loadHistory(const QString &account, const QString &peer, Result result, qint64 before = 0);
    void saveHistory(const QString &account, const QString &peer, const QJsonArray &messages,
                     const QJsonArray &ranges, bool older, bool newer, Result result = {});
    void saveDraft(const QString &account, const QString &peer, const QJsonObject &draft, Result result = {});
    void loadDrafts(const QString &account, Result result);
    void putOutgoing(const QString &account, const QJsonObject &message, Result result);
    void removeOutgoing(const QString &account, const QString &localId, Result result = {});
    void loadOutgoing(const QString &account, Result result);
    void clearHistory(const QString &account, Result result);
    void statistics(const QString &account, Result result);
    void searchMessages(const QString &account, const QString &text, const QString &peer, int offset, Result result);
    void saveCatalog(const QString &account, const QString &name, const QJsonObject &value, Result result = {});
private:
    void run(std::function<QJsonObject(StoreWorker &)> job, Result result);
    QThread m_thread;
    StoreWorker *m_worker;
};
