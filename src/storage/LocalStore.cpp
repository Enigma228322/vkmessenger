#include "LocalStore.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QSet>
#include <QRegularExpression>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QUuid>
#include <stdexcept>

namespace {
QString number(const QJsonValue &value) { return value.isString() ? value.toString() : QString::number(value.toInteger()); }
QByteArray bytes(const QJsonObject &value) { return QJsonDocument(value).toJson(QJsonDocument::Compact); }
QJsonObject object(const QVariant &value) { return QJsonDocument::fromJson(value.toByteArray()).object(); }
}

class StoreWorker : public QObject {
public:
    QString directory, cacheName, protectedName;
    QSqlDatabase cache, protectedData;
    bool initialized = false;
    bool fullTextSearch = false;
    explicit StoreWorker(QString path) : directory(std::move(path)) {}
    ~StoreWorker() override {
        cache.close(); protectedData.close();
        cache = {}; protectedData = {};
        QSqlDatabase::removeDatabase(cacheName); QSqlDatabase::removeDatabase(protectedName);
    }
    static QSqlQuery query(QSqlDatabase db, const QString &sql, const QVariantList &args = {}) {
        QSqlQuery q(db);
        if (!q.prepare(sql)) throw std::runtime_error(q.lastError().text().toStdString());
        for (const auto &arg : args) q.addBindValue(arg);
        if (!q.exec()) throw std::runtime_error(q.lastError().text().toStdString());
        return q;
    }
    void open() {
        if (initialized) return;
        if (!QDir().mkpath(directory)) throw std::runtime_error("Не удалось создать хранилище приложения.");
        QFile::setPermissions(directory, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        auto connection = [this](QSqlDatabase &db, QString &name, const QString &file) {
            if (name.isEmpty()) {
                name = QUuid::createUuid().toString(QUuid::WithoutBraces);
                db = QSqlDatabase::addDatabase("QSQLITE", name);
                db.setDatabaseName(directory + '/' + file);
            }
            if (!db.open()) throw std::runtime_error(db.lastError().text().toStdString());
            QFile::setPermissions(db.databaseName(), QFileDevice::ReadOwner | QFileDevice::WriteOwner);
            query(db, "PRAGMA journal_mode=WAL");
            query(db, "PRAGMA synchronous=FULL");
            auto version = query(db, "PRAGMA user_version");
            if (version.next() && version.value(0).toInt() > 1)
                throw std::runtime_error("Хранилище создано более новой версией приложения.");
        };
        connection(cache, cacheName, "history.sqlite");
        connection(protectedData, protectedName, "personal.sqlite");
        query(cache, "CREATE TABLE IF NOT EXISTS entities(account TEXT,kind TEXT,id TEXT,data BLOB,PRIMARY KEY(account,kind,id))");
        query(cache, "CREATE TABLE IF NOT EXISTS messages(account TEXT,peer TEXT,cmid INTEGER,data BLOB,dirty INTEGER DEFAULT 0,PRIMARY KEY(account,peer,cmid))");
        query(cache, "CREATE TABLE IF NOT EXISTS history_meta(account TEXT,peer TEXT,data BLOB,PRIMARY KEY(account,peer))");
        query(cache, "CREATE TABLE IF NOT EXISTS sync_state(account TEXT PRIMARY KEY,version TEXT,unread INTEGER DEFAULT 0)");
        query(cache, "CREATE TABLE IF NOT EXISTS cache_metadata(key TEXT PRIMARY KEY,value TEXT)");
        auto indexState = query(cache, "SELECT value FROM cache_metadata WHERE key='fts_version'");
        const bool indexed = indexState.next() && indexState.value(0).toString() == "1";
        indexState.finish();
        try {
            query(cache, "CREATE VIRTUAL TABLE IF NOT EXISTS message_search USING fts5(account UNINDEXED,peer UNINDEXED,cmid UNINDEXED,text,tokenize='unicode61')");
            fullTextSearch = true;
            if (!indexed) transaction(cache, [&] {
                query(cache, "DELETE FROM message_search");
                auto rows = query(cache, "SELECT rowid,account,peer,cmid,data FROM messages");
                while (rows.next()) query(cache, "INSERT INTO message_search(rowid,account,peer,cmid,text) VALUES(?,?,?,?,?)", {rows.value(0), rows.value(1), rows.value(2), rows.value(3), object(rows.value(4)).value("text").toString()});
                query(cache, "INSERT OR REPLACE INTO cache_metadata VALUES('fts_version','1')");
            });
        } catch (const std::exception &) { fullTextSearch = false; }
        query(protectedData, "CREATE TABLE IF NOT EXISTS drafts(account TEXT,peer TEXT,data BLOB,PRIMARY KEY(account,peer))");
        query(protectedData, "CREATE TABLE IF NOT EXISTS outbox(account TEXT,id TEXT,data BLOB,created INTEGER,PRIMARY KEY(account,id))");
        query(cache, "PRAGMA user_version=1"); query(protectedData, "PRAGMA user_version=1");
        initialized = true;
    }
    template<typename F> void transaction(QSqlDatabase db, F body) {
        if (!db.transaction()) throw std::runtime_error(db.lastError().text().toStdString());
        try {
            body();
            if (!db.commit()) throw std::runtime_error(db.lastError().text().toStdString());
        } catch (...) { db.rollback(); throw; }
    }
    void message(const QString &account, const QString &peer, const QJsonObject &msg) {
        const auto cmid = msg.value("conversation_message_id").toInteger();
        if (cmid <= 0) return;
        auto normalized = msg;
        if (!normalized.contains("peer_id")) normalized.insert("peer_id", peer.toLongLong());
        if (fullTextSearch) query(cache, "DELETE FROM message_search WHERE rowid IN (SELECT rowid FROM messages WHERE account=? AND peer=? AND cmid=?)", {account, peer, cmid});
        const auto inserted = query(cache, "INSERT OR REPLACE INTO messages VALUES(?,?,?,?,0)", {account, peer, cmid, bytes(normalized)});
        if (fullTextSearch && !msg.value("deleted").toBool() && msg.value("deleted").toInt() != 1)
            query(cache, "INSERT INTO message_search(rowid,account,peer,cmid,text) VALUES(?,?,?,?,?)", {inserted.lastInsertId(), account, peer, cmid, msg.value("text").toString()});
    }
    void trimHistory() {
        // Protected data is in another database and never participates in eviction.
        for (;;) {
            auto size = query(cache, "SELECT COUNT(*),COALESCE(SUM(LENGTH(data)),0) FROM messages");
            if (!size.next() || (size.value(0).toLongLong() <= 100000 && size.value(1).toLongLong() <= 512 * 1048576)) return;
            QSet<QPair<QString, QString>> peers;
            auto oldest = query(cache, "SELECT account,peer FROM messages ORDER BY rowid LIMIT 1000");
            while (oldest.next()) peers.insert({oldest.value(0).toString(), oldest.value(1).toString()});
            for (const auto &peer : peers) query(cache, "DELETE FROM history_meta WHERE account=? AND peer=?", {peer.first, peer.second});
            if (fullTextSearch) query(cache, "DELETE FROM message_search WHERE rowid IN (SELECT rowid FROM messages ORDER BY rowid LIMIT 1000)");
            query(cache, "DELETE FROM messages WHERE rowid IN (SELECT rowid FROM messages ORDER BY rowid LIMIT 1000)");
        }
    }
};

LocalStore::LocalStore(const QString &directory, QObject *parent) : QObject(parent),
    m_worker(new StoreWorker(directory.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/storage" : directory))
{
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_thread.start();
}

LocalStore::~LocalStore()
{
    // Drain accepted writes before leaving; never abandon an acknowledged draft.
    QMetaObject::invokeMethod(m_worker, [] {}, Qt::BlockingQueuedConnection);
    m_thread.quit(); m_thread.wait();
}
QString LocalStore::directory() const { return m_worker->directory; }

void LocalStore::run(std::function<QJsonObject(StoreWorker &)> job, Result result)
{
    const QPointer<LocalStore> guard(this);
    QMetaObject::invokeMethod(m_worker, [guard, worker = m_worker, job = std::move(job), result = std::move(result)] {
        QJsonObject value; QString error;
        try { worker->open(); value = job(*worker); }
        catch (const std::exception &e) { error = QString::fromUtf8(e.what()); }
        if (guard && result) QMetaObject::invokeMethod(guard, [guard, result, value, error] { if (guard) result(value, error); });
    });
}

void LocalStore::loadAccount(const QString &account, Result result)
{
    run([account](StoreWorker &w) {
        QJsonObject conversations, peers, catalogs;
        auto rows = w.query(w.cache, "SELECT kind,id,data FROM entities WHERE account=?", {account});
        while (rows.next()) {
            const auto kind = rows.value(0).toString();
            (kind == "conversation" ? conversations : kind == "catalog" ? catalogs : peers).insert(rows.value(1).toString(), object(rows.value(2)));
        }
        auto state = w.query(w.cache, "SELECT version,unread FROM sync_state WHERE account=?", {account});
        QJsonObject value{{"conversations", conversations}, {"peers", peers}, {"catalogs", catalogs}};
        if (state.next()) { value.insert("version", state.value(0).toString()); value.insert("unread", state.value(1).toInt()); }
        return value;
    }, std::move(result));
}

void LocalStore::commitDiff(const QString &account, const QList<QJsonObject> &pages, const QString &version, bool full, Result result)
{
    run([account, pages, version, full](StoreWorker &w) {
        w.transaction(w.cache, [&] {
            if (full) {
                if (w.fullTextSearch) w.query(w.cache, "DELETE FROM message_search WHERE account=?", {account});
                w.query(w.cache, "DELETE FROM entities WHERE account=? AND kind!='catalog'", {account});
                for (auto table : {"messages", "history_meta", "sync_state"}) w.query(w.cache, "DELETE FROM " + QString(table) + " WHERE account=?", {account});
            }
            int unread = 0;
            auto old = w.query(w.cache, "SELECT unread FROM sync_state WHERE account=?", {account});
            if (old.next()) unread = old.value(0).toInt();
            for (const auto &page : pages) {
                if (page.value("counters").toObject().contains("messages")) unread = page.value("counters").toObject().value("messages").toInt();
                for (auto kind : {"profiles", "contacts", "groups"}) for (auto value : page.value(kind).toArray()) {
                    auto entity = value.toObject();
                    auto peer = number(entity.value("id"));
                    if (QString(kind) == "groups") peer = QString::number(-entity.value("id").toInteger());
                    w.query(w.cache, "INSERT OR REPLACE INTO entities VALUES(?,?,?,?)", {account, "peer", peer, bytes(entity)});
                }
                for (auto value : page.value("conversations_info").toArray()) {
                    auto info = value.toObject();
                    auto convo = info.value("conversation").toObject();
                    auto peer = number(convo.value("peer").toObject().value("id"));
                    if (convo.isEmpty() || peer == "0") continue;
                    w.query(w.cache, "INSERT OR REPLACE INTO entities VALUES(?,?,?,?)", {account, "conversation", peer, bytes(info)});
                    if (info.value("invalidate").toBool() || info.value("invalidate").toInt() == 1) {
                        if (w.fullTextSearch) w.query(w.cache, "DELETE FROM message_search WHERE account=? AND peer=?", {account, peer});
                        w.query(w.cache, "DELETE FROM messages WHERE account=? AND peer=?", {account, peer});
                        w.query(w.cache, "DELETE FROM history_meta WHERE account=? AND peer=?", {account, peer});
                    }
                    for (auto flagValue : info.value("cmids_flags").toArray()) {
                        auto flag = flagValue.toObject();
                        if (w.fullTextSearch && (flag.value("updated_flags").toInt() & 1))
                            w.query(w.cache, "DELETE FROM message_search WHERE rowid IN (SELECT rowid FROM messages WHERE account=? AND peer=? AND cmid=?)", {account, peer, flag.value("cmid").toInteger()});
                        w.query(w.cache, flag.value("updated_flags").toInt() & 1
                            ? "DELETE FROM messages WHERE account=? AND peer=? AND cmid=?"
                            : "UPDATE messages SET dirty=1 WHERE account=? AND peer=? AND cmid=?",
                            {account, peer, flag.value("cmid").toInteger()});
                    }
                    for (auto cmid : info.value("cmids_updated_reactions").toArray())
                        w.query(w.cache, "UPDATE messages SET dirty=1 WHERE account=? AND peer=? AND cmid=?", {account, peer, cmid.toInteger()});
                    for (auto key : {"range_deleted_cmids", "range_updated_cmids"}) for (auto rangeValue : info.value(key).toArray()) {
                        auto range = rangeValue.toObject();
                        if (w.fullTextSearch && QString(key) == "range_deleted_cmids")
                            w.query(w.cache, "DELETE FROM message_search WHERE rowid IN (SELECT rowid FROM messages WHERE account=? AND peer=? AND cmid BETWEEN ? AND ?)", {account, peer, range.value("min").toInteger(), range.value("max").toInteger()});
                        w.query(w.cache, (QString(key) == "range_deleted_cmids" ? "DELETE FROM messages" : "UPDATE messages SET dirty=1")
                            + QString(" WHERE account=? AND peer=? AND cmid BETWEEN ? AND ?"),
                            {account, peer, range.value("min").toInteger(), range.value("max").toInteger()});
                    }
                    for (auto msg : info.value("message").toArray()) w.message(account, peer, msg.toObject());
                }
            }
            w.query(w.cache, "INSERT OR REPLACE INTO sync_state VALUES(?,?,?)", {account, version, unread});
            w.trimHistory();
        });
        return QJsonObject();
    }, std::move(result));
}

void LocalStore::loadHistory(const QString &account, const QString &peer, Result result, qint64 before)
{
    run([account, peer, before](StoreWorker &w) {
        auto rows = w.query(w.cache, "SELECT data,dirty,cmid FROM messages WHERE account=? AND peer=? AND (?=0 OR cmid<?) ORDER BY cmid DESC LIMIT 200", {account, peer, before, before});
        QJsonArray messages, dirty;
        while (rows.next()) { messages.prepend(object(rows.value(0))); if (rows.value(1).toBool()) dirty.append(rows.value(2).toLongLong()); }
        QJsonObject value;
        auto meta = w.query(w.cache, "SELECT data FROM history_meta WHERE account=? AND peer=?", {account, peer});
        if (meta.next()) value = object(meta.value(0));
        value.insert("messages", messages); value.insert("dirty", dirty);
        if (messages.size() == 200) value.insert("older", true);
        return value;
    }, std::move(result));
}

void LocalStore::saveHistory(const QString &account, const QString &peer, const QJsonArray &messages, const QJsonArray &ranges, bool older, bool newer, Result result)
{
    run([account, peer, messages, ranges, older, newer](StoreWorker &w) {
        w.transaction(w.cache, [&] {
            for (auto msg : messages) w.message(account, peer, msg.toObject());
            w.query(w.cache, "INSERT OR REPLACE INTO history_meta VALUES(?,?,?)",
                {account, peer, bytes({{"ranges", ranges}, {"older", older}, {"newer", newer}})});
            w.trimHistory();
        });
        return QJsonObject();
    }, std::move(result));
}

void LocalStore::saveDraft(const QString &account, const QString &peer, const QJsonObject &draft, Result result)
{
    run([account, peer, draft](StoreWorker &w) {
        if (draft.isEmpty()) w.query(w.protectedData, "DELETE FROM drafts WHERE account=? AND peer=?", {account, peer});
        else w.query(w.protectedData, "INSERT OR REPLACE INTO drafts VALUES(?,?,?)", {account, peer, bytes(draft)});
        return QJsonObject();
    }, std::move(result));
}

void LocalStore::loadDrafts(const QString &account, Result result)
{
    run([account](StoreWorker &w) {
        QJsonObject drafts;
        auto rows = w.query(w.protectedData, "SELECT peer,data FROM drafts WHERE account=?", {account});
        while (rows.next()) drafts.insert(rows.value(0).toString(), object(rows.value(1)));
        return drafts;
    }, std::move(result));
}

void LocalStore::putOutgoing(const QString &account, const QJsonObject &message, Result result)
{
    run([account, message](StoreWorker &w) {
        w.query(w.protectedData, "INSERT OR REPLACE INTO outbox VALUES(?,?,?,?)",
            {account, message.value("local_id").toString(), bytes(message), message.value("created_order").toInteger(message.value("created").toInteger() * 1000)});
        return QJsonObject();
    }, std::move(result));
}
void LocalStore::removeOutgoing(const QString &account, const QString &localId, Result result)
{
    run([account, localId](StoreWorker &w) {
        w.query(w.protectedData, "DELETE FROM outbox WHERE account=? AND id=?", {account, localId}); return QJsonObject();
    }, std::move(result));
}
void LocalStore::loadOutgoing(const QString &account, Result result)
{
    run([account](StoreWorker &w) {
        QJsonArray messages;
        auto rows = w.query(w.protectedData, "SELECT data FROM outbox WHERE account=? ORDER BY created,id", {account});
        while (rows.next()) messages.append(object(rows.value(0)));
        return QJsonObject{{"items", messages}};
    }, std::move(result));
}
void LocalStore::clearHistory(const QString &account, Result result)
{
    run([account](StoreWorker &w) {
        w.transaction(w.cache, [&] {
            if (w.fullTextSearch) w.query(w.cache, "DELETE FROM message_search WHERE account=?", {account});
            for (auto table : {"entities", "messages", "history_meta", "sync_state"})
                w.query(w.cache, "DELETE FROM " + QString(table) + " WHERE account=?", {account});
        });
        w.query(w.cache, "PRAGMA wal_checkpoint(TRUNCATE)");
        w.query(w.cache, "VACUUM");
        return QJsonObject();
    }, std::move(result));
}
void LocalStore::statistics(const QString &account, Result result)
{
    run([account](StoreWorker &w) {
        QJsonObject value;
        for (auto table : {"messages", "entities"}) {
            auto q = w.query(w.cache, "SELECT COUNT(*),COALESCE(SUM(LENGTH(data)),0) FROM " + QString(table) + " WHERE account=?", {account});
            if (q.next()) { value.insert(QString(table) + "Count", q.value(0).toLongLong()); value.insert(QString(table) + "Bytes", q.value(1).toLongLong()); }
        }
        auto q = w.query(w.protectedData, "SELECT COUNT(*) FROM outbox WHERE account=?", {account});
        if (q.next()) value.insert("outgoingCount", q.value(0).toInt());
        return value;
    }, std::move(result));
}

void LocalStore::searchMessages(const QString &account, const QString &text, const QString &peer, int offset, Result result)
{
    run([account, text, peer, offset](StoreWorker &w) {
        QJsonArray messages, profiles;
        const auto queryText = text.trimmed().left(256);
        if (queryText.isEmpty()) return QJsonObject{{"items", messages}};
        if (w.fullTextSearch) {
            QStringList tokens;
            for (auto token : queryText.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts)) {
                token.replace('"', "\"\""); tokens.append('"' + token + "\"*");
            }
            auto rows = w.query(w.cache, "SELECT m.data FROM message_search JOIN messages m ON m.rowid=message_search.rowid WHERE message_search MATCH ? AND m.account=? AND (?='' OR m.peer=?) ORDER BY message_search.rank LIMIT 50 OFFSET ?", {tokens.join(" AND "), account, peer, peer, qMax(0, offset)});
            while (rows.next()) messages.append(object(rows.value(0)));
        } else {
            auto rows = w.query(w.cache, "SELECT data FROM messages WHERE account=? AND (?='' OR peer=?) ORDER BY cmid DESC", {account, peer, peer});
            int skipped = 0;
            while (rows.next() && messages.size() < 50) {
                const auto msg = object(rows.value(0));
                if (msg.value("deleted").toBool() || msg.value("deleted").toInt() == 1 || !msg.value("text").toString().contains(queryText, Qt::CaseInsensitive)) continue;
                if (skipped++ < offset) continue;
                messages.append(msg);
            }
        }
        auto peers = w.query(w.cache, "SELECT data FROM entities WHERE account=? AND kind='peer'", {account});
        while (peers.next()) profiles.append(object(peers.value(0)));
        return QJsonObject{{"items", messages}, {"profiles", profiles}, {"cached", true}, {"fullText", w.fullTextSearch}};
    }, std::move(result));
}

void LocalStore::saveCatalog(const QString &account, const QString &name, const QJsonObject &value, Result result)
{
    run([account, name, value](StoreWorker &w) {
        w.query(w.cache, "INSERT OR REPLACE INTO entities VALUES(?,?,?,?)", {account, "catalog", name, bytes(value)});
        return QJsonObject();
    }, std::move(result));
}
