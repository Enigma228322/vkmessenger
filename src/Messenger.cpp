#include "models/TimelinePresentation.h"
#include "Messenger.h"
#include "Keychain.h"
#include "Form.h"
#include "LongPollEvent.h"
#include "Methods.h"
#include "BuildConfig.h"
#include "platform/Preferences.h"
#include <QUrlQuery>
#include <QDateTime>
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QRandomGenerator>
#include <QUuid>
#include <QStandardPaths>
#include <QCryptographicHash>
#include <algorithm>
#include <limits>

static QString id(const QJsonValue &value) { return QString::number(value.toInteger()); }
static QString preview(const QJsonObject &object)
{
    const auto text = object.value("text").toString();
    if (!text.isEmpty()) return text;
    QStringList labels;
    for (const auto &value : object.value("attachments").toArray()) {
        const auto type = value.toObject().value("type").toString();
        if (type == "photo") labels << "Фотография";
        else if (type == "audio_message") labels << "Голосовое сообщение";
        else if (type == "video") labels << "Видео";
        else if (type == "doc") labels << "Документ";
        else if (type == "sticker") labels << "Стикер";
        else labels << "Вложение";
    }
    if (!labels.isEmpty()) return labels.join(", ");
    if (!object.value("fwd_messages").toArray().isEmpty()) return "Пересланное сообщение";
    if (object.contains("action")) return "Событие беседы";
    return "Сообщение";
}

static QString storageDirectory(const QString &overridePath)
{
    return overridePath.isEmpty() ? QCoreApplication::instance()->property("vkmStoragePath").toString() : overridePath;
}

static QString mediaDirectory(const QString &overridePath)
{
    if (!overridePath.isEmpty()) return overridePath + "/media";
    const auto previous = QCoreApplication::instance()->property("vkmMediaPath").toString();
    if (!previous.isEmpty()) return previous;
    const auto storage = storageDirectory(overridePath);
    return storage.isEmpty() ? QString() : storage + "/media";
}

Messenger::Messenger(QObject *parent, QNetworkAccessManager *network, const QString &storagePath, bool persistentSession)
    : QObject(parent), m_store(storageDirectory(storagePath)), m_actions(this), m_profileActions(this), m_folders(this),
      m_media(mediaDirectory(storagePath), nullptr, network),
      m_oauth(this), m_longPoll(this, network),
      m_transfers([this](const QString &method, Form parameters, Transfers::Result callback) {
          api(method, parameters, callback);
      }, this, network), m_api(network)
{
    m_persistentSession = persistentSession;
    m_draftTimer.setSingleShot(true);
    m_draftTimer.setInterval(250);
    connect(&m_draftTimer, &QTimer::timeout, this, &Messenger::flushDrafts);
    m_reconnectTimer.setInterval(15000);
    connect(&m_reconnectTimer, &QTimer::timeout, this, [this] {
        if (m_connected && m_offline && !m_authorizing && !m_demo) {
            m_needCredentials = true;
            scheduleSync(true, false);
        }
    });
    m_reconnectTimer.start();
    connect(&m_transfers, &Transfers::changed, this, [this] {
        if (m_restoringTransfers || m_demo || m_account.isEmpty()) return;
        const auto state = m_transfers.savedState();
        if (m_drafts.value("__transfers") == state) return;
        m_drafts["__transfers"] = state;
        m_dirtyDrafts.insert("__transfers"); flushDrafts();
    });
    m_clientId = QString::fromUtf8(VKM_VK_APP_ID);
    m_redirectUri = QString::fromUtf8(VKM_VK_REDIRECT_URI);
    m_scope = QString::fromUtf8(VKM_VK_SCOPE);
    if (apiInspectorEnabled()) {
        m_clientId = m_settings.value("vk/clientId", m_clientId).toString();
        m_redirectUri = m_settings.value("vk/redirect", m_redirectUri).toString();
        m_scope = m_settings.value("vk/scope", m_scope).toString();
    }
    m_toastTimer.setSingleShot(true);
    connect(&m_toastTimer, &QTimer::timeout, this, [this] { m_toast.clear(); m_toastUrl = QUrl(); emit changed(); });
    connect(&m_transfers, &Transfers::chatPhotoChanged, this, [this] { scheduleSync(true, false); });
    m_sync.setSingleShot(true);
    m_sync.setInterval(120);
    connect(&m_sync, &QTimer::timeout, this, &Messenger::drainSync);
    connect(&m_longPoll, &LongPoll::batch, this, [this](const QJsonObject &batch) {
        applyLongPoll(batch.value("updates").toArray());
    });
    connect(&m_longPoll, &LongPoll::resyncRequired, this, [this] {
        m_needCredentials = true;
        scheduleSync(true, true);
    });
    connect(&m_longPoll, &LongPoll::fatalError, this, &Messenger::fail);
    connect(&m_longPoll, &LongPoll::statusChanged, this, [this](const QString &status) {
        m_status = status;
        emit changed();
    });
    m_activityExpiry.setInterval(1000);
    connect(&m_activityExpiry, &QTimer::timeout, this, [this] {
        bool expired = false;
        const auto now = QDateTime::currentMSecsSinceEpoch();
        for (auto it = m_activity.begin(); it != m_activity.end();) {
            if (it->expires <= now) { it = m_activity.erase(it); expired = true; }
            else ++it;
        }
        for (auto it = m_callbacks.begin(); it != m_callbacks.end();)
            if (it.value() <= now) it = m_callbacks.erase(it); else ++it;
        if (expired) emit changed();
    });
    m_activityExpiry.start();
    m_refresh.setSingleShot(true);
    connect(&m_refresh, &QTimer::timeout, this, [this] {
        ++m_historyGeneration;
        m_api.cancel();
        m_outboxSending = false;
        for (auto &pending : m_outgoing) if (pending.value("state").toString() == "sending") pending.insert("state", "uncertain");
        m_actions.resetAccount(); m_profileActions.cancelPending(); m_folders.reset(false);
        m_transfers.pause();
        m_readPending.clear();
        m_operationBusy = false;
        emit operationChanged();
        m_fetchingHistory = m_fetchingConversations = m_loading = false;
        m_longPoll.stop();
        m_authorizing = true;
        m_status = "Обновление сессии…";
        emit changed();
        m_oauth.refresh(m_session);
    });
    connect(&m_oauth, &OAuth::failed, this, [this](const QString &error) {
        m_authorizing = false;
        fail(error);
    });
    connect(&m_oauth, &OAuth::authenticated, this, &Messenger::acceptSession);
    QTimer::singleShot(0, this, [this] {
        if (!m_persistentSession || m_connected || m_authorizing) return;
        const auto saved = QJsonDocument::fromJson(Keychain::read()).object();
        if (saved.value("auth_type").toString() == "manual_token"
            && !saved.value("access_token").toString().isEmpty()) {
            m_session = saved;
            m_authorizing = true;
            m_token = saved.value("access_token").toString();
            restoreAccount(saved, [this, saved] {
                beginTokenLogin(saved.value("access_token").toString(), LongPoll::cursor(saved.value("group_id")));
            });
            return;
        }
        if (saved.value("refresh_token").toString().isEmpty()) return;
        m_session = saved;
        m_authorizing = true;
        m_status = "Восстановление сессии…";
        emit changed();
        restoreAccount(saved, [this, saved] { m_oauth.refresh(saved); });
    });
}

void Messenger::fail(const QString &error)
{
    m_error = error;
    m_status = "Требуется внимание";
    emit changed();
}

void Messenger::login()
{
    if (m_clientId.isEmpty()) { fail("В этой сборке ещё не настроен вход через VK."); return; }
    logout();
    m_settings.setValue("vk/clientId", m_clientId.trimmed());
    m_settings.setValue("vk/redirect", m_redirectUri.trimmed());
    m_settings.setValue("vk/scope", m_scope.trimmed());
    m_authorizing = true;
    m_status = "Завершите вход в браузере";
    emit changed();
    m_oauth.start(m_clientId.trimmed(), m_redirectUri.trimmed(), m_scope.trimmed());
}

void Messenger::acceptSession(QJsonObject session)
{
    if (session.value("refresh_token").toString().isEmpty())
        session.insert("refresh_token", m_session.value("refresh_token"));
    m_session = session;
    m_token = session.value("access_token").toString();
    m_connected = true;
    m_offline = false;
    m_syncBlocked = false;
    m_demo = false;
    m_authorizing = false;
    m_error.clear();
    m_status = "Подключение к сообщениям…";
    if (m_persistentSession && !Keychain::write(QJsonDocument(session).toJson(QJsonDocument::Compact)))
        m_error = "Не удалось сохранить сессию в Keychain. После закрытия понадобится повторный вход.";
    if (!session.value("refresh_token").toString().isEmpty()) {
        const auto seconds = std::clamp(session.value("expires_in").toInt(3600) - 60, 30, 86400);
        m_refresh.start(seconds * 1000);
    }
    emit changed();
    m_needCredentials = true;
    restoreAccount(session, [this] { m_transfers.resume(); scheduleSync(true, true); pumpOutbox(); openPendingLink(); });
}

