#pragma once
#include "OAuth.h"
#include "LongPoll.h"
#include "Transfers.h"
#include "Json.h"
#include "storage/LocalStore.h"
#include "api/VkApiClient.h"
#include "messaging/ActionController.h"
#include "messaging/FolderController.h"
#include "media/MediaCache.h"
#include "media/VoiceRecorder.h"
#include <QAbstractListModel>
#include <QJsonObject>
#include <QSettings>
#include <QQueue>
#include <QMap>
#include <QSet>
#include <functional>
#include <memory>

class ItemModel : public QAbstractListModel {
    Q_OBJECT
public:
    explicit ItemModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : rows.size(); }
    QVariant data(const QModelIndex &index, int role) const override {
        return index.isValid() && index.row() < rows.size() && role == Qt::UserRole
            ? rows.at(index.row()) : QVariant();
    }
    QHash<int, QByteArray> roleNames() const override { return {{Qt::UserRole, "item"}}; }
    void replace(const QVariantList &value) {
        if (rows == value) return;
        beginResetModel(); rows = value; endResetModel();
    }
    void reconcile(const QVariantList &value) {
        const auto identity = [](const QVariant &value) { const auto row = value.toMap(); return row.value("identity", row.value("id")); };
        for (int i = 0; i < value.size(); ++i) {
            const auto key = identity(value[i]);
            if (i < rows.size() && identity(rows[i]) == key) {
                if (rows[i] != value[i]) { rows[i] = value[i]; emit dataChanged(index(i), index(i)); }
                continue;
            }
            int found = -1;
            for (int j = i + 1; j < rows.size(); ++j)
                if (identity(rows[j]) == key) { found = j; break; }
            if (found >= 0) {
                beginMoveRows({}, found, found, {}, i);
                rows.insert(i, rows.takeAt(found));
                endMoveRows();
                if (rows[i] != value[i]) { rows[i] = value[i]; emit dataChanged(index(i), index(i)); }
            } else {
                beginInsertRows({}, i, i); rows.insert(i, value[i]); endInsertRows();
            }
        }
        if (rows.size() > value.size()) {
            beginRemoveRows({}, value.size(), rows.size() - 1);
            rows.remove(value.size(), rows.size() - value.size()); endRemoveRows();
        }
    }
    Q_INVOKABLE QVariantMap get(int row) const { return row >= 0 && row < rows.size() ? rows[row].toMap() : QVariantMap(); }
    Q_INVOKABLE int find(const QString &id) const {
        for (int i = 0; i < rows.size(); ++i) {
            const auto row = rows[i].toMap();
            if (row.value("id").toString() == id || row.value("identity").toString() == id) return i;
        }
        return -1;
    }
    QVariantList rows;
};

