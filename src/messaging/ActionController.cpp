#include "ActionController.h"
#include "Messenger.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QDateTime>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QClipboard>
#include <QUrlQuery>

namespace {
QString identifier(const QJsonValue &value) { return LongPoll::cursor(value); }
QString displayName(const QJsonObject &person) {
    return person.value("name").toString((person.value("first_name").toString() + ' ' + person.value("last_name").toString()).trimmed());
}
bool isPerson(const QString &value) { bool ok; auto id = value.toLongLong(&ok); return ok && id > 0 && id < 2000000000; }
}

ActionController::ActionController(Messenger *messenger) : QObject(messenger), m_messenger(messenger) {}
void ActionController::cancelPending() { ++m_generation; m_busy = false; emit changed(); }

void ActionController::reset() { ++m_generation; m_results.clear(); m_error.clear(); m_inviteLink.clear(); m_busy = m_hasMore = m_cachedResults = false; m_offset = 0; emit changed(); }

void ActionController::invoke(const QString &method, Form parameters, Callback callback, bool refresh)
{
    if (m_busy) return;
    m_error.clear();
    if (!m_messenger->connected() || m_messenger->demo() || m_messenger->offline()) {
        m_error = m_messenger->demo() ? "Подключитесь к VK, чтобы выполнить это действие." : "Для этого действия нужно подключение к VK.";
        emit changed(); return;
    }
    m_busy = true; emit changed();
    const auto generation = m_generation;
    const auto session = m_messenger->m_generation;
    if (method.startsWith("messages.") || method == "account.setSilenceMode") parameters.append({"group_id", m_messenger->groupId()});
    m_messenger->api(method, parameters, [this, generation, session, callback, refresh](const QJsonValue &value, const QString &error) {
        if (session != m_messenger->m_generation) return;
        if (generation != m_generation) { if (refresh && error.isEmpty()) m_messenger->reload(); return; }
        m_busy = false; m_error = error;
        if (m_error.isEmpty() && ((value.isBool() && !value.toBool()) || (value.isDouble() && value.toInteger() == 0))) m_error = "VK не подтвердил действие.";
        if (m_error.isEmpty()) {
            if (callback) callback(value);
            if (refresh) { m_messenger->reload(); emit completed(); }
        }
        emit changed();
    });
}

void ActionController::editMessage(const QString &peer, const QString &cmid, const QString &text)
{
    if (text.size() > 4096) { m_error = "Сообщение слишком длинное."; emit changed(); return; }
    const auto raw = m_messenger->m_histories.value(peer).value(cmid.toLongLong());
    if (raw.value("out").toInt() != 1) { m_error = "Это сообщение нельзя редактировать."; emit changed(); return; }
    QStringList attachments;
    for (auto value : raw.value("attachments").toArray()) {
        auto a = value.toObject(); auto type = a.value("type").toString(); auto item = a.value(type).toObject();
        if (!item.contains("owner_id") || !item.contains("id")) continue;
        QString id = type + identifier(item.value("owner_id")) + '_' + identifier(item.value("id"));
        if (!item.value("access_key").toString().isEmpty()) id += '_' + item.value("access_key").toString();
        attachments.append(id);
    }
    invoke("messages.edit", {{"peer_id", peer}, {"cmid", cmid}, {"message", text}, {"attachment", attachments.join(',')},
        {"keep_forward_messages", "1"}, {"keep_snippets", "1"}}, [this, peer, cmid](const QJsonValue &) { emit messageEdited(peer, cmid); });
}

void ActionController::deleteMessages(const QString &peer, const QStringList &cmids, bool forEveryone)
{
    if (cmids.isEmpty()) return;
    invoke("messages.delete", {{"peer_id", peer}, {"cmids", cmids.join(',')}, {"delete_for_all", forEveryone ? "1" : "0"}}, [this, peer, cmids, forEveryone](const QJsonValue &value) {
        bool partial = false;
        const auto items = value.toArray();
        for (const auto &v : items) {
            const auto item = v.toObject();
            if (item.value("response").toInt() != 1 || item.contains("error")) { partial = true; continue; }
            if (!forEveryone && item.value("conversation_message_id").toInteger() > 0)
                m_messenger->m_restorable.append(QVariantMap{{"peer_id", peer}, {"cmid", identifier(item.value("conversation_message_id"))}, {"group_id", m_messenger->groupId()}});
        }
        const auto byId = value.toObject();
        for (auto it = byId.begin(); it != byId.end(); ++it) if (it.value().toInt() != 1) partial = true;
        if (items.isEmpty() && byId.isEmpty() && !value.isDouble()) partial = true;
        if (partial) m_error = "Часть сообщений не удалось удалить. Переписка будет обновлена.";
        emit m_messenger->changed();
    });
}