void Messenger::loginWithToken(const QString &token, const QString &group)
{
    const auto value = token.trimmed();
    if (value.isEmpty() || std::any_of(value.begin(), value.end(), [](QChar c) { return c.isSpace(); })) {
        fail("Вставьте access_token целиком, без пробелов и переносов строк внутри токена.");
        return;
    }
    bool validGroup = true;
    if (!group.isEmpty() && (group.toLongLong(&validGroup) <= 0 || !validGroup)) {
        fail("Укажите положительный ID сообщества."); return;
    }
    logout();
    beginTokenLogin(value, group);
}

void Messenger::beginTokenLogin(const QString &token, const QString &group)
{
    m_token = token;
    m_authorizing = true;
    m_status = "Проверка access_token…";
    emit changed();
    const bool community = !group.isEmpty() && group != "0";
    api(community ? "groups.getById" : "users.get", community ? Form{{"group_ids", group}} : Form(), [this, token, group, community](const QJsonValue &value, const QString &error) {
        m_authorizing = false;
        if (!error.isEmpty()) {
            if (!m_connected) m_token.clear();
            fail(error);
            if (m_connected) openPendingLink();
            return;
        }
        const auto users = community && value.isObject() ? value.toObject().value("groups").toArray() : value.toArray();
        if (users.isEmpty() || users.first().toObject().value("id").toInteger() <= 0) {
            m_token.clear();
            fail("Не удалось определить аккаунт. Проверьте тип access_token и ID сообщества.");
            return;
        }
        QJsonObject session{{"access_token", token}, {"auth_type", "manual_token"},
            {"user_id", community ? -users.first().toObject().value("id").toInteger() : users.first().toObject().value("id").toInteger()}};
        if (community) session.insert("group_id", group);
        acceptSession(session);
    });
}

Messenger::~Messenger() { flushDrafts(); m_transfers.disconnect(this); }

void Messenger::logout()
{
    flushDrafts();
    ++m_generation;
    ++m_historyGeneration;
    m_oauth.cancel();
    m_longPoll.stop();
    m_restoringTransfers = true;
    m_transfers.reset();
    m_restoringTransfers = false;
    m_recorder.discard();
    m_sync.stop();
    m_api.cancel();
    m_refresh.stop();
    m_token.clear();
    m_pendingPeer.clear(); m_pendingText.clear(); m_pendingRandomId.clear();
    m_session = {};
    if (m_persistentSession) Keychain::clear();
    m_connected = m_demo = m_sending = m_loading = m_authorizing = false;
    m_offline = m_outboxSending = false; m_queueWrites = 0;
    m_account.clear(); m_drafts.clear(); m_outgoing.clear(); m_actions.resetAccount(); m_profileActions.resetAccount(); m_folders.reset(); m_media.setAccount({});
    m_fetchingHistory = m_fetchingConversations = false;
    m_conversationsDirty = m_historyDirty = false;
    m_activity.clear(); m_online.clear(); m_outRead.clear(); m_unreadCount = 0;
    m_presenceRevision.clear(); m_eventRevision = 0; m_syncBlocked = false;
    m_rawConversations.clear(); m_peers.clear(); m_histories.clear(); m_localIdentities.clear();
    m_lastCmid.clear(); m_inRead.clear(); m_readPending.clear(); m_older.clear(); m_newer.clear();
    m_contentDirty.clear(); m_contentRevision.clear();
    m_verifiedRanges.clear();
    m_version.clear(); m_historyError.clear(); m_needCredentials = true;
    m_replyMessage.clear(); m_eventHistory.clear(); m_operationBusy = false;
    m_callbacks.clear(); m_toastTimer.stop(); m_toast.clear(); m_toastUrl = QUrl();
    m_restorable.clear();
    m_operationError.clear(); m_operationResult = {}; m_lastTyping = 0;
    m_peer.clear(); m_title.clear(); m_error.clear(); m_status.clear();
    m_demoHistory.clear();
    m_conversations.replace({});
    m_messages.replace({});
    emit changed();
    emit operationChanged();
}

void Messenger::api(const QString &method, Form parameters, Callback callback)
{
    m_api.setToken(m_token);
    const auto generation = m_generation;
    m_api.request(method, std::move(parameters), [this, generation, method, callback = std::move(callback)](const ApiResult &result) {
        if (generation != m_generation) return;
        if (result.code == 5) { logout(); fail(result.error); return; }
        if ((result.code == 7 || result.code == 15 || result.code == 27) && method == "messages.getDiff") {
            m_longPoll.stop(); m_syncBlocked = true; fail(result.error);
        }
        if (result.networkFailure) { m_offline = true; emit changed(); }
        callback(result.value, result.error);
    });
}

void Messenger::reload()
{
    if (!m_connected || m_demo || m_authorizing) return;
    scheduleSync(true, true);
}

void Messenger::scheduleSync(bool conversations, bool history)
{
    m_conversationsDirty |= conversations;
    m_historyDirty |= history && !m_peer.isEmpty();
    if (!m_sync.isActive()) m_sync.start(120);
}

void Messenger::drainSync()
{
    if (!m_connected || m_demo || m_authorizing || m_syncBlocked) return;
    if (m_conversationsDirty && !m_fetchingConversations) {
        m_conversationsDirty = false;
        fetchConversations();
    }
    if (m_historyDirty && !m_fetchingHistory && !m_fetchingConversations && !m_peer.isEmpty()) {
        m_historyDirty = false;
        const auto deferred = m_deferredHistory; m_deferredHistory.valid = false;
        if (deferred.valid && deferred.generation == m_historyGeneration) fetchHistory(deferred.older, deferred.latest, deferred.anchor, deferred.keepWindow);
        else fetchHistory();
    }
}

QString Messenger::peerActivity() const
{
    if (m_activity.contains(m_peer)) return m_activity.value(m_peer).text;
    if (m_online.contains(m_peer)) return m_online.value(m_peer) ? "В сети" : "Не в сети";
    return m_demo ? "Демонстрационная переписка" : "VK · Long Poll";
}

void Messenger::applyLongPoll(const QJsonArray &events)
{
    bool conversations = false, history = false, folders = false;
    for (const auto &raw : events) {
        ++m_eventRevision;
        const auto event = LongPollEvent::parse(raw);
        // Expose every event, including extension events, rather than dropping it.
        emit realtimeEvent(event.type, event.raw);
        if (event.type == 119 && event.raw.size() > 1) {
            const auto data = event.raw[1].toObject();
            const auto eventId = LongPoll::cursor(data.value("event_id"));
            if (m_callbacks.remove(eventId)) {
                const auto action = data.value("action").toObject();
                m_toast = action.value("text").toString("Ответ бота"); m_toastUrl = QUrl();
                auto link = action.value("link").toString();
                if (action.value("type").toString() == "open_app")
                    link = "https://vk.ru/app" + id(action.value("app_id")) + "#" + action.value("hash").toString();
                const QUrl url(link);
                if (url.scheme() == "https" || url.scheme() == "http") m_toastUrl = url;
                m_toastTimer.start(10000);
            }
        }
        if (event.type == 115 || event.type == 119 || event.kind == LongPollEvent::Unknown) {
            m_eventHistory.prepend(QVariantMap{{"type", event.type}, {"payload", safeJson(event.raw)},
                {"time", QDateTime::currentDateTime().toString("HH:mm:ss")}});
            while (m_eventHistory.size() > 100) m_eventHistory.removeLast();
        }
        switch (event.kind) {
        case LongPollEvent::Typing: {
            QString text = "Печатает…";
            if (event.type == 64) text = "Записывает голосовое сообщение…";
            if (event.type == 65) text = "Загружает фотографию…";
            if (event.type == 66) text = "Загружает видео…";
            if (event.type == 67) text = "Загружает файл…";
            if (event.type == 68) text = "Записывает видеосообщение…";
            m_activity[event.peer] = {text, QDateTime::currentMSecsSinceEpoch() + 6000};
            break;
        }
        case LongPollEvent::Presence:
            m_online[event.peer] = event.type == 8;
            m_presenceRevision[event.peer] = m_eventRevision;
            break;
        case LongPollEvent::Counter:
            m_unreadCount = event.raw[1].toInt();
            conversations = true;
            break;
        case LongPollEvent::Folders:
            folders = true;
            break;
        case LongPollEvent::Call:
            m_activity[event.peer] = {"Входящий звонок · звонки пока не поддерживаются", QDateTime::currentMSecsSinceEpoch() + 30000};
            break;
        case LongPollEvent::Persistent:
        case LongPollEvent::Notification:
            conversations = true;
            history |= event.peer.isEmpty() || event.peer == m_peer;
            if (event.type == 4 || event.type == 10004) m_activity.remove(event.peer);
            break;
        case LongPollEvent::Unknown:
        case LongPollEvent::Invalid:
            // Server schema extensions and incomplete flag events are resolved
            // from API snapshots, not guessed from positional fields.
            conversations = history = true;
            break;
        }
    }
    if (folders) m_folders.refresh();
    if (conversations || history) scheduleSync(conversations, history);
    emit changed();
}