class Messenger : public QObject {
    Q_OBJECT
    friend class ActionController;
    friend class FolderController;
    Q_PROPERTY(ItemModel *conversations READ conversations CONSTANT)
    Q_PROPERTY(ItemModel *messages READ messages CONSTANT)
    Q_PROPERTY(bool connected READ connected NOTIFY changed)
    Q_PROPERTY(bool demo READ demo NOTIFY changed)
    Q_PROPERTY(bool sending READ sending NOTIFY changed)
    Q_PROPERTY(bool loading READ loading NOTIFY changed)
    Q_PROPERTY(bool authorizing READ authorizing NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QString selectedPeer READ selectedPeer NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString clientId MEMBER m_clientId NOTIFY changed)
    Q_PROPERTY(QString redirectUri MEMBER m_redirectUri NOTIFY changed)
    Q_PROPERTY(QString scope MEMBER m_scope NOTIFY changed)
    Q_PROPERTY(QString peerActivity READ peerActivity NOTIFY changed)
    Q_PROPERTY(int unreadCount MEMBER m_unreadCount NOTIFY changed)
    Q_PROPERTY(bool hasOlder READ hasOlder NOTIFY changed)
    Q_PROPERTY(bool hasNewer READ hasNewer NOTIFY changed)
    Q_PROPERTY(QString historyError MEMBER m_historyError NOTIFY changed)
    Q_PROPERTY(QVariantList methods READ methods CONSTANT)
    Q_PROPERTY(bool operationBusy MEMBER m_operationBusy NOTIFY operationChanged)
    Q_PROPERTY(QString operationError MEMBER m_operationError NOTIFY operationChanged)
    Q_PROPERTY(QVariant operationResult MEMBER m_operationResult NOTIFY operationChanged)
    Q_PROPERTY(QVariantMap reply READ reply NOTIFY changed)
    Q_PROPERTY(QVariantList eventHistory MEMBER m_eventHistory NOTIFY changed)
    Q_PROPERTY(Transfers *transfers READ transfers CONSTANT)
    Q_PROPERTY(QVariantMap currentKeyboard READ currentKeyboard NOTIFY changed)
    Q_PROPERTY(QVariantMap pinnedMessage READ pinnedMessage NOTIFY changed)
    Q_PROPERTY(bool canWrite READ canWrite NOTIFY changed)
    Q_PROPERTY(ActionController *actions READ actions CONSTANT)
    Q_PROPERTY(FolderController *folders READ folders CONSTANT)
    Q_PROPERTY(MediaCache *media READ media CONSTANT)
    Q_PROPERTY(VoiceRecorder *recorder READ recorder CONSTANT)
    Q_PROPERTY(bool isChat READ isChat NOTIFY changed)
    Q_PROPERTY(bool canManageChat READ canManageChat NOTIFY changed)
    Q_PROPERTY(bool apiInspectorEnabled READ apiInspectorEnabled CONSTANT)
    Q_PROPERTY(bool loginConfigured READ loginConfigured NOTIFY changed)
    Q_PROPERTY(bool reduceMotion READ reduceMotion WRITE setReduceMotion NOTIFY changed)
    Q_PROPERTY(bool offline READ offline NOTIFY changed)
    Q_PROPERTY(int outgoingCount READ outgoingCount NOTIFY changed)
    Q_PROPERTY(QString draftText READ draftText NOTIFY draftChanged)
    Q_PROPERTY(QVariantMap cacheStats MEMBER m_cacheStats NOTIFY cacheStatsChanged)
    Q_PROPERTY(QString toast MEMBER m_toast NOTIFY changed)
    Q_PROPERTY(QUrl toastUrl MEMBER m_toastUrl NOTIFY changed)
    Q_PROPERTY(QVariantList restorable MEMBER m_restorable NOTIFY changed)
public:
    explicit Messenger(QObject *parent = nullptr, QNetworkAccessManager *network = nullptr, const QString &storagePath = {}, bool persistentSession = true);
    ~Messenger() override;
    ItemModel *conversations() { return &m_conversations; }
    ItemModel *messages() { return &m_messages; }
    bool connected() const { return m_connected; }
    bool demo() const { return m_demo; }
    bool sending() const { return m_sending; }
    bool loading() const { return m_loading; }
    bool authorizing() const { return m_authorizing; }
    QString title() const { return m_title; }
    QString selectedPeer() const { return m_peer; }
    QString error() const { return m_error; }
    QString status() const { return m_demo ? "Деморежим · локальные сообщения" : m_offline ? "Нет сети · сохранённые переписки" : m_status; }
    bool offline() const { return m_offline; }
    bool loginConfigured() const { return !m_clientId.isEmpty(); }
    int outgoingCount() const { return m_outgoing.size(); }
    bool reduceMotion() const;
    Q_INVOKABLE void refreshPreferences() { emit changed(); }
    Q_INVOKABLE bool openDeepLink(const QUrl &url);
    void setReduceMotion(bool value) { m_settings.setValue("ui/reduceMotion", value); emit changed(); }
    bool apiInspectorEnabled() const {
#if VKM_ENABLE_API_INSPECTOR
        return true;
#else
        return false;
#endif
    }
    Q_INVOKABLE bool isPerson(const QString &id) const { bool ok; auto value = id.toLongLong(&ok); return ok && value > 0 && value < 2000000000; }
    QString draftText() const { return m_drafts.value(m_peer).value("text").toString(); }
    QString peerActivity() const;
    bool hasOlder() const { return m_older.value(m_peer, true); }
    bool hasNewer() const { return m_newer.value(m_peer, false); }
    QVariantList methods() const;
    QVariantMap reply() const { return m_replyMessage; }
    Transfers *transfers() { return &m_transfers; }
    ActionController *actions() { return &m_actions; }
    FolderController *folders() { return &m_folders; }
    MediaCache *media() { return &m_media; }
    VoiceRecorder *recorder() { return &m_recorder; }
    bool isChat() const { return m_peer.toLongLong() > 2000000000; }
    bool canManageChat() const {
        const auto settings = m_rawConversations.value(m_peer).value("conversation").toObject().value("chat_settings").toObject();
        return isChat() && (settings.value("is_admin").toBool() || settings.value("owner_id").toInteger() == m_session.value("user_id").toInteger());
    }
    QVariantMap currentKeyboard() const { return safeJson(m_rawConversations.value(m_peer).value("conversation").toObject().value("current_keyboard")).toMap(); }
    QVariantMap pinnedMessage() const { return safeJson(m_rawConversations.value(m_peer).value("conversation").toObject().value("chat_settings").toObject().value("pinned_message")).toMap(); }
    bool canWrite() const { return m_demo || m_rawConversations.value(m_peer).value("conversation").toObject().value("can_write").toObject().value("allowed").toBool(true); }
    void applyLongPoll(const QJsonArray &events);
    Q_INVOKABLE void login();
    Q_INVOKABLE void loginWithToken(const QString &token, const QString &group = {});
    Q_INVOKABLE void completeLogin(const QString &callback) { m_oauth.complete(callback); }
    Q_INVOKABLE void startDemo();
    Q_INVOKABLE void logout();
    Q_INVOKABLE void selectConversation(const QString &peer, const QString &title);
    Q_INVOKABLE void send(const QString &text);
    Q_INVOKABLE bool attachRecording(const QString &peer);
    Q_INVOKABLE void sendWithOptions(const QString &text, bool silent);
    Q_INVOKABLE void setDraftText(const QString &peer, const QString &text);
    Q_INVOKABLE void retryOutgoing(const QString &localId);
    Q_INVOKABLE void cancelOutgoing(const QString &localId);
    Q_INVOKABLE void refreshCacheStats();
    Q_INVOKABLE void clearHistoryCache();
    Q_INVOKABLE void reload();
    Q_INVOKABLE void loadOlder();
    Q_INVOKABLE void loadGap(const QString &first, const QString &last);
    Q_INVOKABLE void jumpToEnd();
    Q_INVOKABLE void markVisibleRead(const QString &cmid);
    Q_INVOKABLE void execute(const QString &method, const QVariantMap &parameters);
    Q_INVOKABLE QVariantMap methodDefaults(const QString &cmid = {}) const;
    Q_INVOKABLE void setReply(const QString &cmid);
    Q_INVOKABLE void clearReply();
    Q_INVOKABLE void openMessage(const QString &peer, const QString &cmid);
    Q_INVOKABLE void typing();
    Q_INVOKABLE QString forwardJson(const QString &cmid) const;
    Q_INVOKABLE void restoreLast();
    Q_INVOKABLE void dismissRestore() { if (!m_restorable.isEmpty()) { m_restorable.removeLast(); emit changed(); } }
    Q_INVOKABLE void clearError() { m_error.clear(); emit changed(); }
signals:
    void changed();
    void messageSent(const QString &peer);
    void realtimeEvent(int type, const QJsonArray &payload);
    void scrollToEnd();
    void historyAboutToChange();
    void historyChanged();
    void scrollToMessage(const QString &cmid);
    void operationChanged();
    void operationFinished(const QString &method, const QVariant &result);
    void draftChanged();
    void cacheStatsChanged();
private:
    using Callback = std::function<void(const QJsonValue &, const QString &)>;
    void api(const QString &method, Form parameters, Callback callback);
    void fetchConversations();
    void fetchHistory(bool older = false, bool latest = false, qint64 anchor = -1, bool keepWindow = false);
    struct DiffTransaction {
        quint64 generation;
        QString from, to;
        bool full, credentials;
        QList<QJsonObject> pages;
        QSet<QString> sources;
    };
    void fetchDiffPage(const std::shared_ptr<DiffTransaction> &transaction, const QString &source = {});
    void applyDiff(const DiffTransaction &transaction);
    void rebuildConversations();
    void publishHistory();
    void acceptSession(QJsonObject session);
    void beginTokenLogin(const QString &token, const QString &group = {});
    QString groupId() const { return LongPoll::cursor(m_session.value("group_id")).isEmpty() ? QString("0") : LongPoll::cursor(m_session.value("group_id")); }
    void fail(const QString &error);
    void restoreAccount(const QJsonObject &session, std::function<void()> done);
    void flushDrafts();
    void pumpOutbox();
    void queuePayload(const QString &peer, const QString &text, const QJsonObject &options);
    void openPendingLink();
    void reconcileOutbox();
    void persistHistory(const QString &peer);
    void readCachedHistory(const QString &peer, bool older = false, qint64 anchor = -1);
    void scheduleSync(bool conversations, bool history);
    void drainSync();
    QVariantMap message(const QJsonObject &object) const;
    QSettings m_settings;
    LocalStore m_store;
    ActionController m_actions;
    FolderController m_folders;
    MediaCache m_media;
    VoiceRecorder m_recorder;
    QString m_account;
    bool m_persistentSession = true;
    QUrl m_pendingLink;
    QHash<QString, QJsonObject> m_drafts;
    QSet<QString> m_dirtyDrafts;
    QList<QJsonObject> m_outgoing;
    QTimer m_draftTimer, m_reconnectTimer;
    QVariantMap m_cacheStats;
    bool m_offline = false, m_outboxSending = false;
    bool m_restoringTransfers = false;
    int m_queueWrites = 0;
    qint64 m_lastOutgoingOrder = 0;
    OAuth m_oauth;
    LongPoll m_longPoll;
    Transfers m_transfers;
    VkApiClient m_api;
    QTimer m_sync, m_refresh, m_activityExpiry, m_toastTimer;
    ItemModel m_conversations, m_messages;
    QJsonObject m_session;
    QHash<QString, QJsonObject> m_rawConversations, m_peers;
    QHash<QString, QMap<qint64, QJsonObject>> m_histories;
    QHash<QString, QHash<qint64, QString>> m_localIdentities;
    QHash<QString, qint64> m_lastCmid, m_inRead, m_readPending;
    QHash<QString, QSet<qint64>> m_contentDirty;
    QHash<QString, quint64> m_contentRevision;
    QHash<QString, QList<QPair<qint64, qint64>>> m_verifiedRanges;
    QHash<QString, bool> m_older, m_newer;
    QString m_version, m_historyError;
    bool m_needCredentials = true;
    bool m_operationBusy = false;
    QString m_operationError;
    QVariant m_operationResult;
    QVariantMap m_replyMessage;
    QVariantList m_eventHistory;
    qint64 m_lastTyping = 0;
    QHash<QString, qint64> m_callbacks;
    QString m_toast;
    QUrl m_toastUrl;
    QVariantList m_restorable;
    QHash<QString, QVariantList> m_demoHistory;
    QString m_clientId, m_redirectUri, m_scope, m_token, m_peer, m_title, m_error, m_status;
    QString m_pendingPeer, m_pendingText, m_pendingRandomId;
    struct Activity { QString text; qint64 expires; };
    QHash<QString, Activity> m_activity;
    QHash<QString, bool> m_online;
    QHash<QString, quint64> m_presenceRevision;
    QHash<QString, qint64> m_outRead;
    int m_unreadCount = 0;
    quint64 m_generation = 0, m_historyGeneration = 0;
    struct DeferredHistory { bool valid = false, older = false, latest = false, keepWindow = false; qint64 anchor = -1; quint64 generation = 0; };
    DeferredHistory m_deferredHistory;
    quint64 m_eventRevision = 0;
    bool m_connected = false, m_demo = false, m_sending = false, m_loading = false;
    bool m_authorizing = false, m_fetchingConversations = false, m_fetchingHistory = false;
    bool m_conversationsDirty = false, m_historyDirty = false;
    bool m_syncBlocked = false;
};
