#pragma once
#include "Form.h"
#include <QObject>
#include <QUrl>
#include <QJsonValue>
#include <QVariantList>
#include <functional>

class Messenger;

class ActionController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QVariantList results READ results NOTIFY changed)
    Q_PROPERTY(QString inviteLink READ inviteLink NOTIFY changed)
    Q_PROPERTY(bool hasMore READ hasMore NOTIFY changed)
    Q_PROPERTY(bool cachedResults READ cachedResults NOTIFY changed)
    Q_PROPERTY(QVariantList reactionAssets READ reactionAssets NOTIFY reactionAssetsChanged)
    Q_PROPERTY(QVariantList selectableReactions READ selectableReactions NOTIFY reactionAssetsChanged)
public:
    explicit ActionController(Messenger *messenger);
    bool busy() const { return m_busy; }
    QString error() const { return m_error; }
    QVariantList results() const { return m_results; }
    QString inviteLink() const { return m_inviteLink; }
    bool hasMore() const { return m_hasMore; }
    bool cachedResults() const { return m_cachedResults; }
    QVariantList reactionAssets() const { return m_reactionAssets; }
    QVariantList selectableReactions() const { return m_selectableReactions; }
    void resetAccount();
    void cancelPending();
    void restoreReactionAssets(const QJsonObject &catalog);
    void cacheReactionAssets(const QJsonObject &catalog);
    Q_INVOKABLE QString reactionImage(int reaction) const;
    Q_INVOKABLE void ensureReactionAssets();
    Q_INVOKABLE void reset();
    Q_INVOKABLE void editMessage(const QString &peer, const QString &cmid, const QString &text);
    Q_INVOKABLE void deleteMessages(const QString &peer, const QStringList &cmids, bool forEveryone);
    Q_INVOKABLE void forwardMessages(const QString &sourcePeer, const QStringList &cmids, const QString &destination, const QString &comment);
    Q_INVOKABLE void setImportant(const QString &peer, const QString &cmid, bool important);
    Q_INVOKABLE void setPinnedMessage(const QString &peer, const QString &cmid, bool pinned);
    Q_INVOKABLE void conversationAction(const QString &peer, const QString &action);
    Q_INVOKABLE void renameChat(const QString &peer, const QString &title);
    Q_INVOKABLE void createChat(const QString &title, const QStringList &people);
    Q_INVOKABLE void loadMembers(const QString &peer, bool nextPage = false);
    Q_INVOKABLE void addMembers(const QString &peer, const QStringList &people);
    Q_INVOKABLE void removeMember(const QString &peer, const QString &member);
    Q_INVOKABLE void setMemberRole(const QString &peer, const QString &member, bool administrator);
    Q_INVOKABLE void restrictMember(const QString &peer, const QString &member, bool restricted);
    Q_INVOKABLE void search(const QString &query, const QString &peer, bool nextPage = false);
    Q_INVOKABLE void searchPeers(const QString &query);
    Q_INVOKABLE void searchPeople(const QString &query);
    Q_INVOKABLE void copyText(const QString &text);
    Q_INVOKABLE void copyMessageLink(const QString &peer, const QString &cmid);
    Q_INVOKABLE void loadImportant(bool nextPage = false);
    Q_INVOKABLE void loadAttachments(const QString &peer, const QString &type, bool nextPage = false);
    Q_INVOKABLE void loadInvite(const QString &peer, bool reset = false);
    Q_INVOKABLE void joinInvite(const QString &link);
    Q_INVOKABLE void loadReactions();
    Q_INVOKABLE void setReaction(const QString &peer, const QString &cmid, int reaction);
    Q_INVOKABLE void loadReactedPeers(const QString &peer, const QString &cmid, int reaction = 0);
    Q_INVOKABLE void loadStickers(bool favorites = false);
    Q_INVOKABLE void sendSticker(const QString &peer, int stickerId, const QVariantMap &sticker);
    Q_INVOKABLE void favoriteSticker(int stickerId, bool favorite);
    Q_INVOKABLE void sendBotText(const QString &peer, const QString &text, const QString &payload);
    Q_INVOKABLE void clickBotCallback(const QString &peer, const QString &cmid, const QString &author, const QString &payload);
    Q_INVOKABLE void openAttachment(const QVariantMap &attachment);
signals:
    void changed();
    void completed();
    void messageEdited(const QString &peer, const QString &cmid);
    void mediaRequested(const QUrl &source, const QString &type, const QString &title);
    void reactionAssetsChanged();
private:
    using Callback = std::function<void(const QJsonValue &)>;
    void invoke(const QString &method, Form parameters, Callback callback = {}, bool refresh = true);
    void messageResults(const QJsonValue &value, bool append);
    Messenger *m_messenger;
    bool m_busy = false, m_hasMore = false;
    bool m_cachedResults = false;
    QString m_error, m_inviteLink;
    QVariantList m_results;
    QVariantList m_reactionAssets, m_selectableReactions;
    bool m_reactionsLoading = false;
    qint64 m_reactionsCachedAt = 0;
    quint64 m_generation = 0;
    int m_offset = 0;
    QString m_searchQuery, m_searchPeer;
    QString m_attachmentCmid;
    int m_attachmentPosition = -1;
};