void Messenger::fetchConversations()
{
    if (m_fetchingConversations) return;
    m_fetchingConversations = true;
    auto transaction = std::make_shared<DiffTransaction>();
    transaction->generation = m_generation;
    transaction->from = m_version;
    transaction->full = m_version.isEmpty();
    transaction->credentials = m_needCredentials;
    m_needCredentials = false;
    fetchDiffPage(transaction);
}

void Messenger::fetchDiffPage(const std::shared_ptr<DiffTransaction> &transaction, const QString &source)
{
    Form params{{"lp_version", "21"}, {"conversations_limit", "200"}, {"nested_limit", "2"},
        {"group_id", groupId()}, {"counter_filters", "all"}, {"fields", "photo_100,online"}};
    QString filters = "server_version,profiles,contacts,groups,messages";
    if (groupId() == "0") filters += ",counters";
    if (transaction->credentials && source.isEmpty()) filters += ",credentials";
    params.append({"extended_filters", filters});
    if (!transaction->from.isEmpty()) params.append({"from_version", transaction->from});
    if (!source.isEmpty()) {
        params.append({"to_version", transaction->to});
        params.append({"conversations_source", source});
    }
    api("messages.getDiff", params, [this, transaction](const QJsonValue &value, const QString &error) {
        if (transaction->generation != m_generation) return;
        const auto response = value.toObject();
        const auto version = LongPoll::cursor(response.value("server_version"));
        if (!error.isEmpty() || (transaction->pages.isEmpty() && version.isEmpty())) {
            m_fetchingConversations = false;
            m_needCredentials |= transaction->credentials;
            m_conversationsDirty = true;
            fail(error.isEmpty() ? "messages.getDiff: отсутствует server_version." : error);
            if (!m_syncBlocked) m_sync.start(3000);
            return;
        }
        if ((response.value("invalidate_all").toBool() || response.value("invalidate_all").toInt() == 1) && !transaction->full) {
            m_fetchingConversations = false;
            m_version.clear();
            m_needCredentials = true;
            m_longPoll.stop();
            scheduleSync(true, true);
            return;
        }
        if (!transaction->pages.isEmpty() && !version.isEmpty() && version != transaction->to) {
            m_fetchingConversations = false; m_needCredentials |= transaction->credentials;
            fail("getDiff: версии страниц не совпадают. Повторная синхронизация…");
            scheduleSync(true, true); return;
        }
        if (transaction->pages.isEmpty()) {
            if (transaction->credentials) {
                const auto credentials = response.value("credentials").toObject();
                if (credentials.value("server_lp").toString().isEmpty() || credentials.value("key").toString().isEmpty()
                    || LongPoll::cursor(credentials.value("ts")).isEmpty()) {
                    m_fetchingConversations = false; m_needCredentials = true; m_conversationsDirty = true;
                    fail("getDiff: отсутствуют credentials Long Poll."); m_sync.start(3000); return;
                }
            }
            transaction->to = version;
        }
        transaction->pages.append(response);
        const auto next = response.value("conversations_source").toString();
        if (!next.isEmpty()) {
            if (transaction->sources.contains(next) || transaction->pages.size() > 10000) {
                m_fetchingConversations = false;
                m_needCredentials |= transaction->credentials;
                fail("messages.getDiff: некорректный курсор пагинации.");
                return;
            }
            transaction->sources.insert(next);
            fetchDiffPage(transaction, next);
            return;
        }
        // Reserve revisions before the asynchronous commit. A history HTTP
        // response can arrive before its completion is delivered to the GUI.
        QSet<QString> committingPeers;
        if (transaction->full && !m_peer.isEmpty()) committingPeers.insert(m_peer);
        for (const auto &page : transaction->pages) for (const auto &value : page.value("conversations_info").toArray()) {
            const auto conversation = value.toObject().value("conversation").toObject();
            if (!conversation.isEmpty()) committingPeers.insert(id(conversation.value("peer").toObject().value("id")));
        }
        for (const auto &peer : committingPeers) ++m_contentRevision[peer];
        m_store.commitDiff(m_account, transaction->pages, transaction->to, transaction->full,
            [this, transaction](const QJsonObject &, const QString &storeError) {
            if (transaction->generation != m_generation) return;
            m_fetchingConversations = false;
            if (!storeError.isEmpty()) {
                m_needCredentials |= transaction->credentials;
                fail("Не удалось сохранить переписку: " + storeError);
                m_conversationsDirty = true; m_sync.start(3000); return;
            }
            applyDiff(*transaction);
            m_version = transaction->to;
            const bool recovered = m_offline;
            m_offline = false;
            if (recovered) m_transfers.resume();
            if (transaction->credentials || transaction->full) m_folders.refresh();
            reconcileOutbox();
            if (transaction->credentials && !m_needCredentials)
                m_longPoll.start(transaction->pages.first().value("credentials").toObject());
            if (m_conversationsDirty || m_historyDirty) m_sync.start(120);
            pumpOutbox();
            emit changed();
        });
    });
}

void Messenger::applyDiff(const DiffTransaction &transaction)
{
    if (transaction.full) {
        m_rawConversations.clear();
        m_histories.clear(); m_older.clear(); m_newer.clear();
        m_verifiedRanges.clear();
    }
    for (const auto &page : transaction.pages) {
        for (const auto &kind : {"profiles", "groups", "contacts"}) {
            for (const auto &value : page.value(kind).toArray()) {
                auto entity = value.toObject();
                const auto peer = QString::number(entity.value("id").toInteger() * (QString(kind) == "groups" ? -1 : 1));
                m_peers[peer] = entity;
                if (entity.contains("online") && !m_presenceRevision.contains(peer))
                    m_online[peer] = entity.value("online").toInt() == 1;
            }
        }
        if (page.value("counters").toObject().contains("messages"))
            m_unreadCount = page.value("counters").toObject().value("messages").toInt();
        for (const auto &value : page.value("conversations_info").toArray()) {
            const auto info = value.toObject();
            const auto conversation = info.value("conversation").toObject();
            if (conversation.isEmpty()) continue;
            const auto peer = id(conversation.value("peer").toObject().value("id"));
            if (peer == "0") continue;
            m_rawConversations[peer] = info;
            ++m_contentRevision[peer];
            m_lastCmid[peer] = conversation.value("last_conversation_message_id").toInteger();
            m_inRead[peer] = conversation.value("in_read_cmid").toInteger();
            m_outRead[peer] = conversation.value("out_read_cmid").toInteger();
            auto &history = m_histories[peer];
            if (info.value("invalidate").toBool() || info.value("invalidate").toInt() == 1) { history.clear(); m_verifiedRanges.remove(peer); }
            for (const auto &flagValue : info.value("cmids_flags").toArray()) {
                const auto flag = flagValue.toObject();
                const auto cmid = flag.value("cmid").toInteger();
                if (flag.value("updated_flags").toInt() & 1) history.remove(cmid);
                else if (history.contains(cmid)) m_contentDirty[peer].insert(cmid);
            }
            for (const auto &v : info.value("cmids_updated_reactions").toArray())
                if (history.contains(v.toInteger())) m_contentDirty[peer].insert(v.toInteger());
            for (const auto &v : info.value("range_updated_cmids").toArray()) {
                const auto range = v.toObject();
                auto it = history.lowerBound(range.value("min").toInteger());
                for (; it != history.end() && it.key() <= range.value("max").toInteger(); ++it)
                    m_contentDirty[peer].insert(it.key());
            }
            for (const auto &rangeValue : info.value("range_deleted_cmids").toArray()) {
                const auto range = rangeValue.toObject();
                auto it = history.lowerBound(range.value("min").toInteger());
                while (it != history.end() && it.key() <= range.value("max").toInteger()) it = history.erase(it);
            }
            for (const auto &message : info.value("message").toArray()) {
                const auto object = message.toObject();
                const auto cmid = object.value("conversation_message_id").toInteger();
                if (cmid > 0) history[cmid] = object;
            }
            if (peer == m_peer) m_historyDirty = true;
        }
    }
    rebuildConversations();
    m_folders.conversationsChanged();
    reconcileOutbox();
    for (auto it = m_histories.begin(); it != m_histories.end(); ++it) {
        if (it.key() == m_peer) continue;
        while (it->size() > 1) it->erase(it->begin());
    }
    publishHistory();
}