void ActionController::forwardMessages(const QString &sourcePeer, const QStringList &cmids, const QString &destination, const QString &comment)
{
    if (cmids.isEmpty() || destination.isEmpty()) return;
    QJsonArray ids;
    for (const auto &cmid : cmids) { bool ok; auto value = cmid.toLongLong(&ok); if (!ok || value <= 0) return; ids.append(value); }
    m_messenger->queuePayload(destination, comment, {{"forward", QJsonObject{{"peer_id", sourcePeer.toLongLong()}, {"conversation_message_ids", ids}}}, {"preserve_draft", true}});
    emit completed();
}

void ActionController::setImportant(const QString &peer, const QString &cmid, bool important) { invoke("messages.markAsImportant", {{"peer_id", peer}, {"cmids", cmid}, {"important", important ? "1" : "0"}}); }
void ActionController::setPinnedMessage(const QString &peer, const QString &cmid, bool pinned) { invoke(pinned ? "messages.pin" : "messages.unpin", pinned ? Form{{"peer_id", peer}, {"cmid", cmid}} : Form{{"peer_id", peer}}); }

void ActionController::conversationAction(const QString &peer, const QString &action)
{
    const QHash<QString, QString> methods{{"archive", "messages.archiveConversation"}, {"unarchive", "messages.unarchiveConversation"},
        {"pin", "messages.pinConversation"}, {"unpin", "messages.unpinConversation"}, {"unread", "messages.markAsUnreadConversation"},
        {"read", "messages.markAsRead"}, {"important", "messages.markAsImportantConversation"}, {"unimportant", "messages.markAsImportantConversation"},
        {"mute", "account.setSilenceMode"}, {"unmute", "account.setSilenceMode"}, {"delete", "messages.deleteConversation"},
        {"removePhoto", "messages.deleteChatPhoto"}, {"leave", "messages.removeChatUser"}, {"muteMentions", "messages.muteChatMentions"}, {"unmuteMentions", "messages.muteChatMentions"}};
    if (!methods.contains(action)) return;
    Form params{{"peer_id", peer}};
    if (action == "important" || action == "unimportant") params.emplaceBack("important", action == "important" ? "1" : "0");
    if (action == "mute" || action == "unmute") { params.emplaceBack("time", action == "mute" ? "-1" : "0"); params.emplaceBack("sound", action == "mute" ? "0" : "1"); }
    if (action == "muteMentions" || action == "unmuteMentions") params.emplaceBack("mention_status", action == "muteMentions" ? "all" : "none");
    if (action == "read") params.append(qMakePair(QString("mark_conversation_as_read"), QString("1")));
    if (action == "removePhoto" || action == "leave") params = {{"chat_id", QString::number(peer.toLongLong() - 2000000000)}};
    if (action == "leave") params.append({"member_id", identifier(m_messenger->m_session.value("user_id"))});
    invoke(methods.value(action), params, [this, peer, action](auto) {
        if ((action == "unread" || action == "delete" || action == "leave") && peer == m_messenger->selectedPeer()) {
            ++m_messenger->m_historyGeneration; m_messenger->m_peer.clear(); m_messenger->m_messages.replace({}); emit m_messenger->changed();
        }
    });
}

