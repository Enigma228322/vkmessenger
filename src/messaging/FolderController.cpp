#include "FolderController.h"
#include "Messenger.h"
#include <QJsonArray>
#include <QSet>

namespace {
QString folderId(const QJsonValue &id) { return LongPoll::cursor(id); }
QJsonArray peerArray(const QStringList &peers) {
    QJsonArray result; QSet<qint64> seen;
    for (const auto &peer : peers) { bool valid; const auto id = peer.toLongLong(&valid); if (valid && id != 0 && !seen.contains(id)) { result.append(id); seen.insert(id); } }
    return result;
}
bool editable(const QJsonObject &folder) { return ((folder.value("flags").toInt() & 14) >> 1) == 0 && (folder.value("type").toString().isEmpty() || folder.value("type").toString() == "default"); }
}
FolderController::FolderController(Messenger *messenger) : QObject(messenger), m_messenger(messenger) {}
void FolderController::reset(bool clear)
{
    ++m_epoch; m_busy = m_refreshPending = false; m_error.clear();
    if (clear) { m_catalog = {}; m_items.clear(); m_selected.clear(); }
    ++m_revision; emit changed();
}
void FolderController::setSelected(const QString &id)
{
    if (m_selected == id) return;
    if (!id.isEmpty() && get(id).isEmpty()) return;
    m_selected = id;
    if (!m_messenger->m_account.isEmpty()) m_settings.setValue("folders/selected/" + m_messenger->m_account, id);
    ++m_revision; emit changed();
}
void FolderController::restore(const QJsonObject &catalog)
{
    m_catalog = catalog;
    m_selected = m_settings.value("folders/selected/" + m_messenger->m_account).toString();
    publish();
}
void FolderController::publish()
{
    m_items.clear();
    for (const auto &value : m_catalog.value("items").toArray()) {
        const auto folder = value.toObject(); auto row = safeJson(folder).toMap();
        row.insert("id", folderId(folder.value("id"))); row.insert("editable", editable(folder));
        QStringList peers;
        for (const auto &peer : folder.value("included_peer_ids").toArray()) peers.append(folderId(peer));
        row.insert("peers", peers); m_items.append(row);
    }
    if (!m_selected.isEmpty() && get(m_selected).isEmpty()) m_selected.clear();
    ++m_revision; emit changed();
}
QVariantMap FolderController::get(const QString &id) const
{
    for (const auto &value : m_items) if (value.toMap().value("id").toString() == id) return value.toMap();
    return {};
}
bool FolderController::contains(const QString &id, const QString &peer) const
{
    if (id.isEmpty()) return true;
    for (const auto &value : m_catalog.value("items").toArray()) {
        const auto folder = value.toObject(); if (folderId(folder.value("id")) != id) continue;
        for (const auto &excluded : folder.value("excluded_peer_ids").toArray()) if (folderId(excluded) == peer) return false;
        for (const auto &included : folder.value("included_peer_ids").toArray()) if (folderId(included) == peer) return true;
        const auto includedLists = folder.value("included_lists").toArray();
        const auto flags = m_messenger->m_rawConversations.value(peer).value("conversation").toObject().value("peer_flags").toInteger();
        for (const auto &listValue : m_catalog.value("included_lists_info").toArray()) {
            const auto list = listValue.toObject();
            if (!includedLists.contains(list.value("type"))) continue;
            const auto filter = list.value("filter").toObject();
            if (((flags & filter.value("and").toInteger()) ^ filter.value("xor").toInteger()) == 0) return true;
        }
        return false;
    }
    return false;
}
void FolderController::save()
{
    const auto session = m_messenger->m_generation;
    m_messenger->m_store.saveCatalog(m_messenger->m_account, "folders", m_catalog, [this, session](const QJsonObject &, const QString &error) {
        if (session != m_messenger->m_generation || error.isEmpty()) return;
        m_error = "Не удалось сохранить папки на устройстве."; emit changed();
    });
}
void FolderController::refresh()
{
    if (m_busy) { m_refreshPending = true; return; }
    if (!m_messenger->connected() || m_messenger->demo() || m_messenger->offline() || m_messenger->m_account.isEmpty()) return;
    const auto epoch = m_epoch; const auto session = m_messenger->m_generation;
    m_busy = true; m_error.clear(); emit changed();
    m_messenger->api("messages.getFolders", {{"with_peers", "1"}, {"supported_types", "personal,unread,business"}, {"group_id", m_messenger->groupId()}},
        [this, epoch, session](const QJsonValue &value, const QString &error) {
        if (epoch != m_epoch || session != m_messenger->m_generation) return;
        m_busy = false; m_error = error;
        if (error.isEmpty()) {
            const auto catalog = value.toObject();
            if (!catalog.value("items").isArray()) m_error = "VK вернул неизвестный формат папок.";
            else { m_catalog = catalog; save(); publish(); }
        }
        emit changed();
        if (m_refreshPending) { m_refreshPending = false; refresh(); }
    });
}
bool FolderController::validateName(const QString &name)
{
    if (!name.trimmed().isEmpty() && name.trimmed().size() <= 16) return true;
    m_error = "Название папки должно содержать от 1 до 16 символов."; emit changed(); return false;
}
void FolderController::mutate(const QString &method, Form params, std::function<void(const QJsonValue &)> done)
{
    if (m_busy) return;
    if (!m_messenger->connected() || m_messenger->demo() || m_messenger->offline()) { m_error = "Для изменения папок нужно подключение к VK."; emit changed(); return; }
    m_error.clear(); m_busy = true; emit changed();
    const auto epoch = m_epoch; const auto session = m_messenger->m_generation;
    m_messenger->api(method, params, [this, epoch, session, done](const QJsonValue &value, const QString &error) {
        if (session != m_messenger->m_generation || epoch != m_epoch) return;
        m_busy = false; m_error = error;
        if (m_error.isEmpty() && ((value.isDouble() && value.toInteger() == 0) || (value.isBool() && !value.toBool()))) m_error = "VK не подтвердил изменение папки.";
        if (m_error.isEmpty()) { done(value); if (m_error.isEmpty()) { save(); publish(); emit completed(); } }
        emit changed();
        if (m_refreshPending) { m_refreshPending = false; refresh(); }
    });
}
void FolderController::create(const QString &name, const QStringList &peers)
{
    if (!validateName(name)) return;
    mutate("messages.createFolder", {{"name", name.trimmed()}, {"type", "default"}, {"included_peer_ids", peers.join(',')}}, [this, name, peers](const QJsonValue &value) {
        const auto id = value.toObject().value("folder_id");
        if (folderId(id).isEmpty() || folderId(id) == "0") { m_error = "VK не вернул созданную папку."; return; }
        auto items = m_catalog.value("items").toArray();
        items.append(QJsonObject{{"id", id}, {"name", name.trimmed()}, {"type", "default"}, {"flags", 0}, {"included_peer_ids", peerArray(peers)}});
        m_catalog.insert("items", items);
    });
}
void FolderController::update(const QString &id, const QString &name, const QStringList &peers)
{
    const auto current = get(id); if (!current.value("editable").toBool() || !validateName(name)) return;
    const auto previous = current.value("peers").toStringList(); QStringList add, remove;
    for (const auto &peer : peers) if (!previous.contains(peer)) add.append(peer);
    for (const auto &peer : previous) if (!peers.contains(peer)) remove.append(peer);
    Form params{{"folder_id", id}, {"name", name.trimmed()}};
    if (!add.isEmpty()) params.append({"add_included_peer_ids", add.join(',')});
    if (!remove.isEmpty()) params.append({"remove_included_peer_ids", remove.join(',')});
    mutate("messages.updateFolder", params, [this, id, name, peers](const QJsonValue &) {
        auto items = m_catalog.value("items").toArray();
        for (int i = 0; i < items.size(); ++i) { auto folder = items[i].toObject(); if (folderId(folder.value("id")) != id) continue; folder.insert("name", name.trimmed()); folder.insert("included_peer_ids", peerArray(peers)); items[i] = folder; }
        m_catalog.insert("items", items);
    });
}
void FolderController::remove(const QString &id)
{
    if (!get(id).value("editable").toBool()) return;
    mutate("messages.deleteFolder", {{"folder_id", id}}, [this, id](const QJsonValue &) {
        auto items = m_catalog.value("items").toArray();
        for (int i = items.size() - 1; i >= 0; --i) if (folderId(items[i].toObject().value("id")) == id) items.removeAt(i);
        m_catalog.insert("items", items);
    });
}
void FolderController::move(const QString &id, int direction)
{
    auto items = m_catalog.value("items").toArray(); int index = -1;
    for (int i = 0; i < items.size(); ++i) if (folderId(items[i].toObject().value("id")) == id) index = i;
    if (index < 0 || !editable(items[index].toObject())) return;
    int target = index + (direction < 0 ? -1 : 1);
    while (target >= 0 && target < items.size() && !editable(items[target].toObject())) target += direction < 0 ? -1 : 1;
    if (target < 0 || target >= items.size()) return;
    const QJsonValue value = items[index]; items[index] = items[target]; items[target] = value;
    QStringList ids; for (const auto &item : items) ids.append(folderId(item.toObject().value("id")));
    mutate("messages.reorderFolders", {{"folder_ids", ids.join(',')}}, [this, items](const QJsonValue &) { m_catalog.insert("items", items); });
}