void Messenger::rebuildConversations()
{
    QVariantList rows;
    for (auto it = m_rawConversations.cbegin(); it != m_rawConversations.cend(); ++it) {
        const auto peer = it.key();
        const auto convo = it.value().value("conversation").toObject();
        const auto chat = convo.value("chat_settings").toObject();
        const auto entity = m_peers.value(peer);
        const auto lastMessages = it.value().value("message").toArray();
        const auto last = lastMessages.isEmpty() ? QJsonObject() : lastMessages.first().toObject();
        const auto sort = convo.value("sort_id").toObject();
        if (sort.contains("minor_id") && sort.value("minor_id").toInteger() == 0) continue;
        QString title = chat.value("title").toString();
        if (title.isEmpty()) title = entity.value("name").toString();
        if (title.isEmpty()) title = (entity.value("first_name").toString() + " " + entity.value("last_name").toString()).trimmed();
        if (title.isEmpty()) title = "Диалог " + peer;
        if (peer == m_peer) m_title = title;
        const auto push = convo.value("push_settings").toObject();
        const auto flags = convo.value("peer_flags").toInteger();
        rows.append(QVariantMap{{"id", peer}, {"peer", peer}, {"title", title},
            {"avatar", chat.isEmpty() ? entity.value("photo_100").toString() : chat.value("photo").toObject().value("photo_100").toString()},
            {"preview", last.isEmpty() ? QString() : preview(last)}, {"unread", convo.value("unread_count").toInt()},
            {"muted", push.value("disabled_forever").toBool() || push.value("disabled_until").toInteger() == -1
                || push.value("disabled_until").toInteger() > QDateTime::currentSecsSinceEpoch()},
            {"time", last.isEmpty() ? QString() : QDateTime::fromSecsSinceEpoch(last.value("date").toInteger()).toString("HH:mm")},
            {"sort", convo.value("sort_id").toObject().value("major_id").toInteger(last.value("date").toInteger())},
            {"minor", convo.value("sort_id").toObject().value("minor_id").toInteger()},
            {"pinned", (sort.value("major_id").toInteger() & ((1 << 4) | (1 << 5) | (1 << 6))) != 0},
            {"important", convo.value("important").toBool() || (flags & 1) != 0},
            {"archived", (flags & (1 << 23)) != 0}, {"markedUnread", (flags & (1 << 20)) != 0},
            {"canWrite", convo.value("can_write").toObject().value("allowed").toBool(true)}});
    }
    std::stable_sort(rows.begin(), rows.end(), [](const QVariant &a, const QVariant &b) {
        const auto x = a.toMap(), y = b.toMap();
        if (x.value("sort") != y.value("sort")) return x.value("sort").toLongLong() > y.value("sort").toLongLong();
        return x.value("minor").toLongLong() > y.value("minor").toLongLong();
    });
    m_conversations.reconcile(rows);
}

QVariantMap Messenger::message(const QJsonObject &object) const
{
    const auto cmid = object.value("conversation_message_id").toInteger();
    const auto sender = m_peers.value(id(object.value("from_id")));
    QString name = sender.value("name").toString();
    if (name.isEmpty()) name = (sender.value("first_name").toString() + " " + sender.value("last_name").toString()).trimmed();
    return {{"id", QString::number(cmid)}, {"identity", m_localIdentities.value(m_peer).value(cmid, QString::number(cmid))}, {"cmid", QString::number(cmid)}, {"globalId", id(object.value("id"))}, {"text", preview(object)},
        {"outgoing", object.value("out").toInt() == 1},
        {"read", object.value("out").toInt() == 1 && cmid > 0 && cmid <= m_outRead.value(m_peer)},
        {"raw", safeJson(object)}, {"important", object.value("important").toBool()},
        {"edited", object.value("update_time").toInteger() > 0},
        {"attachments", safeJson(object.value("attachments"))},
        {"time", QDateTime::fromSecsSinceEpoch(object.value("date").toInteger()).toString("dd.MM · HH:mm")},
        {"sender", name.isEmpty() ? id(object.value("from_id")) : name}, {"senderAvatar", sender.value("photo_100").toString()}};
}

void Messenger::publishHistory()
{
    if (m_demo || m_peer.isEmpty()) return;
    QVariantList rows;
    qint64 previous = 0;
    for (const auto &object : m_histories.value(m_peer)) {
        const auto cmid = object.value("conversation_message_id").toInteger();
        if (previous > 0 && cmid > previous + 1) {
            bool verified = false;
            for (const auto &range : m_verifiedRanges.value(m_peer))
                if (range.first <= previous && range.second >= cmid) { verified = true; break; }
            if (!verified) rows.append(QVariantMap{{"id", QString("gap:%1:%2").arg(previous).arg(cmid)},
                {"gap", true}, {"first", QString::number(previous + 1)}, {"last", QString::number(cmid - 1)}});
        }
        previous = cmid;
        const auto deleted = object.value("deleted");
        if (deleted.toBool() || deleted.toInt() == 1) continue;
        rows.append(message(object));
    }
    for (const auto &pending : m_outgoing) {
        if (pending.value("peer").toString() != m_peer || pending.value("confirmed").toBool()) continue;
        const auto state = pending.value("state").toString();
        rows.append(QVariantMap{{"id", pending.value("local_id").toString()}, {"identity", pending.value("local_id").toString()}, {"local", true},
            {"text", pending.value("text").toString()}, {"outgoing", true}, {"read", false},
            {"time", QDateTime::fromSecsSinceEpoch(pending.value("created").toInteger()).toString("HH:mm")},
            {"sentAt", pending.value("created").toInteger()}, {"sender", "Вы"}, {"delivery", state}, {"deliveryError", pending.value("error").toString()},
            {"raw", safeJson(pending.value("preview"))}, {"attachments", safeJson(pending.value("preview").toObject().value("attachments"))}});
    }
    emit historyAboutToChange();
    m_messages.reconcile(TimelinePresentation::decorate(rows, m_inRead.value(m_peer)));
    emit historyChanged();
}

void Messenger::selectConversation(const QString &peer, const QString &title)
{
    if (m_peer == peer) return;
    m_localIdentities.remove(m_peer);
    if (!m_demo && m_histories.value(m_peer).size() > 1) {
        auto &old = m_histories[m_peer];
        const auto last = old.last(); const auto key = old.lastKey();
        old.clear(); old.insert(key, last);
        m_verifiedRanges.remove(m_peer); m_contentDirty.remove(m_peer);
    }
    ++m_historyGeneration;
    m_peer = peer; m_title = title;
    m_messages.replace({});
    m_fetchingHistory = false;
    m_historyDirty = false;
    m_loading = false;
    m_historyError.clear();
    m_replyMessage = m_drafts.value(peer).value("reply").toObject().toVariantMap();
    if (m_demo) m_messages.replace(TimelinePresentation::decorate(m_demoHistory.value(peer)));
    else { publishHistory(); readCachedHistory(peer); }
    emit draftChanged();
    emit changed();
}