void ActionController::renameChat(const QString &peer, const QString &title) { if (!title.trimmed().isEmpty()) invoke("messages.editChat", {{"chat_id", QString::number(peer.toLongLong() - 2000000000)}, {"title", title.trimmed()}}); }
void ActionController::createChat(const QString &title, const QStringList &people)
{
    QStringList ids; for (const auto &person : people) if (isPerson(person)) ids.append(person);
    if (ids.isEmpty()) { m_error = "Выберите участников беседы."; emit changed(); return; }
    invoke("messages.createChat", {{"user_ids", ids.join(',')}, {"title", title.trimmed()}}, [this, title](const QJsonValue &value) {
        const auto id = value.isObject() ? value.toObject().value("chat_id").toInteger() : value.toInteger();
        if (id > 0) m_messenger->selectConversation(QString::number(id + 2000000000), title);
    });
}
void ActionController::addMembers(const QString &peer, const QStringList &people)
{
    QStringList ids; for (const auto &person : people) if (isPerson(person)) ids.append(person);
    if (!ids.isEmpty()) invoke("messages.addChatUsers", {{"chat_id", QString::number(peer.toLongLong() - 2000000000)}, {"user_ids", ids.join(',')}});
}
void ActionController::removeMember(const QString &peer, const QString &member) { invoke("messages.removeChatUser", {{"chat_id", QString::number(peer.toLongLong() - 2000000000)}, {"member_id", member}}); }
void ActionController::setMemberRole(const QString &peer, const QString &member, bool administrator) { invoke("messages.setMemberRole", {{"peer_id", peer}, {"member_id", member}, {"role", administrator ? "admin" : "member"}}); }
void ActionController::restrictMember(const QString &peer, const QString &member, bool restricted) {
    Form params{{"peer_id", peer}, {"member_ids_long", member}, {"action", restricted ? "ro" : "rw"}};
    if (restricted) params.emplaceBack("for", "3600");
    invoke("messages.changeConversationMemberRestrictions", params, [this](const QJsonValue &value) {
        if (!value.toObject().value("failed_member_ids").toArray().isEmpty()) m_error = "Не удалось изменить ограничение участника.";
    });
}

void ActionController::loadMembers(const QString &peer, bool nextPage)
{
    const int offset = nextPage ? m_offset + 200 : 0;
    invoke("messages.getConversationMembers", {{"peer_id", peer}, {"extended", "1"}, {"fields", "photo_100"}, {"count", "200"}, {"offset", QString::number(offset)}}, [this, nextPage, offset](const QJsonValue &value) {
        m_offset = offset;
        if (!nextPage) m_results.clear();
        auto response = value.toObject(); QHash<QString, QJsonObject> profiles;
        for (auto p : response.value("profiles").toArray()) profiles[identifier(p.toObject().value("id"))] = p.toObject();
        for (auto p : response.value("groups").toArray()) profiles[QString::number(-p.toObject().value("id").toInteger())] = p.toObject();
        for (auto v : response.value("items").toArray()) {
            auto member = v.toObject(); auto id = identifier(member.value("member_id")); auto profile = profiles.value(id);
            auto row = safeJson(member).toMap(); row.insert("peer", id); row.insert("title", displayName(profile)); row.insert("avatar", profile.value("photo_100").toString());
            m_results.append(row);
        }
        m_hasMore = response.value("count").toInt() > m_results.size();
    }, false);
}

void ActionController::messageResults(const QJsonValue &value, bool append)
{
    if (!append) m_results.clear();
    auto response = value.toObject();
    QHash<QString, QJsonObject> peers;
    for (auto p : response.value("profiles").toArray()) peers[identifier(p.toObject().value("id"))] = p.toObject();
    for (auto p : response.value("groups").toArray()) peers[QString::number(-p.toObject().value("id").toInteger())] = p.toObject();
    for (auto v : response.value("items").toArray()) {
        auto msg = v.toObject();
        m_results.append(QVariantMap{{"peer", identifier(msg.value("peer_id"))}, {"cmid", identifier(msg.value("conversation_message_id"))},
            {"title", displayName(peers.value(identifier(msg.value("from_id"))))}, {"text", msg.value("text").toString()}, {"attachments", safeJson(msg.value("attachments"))}});
    }
    m_hasMore = response.value("items").toArray().size() >= 50;
}
void ActionController::search(const QString &query, const QString &peer, bool nextPage)
{
    if (query.trimmed().isEmpty()) return;
    if (nextPage && m_busy) return;
    if (!nextPage) reset();
    const auto generation = m_generation;
    const int offset = nextPage ? m_offset + 50 : 0;
    const bool offline = m_messenger->offline();
    const auto serverFinished = std::make_shared<bool>(false);
    if (offline) { m_busy = true; emit changed(); }
    m_messenger->m_store.searchMessages(m_messenger->m_account, query, peer, offset,
        [this, generation, offset, nextPage, offline, serverFinished](const QJsonObject &value, const QString &error) {
        if (generation != m_generation || *serverFinished) return;
        if (offline) m_busy = false;
        if (!error.isEmpty()) { if (offline) m_error = "Не удалось выполнить поиск в сохранённой истории."; }
        else { m_offset = offset; m_cachedResults = true; messageResults(value, nextPage); }
        emit changed();
    });
    if (offline) return;
    Form params{{"q", query}, {"extended", "1"}, {"count", "50"}, {"offset", QString::number(offset)}};
    if (!peer.isEmpty()) params.append({"peer_id", peer});
    invoke("messages.search", params, [this, nextPage, offset, serverFinished](auto value) {
        *serverFinished = true; m_offset = offset; m_cachedResults = false; messageResults(value, nextPage);
    }, false);
}
void ActionController::searchPeers(const QString &query)
{
    invoke("messages.searchConversations", {{"q", query}, {"count", "100"}, {"extended", "1"}, {"fields", "photo_100"}}, [this](const QJsonValue &value) {
        m_results.clear(); const auto response = value.toObject(); QHash<QString, QJsonObject> peers;
        for (auto p : response.value("profiles").toArray()) peers[identifier(p.toObject().value("id"))] = p.toObject();
        for (auto p : response.value("groups").toArray()) peers[QString::number(-p.toObject().value("id").toInteger())] = p.toObject();
        for (auto v : response.value("items").toArray()) {
            auto convo = v.toObject(); if (convo.contains("conversation")) convo = convo.value("conversation").toObject();
            const auto id = identifier(convo.value("peer").toObject().value("id"));
            const auto title = convo.value("chat_settings").toObject().value("title").toString(displayName(peers.value(id)));
            m_results.append(QVariantMap{{"peer", id}, {"title", title}, {"avatar", peers.value(id).value("photo_100").toString()}});
        }
    }, false);
}
void ActionController::searchPeople(const QString &query)
{
    invoke("users.search", {{"q", query}, {"count", "100"}, {"fields", "photo_100"}}, [this](const QJsonValue &value) {
        m_results.clear();
        for (const auto &item : value.toObject().value("items").toArray()) {
            const auto person = item.toObject();
            m_results.append(QVariantMap{{"peer", identifier(person.value("id"))}, {"title", displayName(person)}, {"avatar", person.value("photo_100").toString()}});
        }
    }, false);
}
void ActionController::copyText(const QString &text)
{
    QGuiApplication::clipboard()->setText(text);
}