void Messenger::fetchHistory(bool older, bool latest, qint64 anchor, bool keepWindow)
{
    if (m_fetchingConversations) {
        m_deferredHistory = {true, older, latest, keepWindow, anchor, m_historyGeneration};
        m_historyDirty = true; return;
    }
    if (m_fetchingHistory || m_peer.isEmpty() || m_offline || !m_connected || m_authorizing) return;
    m_fetchingHistory = true;
    m_loading = true;
    m_historyError.clear();
    emit changed();
    const auto generation = m_historyGeneration;
    const auto peer = m_peer;
    const auto revision = m_contentRevision.value(peer);
    const auto dirty = m_contentDirty.value(peer);
    const auto cache = m_histories.value(peer);
    const bool replaceWindow = (anchor >= 0 && !keepWindow) || (latest && m_newer.value(peer));
    const auto mark = anchor >= 0 ? anchor : older && !cache.isEmpty() ? cache.firstKey() - 1 : qMax(qint64(0), m_lastCmid.value(peer) - 1);
    QJsonArray requests;
    QJsonObject contentRequest{{"peer_id", peer.toLongLong()}, {"cmid_mark", mark}, {"offset", anchor >= 0 ? -25 : older ? 0 : -1}, {"limit", 50}};
    QJsonArray updated;
    for (auto cmid : dirty) updated.append(cmid);
    if (!updated.isEmpty()) contentRequest.insert("updated_cmids", updated);
    requests.append(contentRequest);
    api("messages.getDiffContent", {{"conversation_messages", QString::fromUtf8(QJsonDocument(requests).toJson(QJsonDocument::Compact))},
        {"extended", "1"}, {"nested_limit", "2"}, {"group_id", groupId()}},
        [this, generation, peer, older, latest, revision, dirty, anchor, replaceWindow, keepWindow](const QJsonValue &value, const QString &error) {
        if (generation != m_historyGeneration) return;
        m_fetchingHistory = false;
        m_loading = false;
        if (revision != m_contentRevision.value(peer)) {
            QTimer::singleShot(0, this, [this, generation, older, latest, anchor, keepWindow] {
                if (generation == m_historyGeneration) fetchHistory(older, latest, anchor, keepWindow);
            });
            return;
        }
        if (!error.isEmpty()) {
            m_historyError = error;
            emit changed();
            return;
        }
        const auto response = value.toObject();
        if (!response.value("items").isArray()) {
            m_historyError = "messages.getDiffContent: в ответе отсутствует items.";
            emit changed(); return;
        }
        bool found = false;
        int count = 0;
        qint64 first = std::numeric_limits<qint64>::max(), last = 0;
        for (const auto &v : response.value("profiles").toArray()) {
            auto p = v.toObject(); m_peers[id(p.value("id"))] = p;
        }
        for (const auto &v : response.value("groups").toArray()) {
            auto p = v.toObject(); m_peers[QString::number(-p.value("id").toInteger())] = p;
        }
        for (const auto &v : response.value("items").toArray()) {
            const auto item = v.toObject();
            if (id(item.value("peer_id")) != peer) continue;
            if (replaceWindow && !found) { m_histories[peer].clear(); m_older.remove(peer); m_verifiedRanges.remove(peer); }
            found = true;
            for (const auto &key : {"messages", "requested_messages", "range_messages"}) {
                for (const auto &v : item.value(key).toArray()) {
                    const auto msg = v.toObject();
                    const auto cmid = msg.value("conversation_message_id").toInteger();
                    if (cmid > 0) {
                        m_histories[peer][cmid] = msg;
                        if (QString(key) == "messages") { ++count; first = qMin(first, cmid); last = qMax(last, cmid); }
                    }
                }
            }
        }
        if (!found) m_historyError = "Сервер не вернул содержимое этого диалога.";
        else m_contentDirty[peer].subtract(dirty);
        if (last > 0) {
            auto ranges = m_verifiedRanges.value(peer);
            ranges.append({first, last});
            std::sort(ranges.begin(), ranges.end());
            QList<QPair<qint64, qint64>> merged;
            for (const auto &range : ranges) {
                if (!merged.isEmpty() && range.first <= merged.last().second + 1)
                    merged.last().second = qMax(merged.last().second, range.second);
                else merged.append(range);
            }
            m_verifiedRanges[peer] = merged;
        }
        if (older || !m_older.contains(peer)) m_older[peer] = count >= 50;
        m_newer[peer] = !m_histories.value(peer).isEmpty() && m_histories.value(peer).lastKey() < m_lastCmid.value(peer);
        persistHistory(peer);
        while (m_histories[peer].size() > 1000) {
            if (older) m_histories[peer].erase(std::prev(m_histories[peer].end()));
            else { m_histories[peer].erase(m_histories[peer].begin()); m_older[peer] = true; }
        }
        m_newer[peer] = !m_histories.value(peer).isEmpty() && m_histories.value(peer).lastKey() < m_lastCmid.value(peer);
        reconcileOutbox();
        publishHistory();
        if (latest) emit scrollToEnd();
        if (anchor >= 0) emit scrollToMessage(QString::number(anchor));
        if (m_historyDirty && !m_sync.isActive()) m_sync.start(120);
        emit changed();
    });
}

void Messenger::loadOlder() { if (!m_demo && hasOlder() && !m_loading) readCachedHistory(m_peer, true); }
void Messenger::loadGap(const QString &first, const QString &last)
{
    if (!m_loading && !m_demo) fetchHistory(false, false, qMin(last.toLongLong(), first.toLongLong() + 24), true);
}
void Messenger::jumpToEnd()
{
    if (m_demo || (!hasNewer() && !m_messages.rows.isEmpty() && m_historyError.isEmpty())) { emit scrollToEnd(); return; }
    if (m_offline && !m_loading) { m_histories[m_peer].clear(); m_verifiedRanges.remove(m_peer); readCachedHistory(m_peer); return; }
    if (!m_loading) fetchHistory(false, true);
}

void Messenger::markVisibleRead(const QString &cmidString)
{
    const auto cmid = cmidString.toLongLong();
    const bool marked = m_rawConversations.value(m_peer).value("conversation").toObject().value("peer_flags").toInteger() & (1 << 20);
    if (!m_connected || m_demo || m_offline || m_authorizing || m_peer.isEmpty() || cmid <= 0
        || (cmid <= m_inRead.value(m_peer) && !marked) || m_readPending.value(m_peer) > 0) return;
    const auto peer = m_peer;
    m_readPending[peer] = cmid;
    api("messages.markAsRead", {{"peer_id", peer}, {"up_to_cmid", cmidString}, {"group_id", groupId()}, {"mark_conversation_as_read", "1"}},
        [this, peer, cmid](const QJsonValue &value, const QString &error) {
        m_readPending.remove(peer);
        if (!error.isEmpty()) { fail(error); return; }
        if (value.toInt() != 1 && !value.toBool()) { fail("VK не подтвердил прочтение сообщения."); return; }
        m_inRead[peer] = qMax(m_inRead.value(peer), cmid);
        auto info = m_rawConversations.value(peer); auto convo = info.value("conversation").toObject();
        convo.insert("peer_flags", convo.value("peer_flags").toInteger() & ~qint64(1 << 20));
        info.insert("conversation", convo); m_rawConversations[peer] = info;
        scheduleSync(true, false);
        emit changed();
    });
}

bool Messenger::attachRecording(const QString &peer)
{
    if (!m_connected || m_demo || peer.isEmpty() || !m_recorder.ready()) return false;
    if (!m_transfers.adoptRecording(m_recorder.file(), peer)) return false;
    m_recorder.release(); return true;
}

void Messenger::send(const QString &text) { sendWithOptions(text, false); }

void Messenger::sendWithOptions(const QString &text, bool silent)
{
    const auto attachments = m_transfers.attachments(m_peer);
    if ((text.trimmed().isEmpty() && attachments.isEmpty()) || m_peer.isEmpty() || m_sending) return;
    if (text.size() > 4096) { fail("Сообщение должно быть не длиннее 4096 символов."); return; }
    const auto peer = m_peer;
    if (m_demo) {
        m_demoHistory[peer].append(QVariantMap{{"id", OAuth::randomString()}, {"text", text}, {"outgoing", true},
            {"sentAt", QDateTime::currentSecsSinceEpoch()}, {"time", QDateTime::currentDateTime().toString("dd.MM · HH:mm")}, {"sender", "Вы"}});
        m_messages.replace(TimelinePresentation::decorate(m_demoHistory[peer]));
        auto rows = m_conversations.rows;
        for (auto &row : rows) {
            auto map = row.toMap();
            if (map.value("peer").toString() == peer) { map["preview"] = text; row = map; }
        }
        m_conversations.replace(rows);
        emit messageSent(peer);
        return;
    }
    if (!canWrite()) return;
    QJsonObject options{{"silent", silent}};
    QJsonArray files;
    for (const auto &attachment : attachments) files.append(attachment);
    options.insert("attachments", files);
    if (!m_replyMessage.isEmpty()) {
        options.insert("reply_cmid", m_replyMessage.value("cmid").toString());
        options.insert("preview", QJsonObject{{"text", text}, {"reply_message", QJsonObject::fromVariantMap(m_replyMessage)}});
    }
    queuePayload(peer, text, options);
}

void Messenger::queuePayload(const QString &peer, const QString &text, const QJsonObject &options)
{
    if (!m_connected || m_account.isEmpty() || peer.isEmpty() || text.size() > 4096) return;
    ++m_queueWrites; m_sending = true;
    auto pending = options;
    pending.insert("local_id", "local:" + QUuid::createUuid().toString(QUuid::WithoutBraces));
    pending.insert("peer", peer); pending.insert("text", text);
    pending.insert("created", QDateTime::currentSecsSinceEpoch());
    m_lastOutgoingOrder = qMax(QDateTime::currentMSecsSinceEpoch(), m_lastOutgoingOrder + 1);
    pending.insert("created_order", m_lastOutgoingOrder);
    pending.insert("state", "queued");
    pending.insert("random_id", QString::number(QRandomGenerator::system()->bounded(1, 2147483647)));
    QStringList attachments;
    for (auto file : pending.value("attachments").toArray()) attachments.append(file.toString());
    const auto generation = m_generation;
    emit changed();
    m_store.putOutgoing(m_account, pending, [this, generation, pending, peer, attachments](const QJsonObject &, const QString &error) {
        if (generation != m_generation) return;
        m_sending = --m_queueWrites > 0;
        if (!error.isEmpty()) { fail("Не удалось сохранить исходящее сообщение: " + error); return; }
        m_outgoing.append(pending);
        m_transfers.acknowledge(peer, attachments);
        if (!pending.value("preserve_draft").toBool() && m_peer == peer && m_replyMessage.value("cmid").toString() == pending.value("reply_cmid").toString()) clearReply();
        if (!pending.value("preserve_draft").toBool() && m_drafts.value(peer).value("text").toString() == pending.value("text").toString()) setDraftText(peer, {});
        flushDrafts();
        publishHistory();
        emit messageSent(peer);
        emit changed();
        pumpOutbox();
    });
}

QVariantList Messenger::methods() const { return Methods::catalog(); }

QVariantMap Messenger::methodDefaults(const QString &cmid) const
{
    QVariantMap values{{"peer_id", m_peer}, {"peer_ids", m_peer}, {"cmid", cmid}, {"cmids", cmid},
        {"conversation_message_ids", cmid}, {"extended", "1"}, {"count", "50"}, {"group_id", groupId()},
        {"lp_version", "21"}, {"fields", "photo_100"}, {"user_id", LongPoll::cursor(m_session.value("user_id"))}};
    if (m_peer.toLongLong() > 2000000000) values["chat_id"] = QString::number(m_peer.toLongLong() - 2000000000);
    const auto raw = m_histories.value(m_peer).value(cmid.toLongLong());
    if (!raw.isEmpty()) {
        values["message_id"] = id(raw.value("id"));
        values["message_ids"] = id(raw.value("id"));
        values["message"] = raw.value("text").toString();
        QStringList attachments;
        for (const auto &v : raw.value("attachments").toArray()) {
            const auto a = v.toObject(); const auto type = a.value("type").toString(); const auto object = a.value(type).toObject();
            if (object.contains("owner_id") && object.contains("id")) {
                auto attachment = type + id(object.value("owner_id")) + "_" + id(object.value("id"));
                if (!object.value("access_key").toString().isEmpty()) attachment += "_" + object.value("access_key").toString();
                attachments << attachment;
            }
        }
        values["attachment"] = attachments.join(',');
    }
    return values;
}

void Messenger::execute(const QString &method, const QVariantMap &parameters)
{
    if (m_operationBusy) return;
    m_operationError.clear(); m_operationResult = {};
    Form form;
    if (!m_connected || m_demo) m_operationError = "Для этой операции подключитесь с access_token.";
    else if (!Methods::encode(method, parameters, form, m_operationError)) {}
    else {
        if (method == "messages.send" && !parameters.contains("random_id"))
            form.append({"random_id", QString::number(QRandomGenerator::system()->bounded(1, 2147483647))});
        m_operationBusy = true;
        emit operationChanged();
        const auto targetPeer = parameters.value("peer_id").toString();
        api(method, form, [this, method, targetPeer, parameters](const QJsonValue &value, const QString &error) {
            m_operationBusy = false;
            m_operationError = error;
            if (error.isEmpty()) {
                m_operationResult = safeJson(value);
                if (method == "messages.delete" && value.isArray()) {
                    for (const auto &v : value.toArray()) {
                        const auto item = v.toObject();
                        if (item.value("response").toInt() != 1 || item.contains("error")) continue;
                        QVariantMap restore;
                        const auto cmid = item.value("conversation_message_id").toInteger();
                        if (cmid > 0) {
                            restore["peer_id"] = item.contains("peer_id") ? id(item.value("peer_id")) : targetPeer;
                            restore["cmid"] = QString::number(cmid);
                        } else if (item.value("message_id").toInteger() > 0) restore["message_id"] = id(item.value("message_id"));
                        if (!restore.isEmpty()) { restore["group_id"] = groupId(); m_restorable.append(restore); }
                    }
                    while (m_restorable.size() > 100) m_restorable.removeFirst();
                    emit changed();
                }
                if (method == "messages.restore" && (value.toInt() == 1 || value.toBool())) {
                    for (int i = m_restorable.size() - 1; i >= 0; --i)
                        if (m_restorable[i].toMap() == parameters) m_restorable.removeAt(i);
                    emit changed();
                }
                if (method == "messages.sendMessageEvent") {
                    const auto event = LongPoll::cursor(value);
                    if (!event.isEmpty()) m_callbacks[event] = QDateTime::currentMSecsSinceEpoch() + 60000;
                }
                if (method == "messages.createChat" || method == "messages.joinChatByInviteLink") {
                    const auto chat = value.isObject() ? value.toObject().value("chat_id").toInteger() : value.toInteger();
                    if (chat > 0) selectConversation(QString::number(chat + 2000000000), parameters.value("title", "Чат").toString());
                }
                if (Methods::mutates(method)) {
                    if ((value.isBool() && !value.toBool()) || (value.isDouble() && value.toInt() == 0))
                        m_operationError = "Сервер не подтвердил выполнение операции.";
                    else {
                        if ((method == "messages.markAsUnreadConversation" || method == "messages.deleteConversation") && targetPeer == m_peer) {
                            ++m_historyGeneration; m_peer.clear(); m_title.clear(); m_messages.replace({});
                            m_loading = m_fetchingHistory = false;
                            emit changed();
                        }
                        scheduleSync(true, true);
                    }
                }
                emit operationFinished(method, m_operationResult);
            }
            emit operationChanged();
        });
        return;
    }
    emit operationChanged();
}

void Messenger::setReply(const QString &cmid)
{
    const auto index = m_messages.find(cmid);
    if (index >= 0) {
        m_replyMessage = m_messages.get(index);
        m_drafts[m_peer].insert("reply", QJsonObject::fromVariantMap(m_replyMessage));
        if (!m_demo && !m_account.isEmpty()) { m_dirtyDrafts.insert(m_peer); m_draftTimer.start(); }
    }
    emit changed();
}
void Messenger::clearReply()
{
    m_replyMessage.clear(); m_drafts[m_peer].remove("reply");
    if (!m_demo && !m_account.isEmpty() && !m_peer.isEmpty()) { m_dirtyDrafts.insert(m_peer); m_draftTimer.start(); }
    emit changed();
}

void Messenger::openMessage(const QString &peer, const QString &cmid)
{
    if (m_demo) { selectConversation(peer, "Демонстрационная переписка"); emit scrollToMessage(cmid); return; }
    QString title = "Диалог " + peer;
    for (const auto &value : m_conversations.rows)
        if (value.toMap().value("peer").toString() == peer) title = value.toMap().value("title").toString();
    selectConversation(peer, title);
    ++m_historyGeneration; m_fetchingHistory = false; m_loading = false;
    m_histories[peer].clear(); m_verifiedRanges.remove(peer);
    readCachedHistory(peer, false, cmid.toLongLong());
}

void Messenger::typing()
{
    if (!m_connected || m_demo || m_offline || m_authorizing || m_peer.isEmpty()) return;
    const auto now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_lastTyping < 5000) return;
    m_lastTyping = now;
    api("messages.setActivity", {{"peer_id", m_peer}, {"type", "typing"}, {"group_id", groupId()}}, [](const QJsonValue &, const QString &) {});
}

QString Messenger::forwardJson(const QString &cmid) const
{
    const QJsonObject forward{{"peer_id", m_peer.toLongLong()}, {"conversation_message_ids", QJsonArray{cmid.toLongLong()}}};
    return QString::fromUtf8(QJsonDocument(forward).toJson(QJsonDocument::Compact));
}

void Messenger::restoreLast()
{
    if (!m_restorable.isEmpty()) execute("messages.restore", m_restorable.last().toMap());
}

void Messenger::startDemo()
{
    const auto saved = m_persistentSession ? Keychain::read() : QByteArray();
    logout();
    if (!saved.isEmpty()) Keychain::write(saved);
    m_demo = m_connected = true;
    m_media.setAccount("demo");
    const QStringList names{"Команда VK", "Анна Смирнова", "Дизайн и интерфейсы", "Избранное"};
    const QStringList texts{"Первый desktop-клиент уже можно попробовать ✨", "Привет! Как продвигается приложение?", "Давайте посмотрим новую версию интерфейса", "Здесь можно сохранять заметки и идеи"};
    QVariantList rows;
    for (int i = 0; i < names.size(); ++i) {
        const auto peer = QString::number(i + 1);
        rows.append(QVariantMap{{"peer", peer}, {"title", names[i]}, {"preview", texts[i]},
            {"unread", i == 1 ? 2 : 0}, {"avatar", ""}, {"time", "12:40"}});
        m_demoHistory[peer] = {
            QVariantMap{{"id", "1"}, {"text", "Привет! Это VK для macOS."}, {"outgoing", false}, {"sentAt", QDateTime::currentSecsSinceEpoch() - 120}, {"time", "12:38"}, {"sender", names[i]}},
            QVariantMap{{"id", "2"}, {"text", "Общий интерфейс на Qt Quick, а логика — на C++."}, {"outgoing", true}, {"sentAt", QDateTime::currentSecsSinceEpoch() - 60}, {"time", "12:39"}, {"sender", "Вы"}},
            QVariantMap{{"id", "3"}, {"text", texts[i]}, {"outgoing", false}, {"sentAt", QDateTime::currentSecsSinceEpoch()}, {"time", "12:40"}, {"sender", names[i]}}
        };
    }
    m_conversations.replace(rows);
    selectConversation("1", names.first());
    emit changed();
}