void ActionController::copyMessageLink(const QString &peer, const QString &cmid)
{
    QUrl url("vkm://chat/" + peer); QUrlQuery query; query.addQueryItem("message", cmid); url.setQuery(query);
    QGuiApplication::clipboard()->setText(url.toString(QUrl::FullyEncoded));
}
void ActionController::loadImportant(bool nextPage)
{
    if (!nextPage) m_offset = 0; else m_offset += 50;
    invoke("messages.getImportantMessages", {{"count", "50"}, {"offset", QString::number(m_offset)}, {"extended", "1"}}, [this, nextPage](const QJsonValue &value) {
        auto response = value.toObject(); auto messages = response.value("messages").toObject();
        if (!messages.isEmpty()) { messages.insert("profiles", response.value("profiles")); messageResults(messages, nextPage); }
        else messageResults(value, nextPage);
    }, false);
}
void ActionController::loadAttachments(const QString &peer, const QString &type, bool nextPage)
{
    Form params{{"peer_id", peer}, {"media_type", type}, {"count", "50"}, {"offset", nextPage ? "1" : "0"}, {"photo_sizes", "1"}};
    if (nextPage && !m_attachmentCmid.isEmpty()) {
        params.append({"cmid", m_attachmentCmid});
        if (m_attachmentPosition >= 0) params.append({"attachment_position", QString::number(m_attachmentPosition)});
    }
    invoke("messages.getHistoryAttachments", params, [this, nextPage, peer](const QJsonValue &value) {
        if (!nextPage) m_results.clear();
        const auto items = value.toObject().value("items").toArray();
        for (auto item : items) {
            auto row = item.toObject(); m_results.append(QVariantMap{{"peer", peer}, {"cmid", identifier(row.value("cmid"))}, {"title", "Вложение"}, {"attachments", QVariantList{safeJson(row.value("attachment"))}}});
        }
        const auto edge = items.isEmpty() ? QJsonObject() : items.last().toObject();
        const auto nextCmid = identifier(edge.value("cmid"));
        const auto nextPosition = edge.value("position").toInt(-1);
        m_hasMore = items.size() >= 50 && (!nextPage || nextCmid != m_attachmentCmid || nextPosition != m_attachmentPosition);
        m_attachmentCmid = nextCmid; m_attachmentPosition = nextPosition;
    }, false);
}
void ActionController::loadInvite(const QString &peer, bool resetLink) { invoke("messages.getInviteLink", {{"peer_id", peer}, {"reset", resetLink ? "1" : "0"}}, [this](const QJsonValue &value) { m_inviteLink = value.toObject().value("link").toString(); }, false); }
void ActionController::joinInvite(const QString &link) { invoke("messages.joinChatByInviteLink", {{"link", link}}, [this](const QJsonValue &value) { const auto chat = value.isObject() ? value.toObject().value("chat_id").toInteger() : value.toInteger(); if (chat > 0) m_messenger->selectConversation(QString::number(chat + 2000000000), "Беседа"); }); }
void ActionController::loadReactions()
{
    invoke("messages.getReactionsAssets", {}, [this](const QJsonValue &value) {
        const auto catalog = value.toObject(); cacheReactionAssets(catalog);
        m_results.clear();
        const auto allowed = catalog.value("reaction_ids").toArray();
        for (const auto &asset : m_reactionAssets) {
            const auto row = asset.toMap();
            if (!catalog.contains("reaction_ids") || allowed.contains(row.value("reaction_id").toInt())) m_results.append(row);
        }
    }, false);
}
void ActionController::setReaction(const QString &peer, const QString &cmid, int reaction) { invoke(reaction > 0 ? "messages.sendReaction" : "messages.deleteReaction", reaction > 0 ? Form{{"peer_id", peer}, {"cmid", cmid}, {"reaction_id", QString::number(reaction)}} : Form{{"peer_id", peer}, {"cmid", cmid}}); }
void ActionController::loadReactedPeers(const QString &peer, const QString &cmid, int reaction)
{
    Form params{{"peer_id", peer}, {"cmid", cmid}}; if (reaction > 0) params.append({"reaction_id", QString::number(reaction)});
    invoke("messages.getReactedPeers", params, [this](const QJsonValue &value) {
        m_results.clear(); auto response = value.toObject();
        for (auto p : response.value("profiles").toArray()) { auto person = p.toObject(); m_results.append(QVariantMap{{"peer", identifier(person.value("id"))}, {"title", displayName(person)}, {"avatar", person.value("photo_100").toString()}}); }
    }, false);
}
void ActionController::loadStickers(bool favorites)
{
    invoke(favorites ? "store.getFavoriteStickers" : "store.getProducts", favorites ? Form{} : Form{{"type", "stickers"}, {"filters", "active"}, {"extended", "1"}}, [this, favorites](const QJsonValue &value) {
        m_results.clear();
        const auto response = value.toObject();
        if (favorites) { m_results = safeJson(response.value("items")).toList(); return; }
        for (auto product : response.value("items").toArray()) {
            auto pack = product.toObject();
            for (auto v : pack.value("stickers").toArray()) { auto sticker = v.toObject(); sticker.insert("packTitle", pack.value("title")); m_results.append(safeJson(sticker)); }
        }
    }, false);
}
void ActionController::sendSticker(const QString &peer, int stickerId, const QVariantMap &sticker)
{
    if (stickerId <= 0) return;
    m_messenger->queuePayload(peer, {}, {{"sticker_id", stickerId}, {"preserve_draft", true},
        {"preview", QJsonObject{{"attachments", QJsonArray{QJsonObject{{"type", "sticker"}, {"sticker", QJsonObject::fromVariantMap(sticker)}}}}}}});
    emit completed();
}
void ActionController::favoriteSticker(int stickerId, bool favorite) { invoke(favorite ? "store.addStickersToFavorite" : "store.removeStickersFromFavorite", {{"sticker_ids", QString::number(stickerId)}}, {}, false); }
void ActionController::sendBotText(const QString &peer, const QString &text, const QString &payload) { m_messenger->queuePayload(peer, text, {{"payload", payload}, {"preserve_draft", true}}); }
void ActionController::clickBotCallback(const QString &peer, const QString &cmid, const QString &author, const QString &payload)
{
    invoke("messages.sendMessageEvent", {{"peer_id", peer}, {"cmid", cmid}, {"author_id", author}, {"payload", payload}, {"from", "vkm"}}, [this](const QJsonValue &value) {
        const auto event = LongPoll::cursor(value);
        if (!event.isEmpty()) m_messenger->m_callbacks[event] = QDateTime::currentMSecsSinceEpoch() + 60000;
    }, false);
}
void ActionController::openAttachment(const QVariantMap &attachment)
{
    const auto type = attachment.value("type").toString();
    const auto item = QJsonObject::fromVariantMap(attachment.value(type).toMap());
    const auto title = item.value("title").toString(item.value("text").toString("Вложение"));
    if (type == "photo") {
        const auto sizes = item.value("sizes").toArray();
        if (!sizes.isEmpty()) emit mediaRequested(QUrl(sizes.last().toObject().value("url").toString()), type, title);
    } else if (type == "audio_message" || type == "audio") {
        const auto url = type == "audio_message" ? item.value("link_mp3").toString(item.value("link_ogg").toString()) : item.value("url").toString();
        if (!url.isEmpty()) emit mediaRequested(QUrl(url), type, title);
    } else if (type == "video") {
        const auto selectUrl = [](const QJsonObject &video) {
            const auto files = video.value("files").toObject();
            for (auto key : {"mp4_720", "mp4_480", "mp4_360", "mp4_240", "hls", "dash_uni"})
                if (QUrl(files.value(key).toString()).scheme() == "https") return QUrl(files.value(key).toString());
            return QUrl();
        };
        const auto stream = selectUrl(item);
        if (!stream.isEmpty()) { emit mediaRequested(stream, type, title); return; }
        QString key = identifier(item.value("owner_id")) + '_' + identifier(item.value("id"));
        if (!item.value("access_key").toString().isEmpty()) key += '_' + item.value("access_key").toString();
        invoke("video.get", {{"videos", key}}, [this, selectUrl, title](const QJsonValue &value) {
            const auto videos = value.toObject().value("items").toArray();
            const auto stream = videos.isEmpty() ? QUrl() : selectUrl(videos.first().toObject());
            if (stream.isEmpty()) m_error = "VK не предоставил видеопоток для этого ролика. Его можно открыть в VK.";
            else emit mediaRequested(stream, "video", title);
        }, false);
    } else {
        const auto url = QUrl(item.value("url").toString());
        if (url.scheme() == "https" || url.scheme() == "http") QDesktopServices::openUrl(url);
    }
}