void Messenger::restoreAccount(const QJsonObject &session, std::function<void()> done)
{
    const auto user = LongPoll::cursor(session.value("user_id"));
    const auto group = LongPoll::cursor(session.value("group_id"));
    const auto account = group.isEmpty() || group == "0" ? "user:" + user : "group:" + group;
    if (user.isEmpty() || user == "0" || m_account == account) { done(); return; }
    m_account = account;
    m_media.setAccount(account);
    const auto generation = m_generation;
    m_store.loadAccount(account, [this, generation, account, done](const QJsonObject &value, const QString &error) {
        if (generation != m_generation || account != m_account) return;
        if (!error.isEmpty()) fail("Не удалось открыть сохранённые переписки: " + error);
        m_folders.restore(value.value("catalogs").toObject().value("folders").toObject());
        m_actions.restoreReactionAssets(value.value("catalogs").toObject().value("reactions").toObject());
        const auto conversations = value.value("conversations").toObject();
        for (auto it = conversations.begin(); it != conversations.end(); ++it) {
            m_rawConversations[it.key()] = it.value().toObject();
            const auto convo = it.value().toObject().value("conversation").toObject();
            m_lastCmid[it.key()] = convo.value("last_conversation_message_id").toInteger();
            m_inRead[it.key()] = convo.value("in_read_cmid").toInteger();
            m_outRead[it.key()] = convo.value("out_read_cmid").toInteger();
        }
        const auto peers = value.value("peers").toObject();
        for (auto it = peers.begin(); it != peers.end(); ++it) m_peers[it.key()] = it.value().toObject();
        m_version = value.value("version").toString();
        m_unreadCount = value.value("unread").toInt();
        if (!conversations.isEmpty() && m_authorizing) { m_connected = true; m_offline = true; }
        rebuildConversations();
        m_store.loadDrafts(account, [this, generation, account, done](const QJsonObject &drafts, const QString &draftError) {
            if (generation != m_generation || account != m_account) return;
            if (!draftError.isEmpty()) fail("Не удалось открыть черновики: " + draftError);
            for (auto it = drafts.begin(); it != drafts.end(); ++it) m_drafts[it.key()] = it.value().toObject();
            const auto root = m_store.directory();
            m_restoringTransfers = true;
            m_transfers.setWorkspace(root + "/uploads/" + QCryptographicHash::hash(account.toUtf8(), QCryptographicHash::Sha256).toHex());
            m_transfers.restoreState(m_drafts.value("__transfers"));
            m_restoringTransfers = false;
            m_store.loadOutgoing(account, [this, generation, account, done](const QJsonObject &value, const QString &outboxError) {
                if (generation != m_generation || account != m_account) return;
                if (!outboxError.isEmpty()) fail("Не удалось открыть очередь отправки: " + outboxError);
                for (const auto &item : value.value("items").toArray()) {
                    auto pending = item.toObject();
                    m_lastOutgoingOrder = qMax(m_lastOutgoingOrder, pending.value("created_order").toInteger());
                    if (pending.value("confirmed").toBool() || pending.value("state").toString() == "cancelled") {
                        m_store.removeOutgoing(account, pending.value("local_id").toString()); continue;
                    }
                    if (pending.value("state").toString() == "sending") pending.insert("state", "queued");
                    m_outgoing.append(pending);
                }
                if (!m_peer.isEmpty()) m_replyMessage = m_drafts.value(m_peer).value("reply").toObject().toVariantMap();
                emit draftChanged(); emit changed();
                done();
            });
        });
    });
}

void Messenger::setDraftText(const QString &peer, const QString &text)
{
    if (peer.isEmpty() || m_drafts.value(peer).value("text").toString() == text) return;
    auto draft = m_drafts.value(peer);
    if (text.isEmpty()) draft.remove("text"); else draft.insert("text", text);
    m_drafts[peer] = draft;
    if (!m_demo && !m_account.isEmpty()) { m_dirtyDrafts.insert(peer); m_draftTimer.start(); }
}

void Messenger::flushDrafts()
{
    m_draftTimer.stop();
    if (!m_demo && !m_account.isEmpty()) for (const auto &peer : m_dirtyDrafts) {
        const auto generation = m_generation;
        m_store.saveDraft(m_account, peer, m_drafts.value(peer), [this, generation, peer](const QJsonObject &, const QString &error) {
            if (generation == m_generation && !error.isEmpty()) { m_dirtyDrafts.insert(peer); fail("Не удалось сохранить черновик: " + error); }
        });
    }
    m_dirtyDrafts.clear();
}

void Messenger::readCachedHistory(const QString &peer, bool older, qint64 anchor)
{
    const auto generation = m_historyGeneration;
    const auto revision = m_contentRevision.value(peer);
    const auto before = anchor >= 0 ? qMin(anchor, std::numeric_limits<qint64>::max() - 101) + 101 : older && !m_histories.value(peer).isEmpty() ? m_histories.value(peer).firstKey() : 0;
    m_loading = true;
    emit changed();
    m_store.loadHistory(m_account, peer, [this, generation, revision, peer, older, anchor](const QJsonObject &value, const QString &error) {
        if (generation != m_historyGeneration || peer != m_peer) return;
        m_loading = false;
        if (revision == m_contentRevision.value(peer)) {
            for (const auto &item : value.value("messages").toArray()) {
                auto message = item.toObject();
                const auto cmid = message.value("conversation_message_id").toInteger();
                if (!m_histories[peer].contains(cmid)) m_histories[peer][cmid] = message;
            }
            for (const auto &cmid : value.value("dirty").toArray()) m_contentDirty[peer].insert(cmid.toInteger());
            for (auto item : value.value("ranges").toArray()) {
                auto range = item.toArray();
                if (range.size() == 2) m_verifiedRanges[peer].append({range[0].toInteger(), range[1].toInteger()});
            }
            m_older[peer] = value.value("older").toBool(true);
            while (m_histories[peer].size() > 1000) {
                if (older) m_histories[peer].erase(std::prev(m_histories[peer].end()));
                else { m_histories[peer].erase(m_histories[peer].begin()); m_older[peer] = true; }
            }
            if (older && m_offline && value.value("messages").toArray().isEmpty()) {
                m_older[peer] = false; m_historyError = "Более ранняя история не сохранена. Подключитесь к сети для загрузки.";
            }
            m_newer[peer] = !m_histories.value(peer).isEmpty() && m_histories.value(peer).lastKey() < m_lastCmid.value(peer);
            reconcileOutbox(); publishHistory();
            if (anchor >= 0) emit scrollToMessage(QString::number(anchor));
            else if (!older && m_offline) emit scrollToEnd();
        }
        if (!error.isEmpty()) m_historyError = "Не удалось прочитать кэш: " + error;
        if (!m_offline) fetchHistory(older, !older && anchor < 0, anchor);
        emit changed();
    }, before);
}

void Messenger::persistHistory(const QString &peer)
{
    QJsonArray messages, ranges;
    for (const auto &message : m_histories.value(peer)) messages.append(message);
    for (const auto &range : m_verifiedRanges.value(peer)) ranges.append(QJsonArray{range.first, range.second});
    const auto generation = m_generation;
    m_store.saveHistory(m_account, peer, messages, ranges, m_older.value(peer), m_newer.value(peer),
        [this, generation](const QJsonObject &, const QString &error) {
        if (generation == m_generation && !error.isEmpty()) fail("Не удалось сохранить историю: " + error);
    });
}