void ActionController::resetAccount()
{
    reset(); m_reactionAssets.clear(); m_selectableReactions.clear(); m_reactionsLoading = false; m_reactionsCachedAt = 0; emit reactionAssetsChanged();
}
QString ActionController::reactionImage(int reaction) const
{
    for (const auto &value : m_reactionAssets) {
        const auto item = value.toMap();
        if (item.value("reaction_id").toInt() == reaction) return item.value("links").toMap().value("static").toString();
    }
    return {};
}
void ActionController::ensureReactionAssets()
{
    if (m_reactionsLoading || m_messenger->demo() || m_messenger->offline()) return;
    if (!m_reactionAssets.isEmpty() && QDateTime::currentSecsSinceEpoch() - m_reactionsCachedAt < 86400) return;
    m_reactionsLoading = true;
    const auto session = m_messenger->m_generation;
    m_messenger->api("messages.getReactionsAssets", {}, [this, session](const QJsonValue &value, const QString &) {
        if (session != m_messenger->m_generation) return;
        m_reactionsLoading = false;
        auto response = value.toObject(); auto assets = response.value("override_assets").toArray();
        if (assets.isEmpty()) assets = response.value("assets").toArray();
        if (!assets.isEmpty()) cacheReactionAssets(response);
    });
}

void ActionController::restoreReactionAssets(const QJsonObject &catalog)
{
    m_reactionsCachedAt = catalog.value("_cached_at").toInteger();
    auto assets = catalog.value("assets").toArray();
    const auto overrides = catalog.value("override_assets").toArray();
    // Overrides replace only matching IDs, never the rest of the catalog.
    for (int i = 0; i < assets.size(); ++i) for (const auto &overrideValue : overrides)
        if (assets[i].toObject().value("reaction_id") == overrideValue.toObject().value("reaction_id")) { assets[i] = overrideValue; break; }
    m_reactionAssets = safeJson(assets).toList();
    m_selectableReactions.clear();
    const auto allowed = catalog.value("reaction_ids").toArray();
    for (const auto &asset : m_reactionAssets)
        if (!catalog.contains("reaction_ids") || allowed.contains(asset.toMap().value("reaction_id").toInt())) m_selectableReactions.append(asset);
    emit reactionAssetsChanged();
}
void ActionController::cacheReactionAssets(const QJsonObject &catalog)
{
    auto saved = catalog; saved.insert("_cached_at", QDateTime::currentSecsSinceEpoch());
    restoreReactionAssets(saved);
    if (!m_reactionAssets.isEmpty() && !m_messenger->m_account.isEmpty())
        m_messenger->m_store.saveCatalog(m_messenger->m_account, "reactions", saved);
}