void Messenger::pumpOutbox()
{
    if (m_outboxSending || !m_connected || m_demo || m_offline || m_authorizing || m_syncBlocked || m_token.isEmpty()) return;
    int index = -1;
    QSet<QString> blockedPeers;
    for (int i = 0; i < m_outgoing.size(); ++i) {
        const auto peer = m_outgoing[i].value("peer").toString();
        const auto state = m_outgoing[i].value("state").toString();
        if (state == "failed") blockedPeers.insert(peer);
        if ((state == "queued" || state == "uncertain") && !blockedPeers.contains(peer)) { index = i; break; }
    }
    if (index < 0) return;
    auto pending = m_outgoing[index];
    pending.insert("state", "sending");
    m_outgoing[index] = pending;
    m_outboxSending = true;
    publishHistory();
    const auto generation = m_generation;
    const auto localId = pending.value("local_id").toString();
    // The stable random_id and payload are already durable before this request.
    Form params{{"peer_id", pending.value("peer").toString()}, {"message", pending.value("text").toString()},
        {"random_id", pending.value("random_id").toString()}, {"group_id", groupId()}};
    QStringList attachments;
    for (const auto &file : pending.value("attachments").toArray()) attachments.append(file.toString());
    if (!attachments.isEmpty()) params.append({"attachment", attachments.join(',')});
    if (!pending.value("reply_cmid").toString().isEmpty()) {
        QJsonObject forward{{"peer_id", pending.value("peer").toString().toLongLong()},
            {"conversation_message_ids", QJsonArray{pending.value("reply_cmid").toString().toLongLong()}}, {"is_reply", true}};
        params.append({"forward", QString::fromUtf8(QJsonDocument(forward).toJson(QJsonDocument::Compact))});
    }
    if (pending.value("forward").isObject()) params.append({"forward", QString::fromUtf8(QJsonDocument(pending.value("forward").toObject()).toJson(QJsonDocument::Compact))});
    if (pending.value("sticker_id").toInt() > 0) params.append({"sticker_id", QString::number(pending.value("sticker_id").toInt())});
    if (pending.value("silent").toBool()) params.emplaceBack("silent", "1");
    if (!pending.value("payload").toString().isEmpty()) params.append({"payload", pending.value("payload").toString()});
    api("messages.send", params, [this, generation, localId, peer = pending.value("peer").toString()](const QJsonValue &value, const QString &error) {
        if (generation != m_generation) return;
        m_outboxSending = false;
        for (int i = 0; i < m_outgoing.size(); ++i) {
            auto pending = m_outgoing[i];
            if (pending.value("local_id").toString() != localId) continue;
            if (pending.value("confirmed").toBool()) break;
            auto failure = error;
            const auto globalId = value.isObject() ? value.toObject().value("message_id").toInteger() : value.toInteger();
            const auto cmid = value.toObject().value("conversation_message_id").toInteger();
            if (failure.isEmpty() && globalId <= 0 && cmid <= 0) failure = "Сервер не подтвердил отправку. Можно повторить без создания копии.";
            pending.insert("state", failure.isEmpty() ? "sent" : m_offline ? "uncertain" : "failed");
            pending.insert("error", failure);
            if (globalId > 0) pending.insert("global_id", QString::number(globalId));
            if (cmid > 0) pending.insert("cmid", QString::number(cmid));
            m_outgoing[i] = pending;
            m_store.putOutgoing(m_account, pending, [this, generation](const QJsonObject &, const QString &error) {
                if (generation == m_generation && !error.isEmpty()) fail("Не удалось сохранить состояние отправки: " + error);
            });
            break;
        }
        reconcileOutbox(); publishHistory();
        if (error.isEmpty()) {
            if (peer == m_peer) { ++m_historyGeneration; m_fetchingHistory = m_loading = false; }
            scheduleSync(true, peer == m_peer);
        }
        emit changed();
        pumpOutbox();
    });
}

void Messenger::reconcileOutbox()
{
    for (int i = m_outgoing.size() - 1; i >= 0; --i) {
        const auto pending = m_outgoing[i];
        bool found = false;
        for (const auto &message : m_histories.value(pending.value("peer").toString())) {
            if (message.value("out").toInt() != 1) continue;
            found |= (message.value("random_id").toInteger() > 0 && LongPoll::cursor(message.value("random_id")) == pending.value("random_id").toString());
            found |= (!pending.value("global_id").toString().isEmpty() && id(message.value("id")) == pending.value("global_id").toString());
            found |= (!pending.value("cmid").toString().isEmpty() && id(message.value("conversation_message_id")) == pending.value("cmid").toString());
        }
        if (!found) continue;
        if (pending.value("confirmed").toBool()) continue;
        const auto localId = pending.value("local_id").toString();
        auto confirmed = pending;
        confirmed.insert("confirmed", true); confirmed.insert("state", "sent");
        for (const auto &message : m_histories.value(pending.value("peer").toString())) {
            if (message.value("out").toInt() != 1) continue;
            if ((message.value("random_id").toInteger() > 0 && LongPoll::cursor(message.value("random_id")) == pending.value("random_id").toString())
                || (!pending.value("global_id").toString().isEmpty() && id(message.value("id")) == pending.value("global_id").toString())
                || (!pending.value("cmid").toString().isEmpty() && id(message.value("conversation_message_id")) == pending.value("cmid").toString())) {
                const auto cmid = message.value("conversation_message_id").toInteger();
                m_localIdentities[pending.value("peer").toString()][cmid] = localId;
                confirmed.insert("cmid", QString::number(cmid));
                confirmed.insert("global_id", id(message.value("id")));
            }
        }
        m_outgoing[i] = confirmed;
        const auto generation = m_generation;
        m_store.putOutgoing(m_account, confirmed, [this, generation, localId](const QJsonObject &, const QString &error) {
            if (generation != m_generation) return;
            if (!error.isEmpty()) { fail("Не удалось сохранить подтверждение отправки: " + error); return; }
            m_store.removeOutgoing(m_account, localId, [this, generation, localId](const QJsonObject &, const QString &error) {
                if (generation != m_generation) return;
                if (!error.isEmpty()) { fail("Не удалось обновить очередь отправки: " + error); return; }
                for (int j = m_outgoing.size() - 1; j >= 0; --j) if (m_outgoing[j].value("local_id").toString() == localId) m_outgoing.removeAt(j);
                publishHistory(); emit changed();
            });
        });
    }
}

void Messenger::retryOutgoing(const QString &localId)
{
    for (auto &pending : m_outgoing) if (pending.value("local_id").toString() == localId && !pending.value("confirmed").toBool() && (pending.value("state").toString() == "failed" || pending.value("state").toString() == "uncertain" || pending.value("state").toString() == "queued")) {
        pending.insert("state", "queued"); pending.remove("error");
        m_store.putOutgoing(m_account, pending, {});
    }
    m_offline = false;
    publishHistory(); pumpOutbox(); reload(); emit changed();
}

void Messenger::cancelOutgoing(const QString &localId)
{
    for (int i = 0; i < m_outgoing.size(); ++i) {
        const auto pending = m_outgoing[i];
        if (pending.value("local_id").toString() != localId) continue;
        if (pending.value("state").toString() == "sending" || pending.value("state").toString() == "sent") return;
        auto cancelled = pending; cancelled.insert("state", "cancelled");
        m_outgoing[i] = cancelled;
        const auto generation = m_generation;
        m_store.putOutgoing(m_account, cancelled, [this, generation, localId, pending](const QJsonObject &, const QString &error) {
            if (generation != m_generation) return;
            if (!error.isEmpty()) {
                for (auto &item : m_outgoing) if (item.value("local_id").toString() == localId) item = pending;
                fail("Не удалось отменить отправку: " + error); return;
            }
            m_store.removeOutgoing(m_account, localId);
            for (int j = m_outgoing.size() - 1; j >= 0; --j) if (m_outgoing[j].value("local_id").toString() == localId) m_outgoing.removeAt(j);
            publishHistory(); pumpOutbox(); emit changed();
        });
        break;
    }
}

void Messenger::refreshCacheStats()
{
    const auto generation = m_generation;
    m_store.statistics(m_account, [this, generation](const QJsonObject &value, const QString &error) {
        if (generation != m_generation) return;
        if (!error.isEmpty()) { fail(error); return; }
        m_cacheStats = value.toVariantMap(); emit cacheStatsChanged();
    });
}

void Messenger::clearHistoryCache()
{
    if (m_demo || m_account.isEmpty() || m_sending) return;
    m_transfers.pause();
    ++m_generation; ++m_historyGeneration; m_api.cancel(); m_longPoll.stop(); m_actions.resetAccount(); m_profileActions.resetAccount(); m_folders.reset();
    m_readPending.clear(); m_operationBusy = false; emit operationChanged();
    m_fetchingConversations = m_fetchingHistory = m_loading = false;
    m_outboxSending = false;
    for (auto &pending : m_outgoing) if (pending.value("state").toString() == "sending") pending.insert("state", "uncertain");
    const auto generation = m_generation;
    m_store.clearHistory(m_account, [this, generation](const QJsonObject &, const QString &error) {
        if (generation != m_generation) return;
        if (!error.isEmpty()) { fail(error); return; }
        ++m_historyGeneration;
        m_histories.clear(); m_verifiedRanges.clear(); m_older.clear(); m_newer.clear(); m_contentDirty.clear();
        m_version.clear(); m_needCredentials = true;
        m_fetchingConversations = m_fetchingHistory = m_loading = false;
        m_longPoll.stop();
        publishHistory(); refreshCacheStats(); if (!m_offline) m_transfers.resume(); reload();
    });
}

bool Messenger::reduceMotion() const
{
    return m_settings.value("ui/reduceMotion", false).toBool() || systemReduceMotion();
}

bool Messenger::openDeepLink(const QUrl &url)
{
    if (url.scheme() != "vkm" || url.host() != "chat") return false;
    bool valid = false;
    const auto peer = url.path().mid(1).toLongLong(&valid);
    if (!valid || peer == 0) return false;
    const QUrlQuery query(url);
    if (query.hasQueryItem("message") && query.queryItemValue("message").toLongLong() <= 0) return false;
    if (!m_connected || m_authorizing) { m_pendingLink = url; return true; }
    const auto cmid = query.queryItemValue("message");
    if (cmid.isEmpty()) selectConversation(QString::number(peer), "Переписка");
    else openMessage(QString::number(peer), cmid);
    return true;
}
void Messenger::openPendingLink()
{
    if (m_pendingLink.isEmpty() || !m_connected || m_authorizing) return;
    const auto link = m_pendingLink; m_pendingLink = QUrl(); openDeepLink(link);
}
