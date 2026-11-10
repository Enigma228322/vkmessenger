#include "ConversationFilterModel.h"
#include "messaging/FolderController.h"
#include <QTimer>

FolderPeerModel::FolderPeerModel(QAbstractItemModel *source, QObject *parent)
    : QSortFilterProxyModel(parent), m_source(source)
{
    m_missing.setItemRoleNames({{Qt::UserRole, "item"}});
    m_missing.setColumnCount(1);
    m_combined.addSourceModel(source); m_combined.addSourceModel(&m_missing);
    setSourceModel(&m_combined);
    const auto schedule = [this] {
        if (m_savedPeers.isEmpty()) return;
        if (m_rebuildPending) return;
        m_rebuildPending = true;
        QTimer::singleShot(0, this, [this] { m_rebuildPending = false; rebuildKnownPeers(); });
    };
    connect(source, &QAbstractItemModel::modelReset, this, schedule);
    connect(source, &QAbstractItemModel::rowsInserted, this, schedule);
    connect(source, &QAbstractItemModel::rowsRemoved, this, schedule);
    connect(source, &QAbstractItemModel::dataChanged, this, schedule);
    rebuildKnownPeers();
}

void FolderPeerModel::rebuildKnownPeers()
{
    if (m_savedPeers.isEmpty()) { m_known.clear(); rebuildMissingPeers(); return; }
    m_known.clear(); m_known.reserve(m_source->rowCount());
    for (int row = 0; row < m_source->rowCount(); ++row)
        m_known.insert(m_source->data(m_source->index(row, 0), Qt::UserRole).toMap().value("peer").toString());
    rebuildMissingPeers();
}

void FolderPeerModel::rebuildMissingPeers()
{
    QStringList missing;
    for (const auto &peer : m_savedPeers) if (!m_known.contains(peer)) missing.append(peer);
    if (missing == m_missingPeers) return;
    m_missingPeers = missing;
    m_missing.removeRows(0, m_missing.rowCount());
    for (const auto &peer : missing) {
        auto item = new QStandardItem;
        item->setData(QVariantMap{{"peer", peer}, {"title", "Сохранённая переписка"}, {"missing", true}}, Qt::UserRole);
        m_missing.appendRow(item);
    }
}

void FolderPeerModel::setSavedPeers(const QStringList &value)
{
    QStringList peers; QSet<QString> unique;
    for (const auto &peer : value) {
        bool valid = false; const auto id = peer.toLongLong(&valid);
        if (valid && id != 0 && !unique.contains(peer)) { peers.append(peer); unique.insert(peer); }
    }
    if (m_savedPeers == peers) return;
    m_savedPeers = peers;
    // A source update may have been queued immediately before opening the editor.
    rebuildKnownPeers(); emit savedPeersChanged();
}

void FolderPeerModel::setQuery(const QString &value)
{
    const auto query = value.trimmed(); if (query == m_query) return;
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    beginFilterChange();
#endif
    m_query = query;
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    endFilterChange(Direction::Rows);
#else
    invalidateFilter();
#endif
    emit queryChanged();
}

bool FolderPeerModel::filterAcceptsRow(int row, const QModelIndex &parent) const
{
    return m_query.isEmpty() || sourceModel()->data(sourceModel()->index(row, 0, parent), Qt::UserRole).toMap().value("title").toString().contains(m_query, Qt::CaseInsensitive);
}

QVariantMap FolderPeerModel::get(int row) const
{
    return row >= 0 && row < rowCount() ? data(index(row, 0), Qt::UserRole).toMap() : QVariantMap();
}

ConversationPickerModel::ConversationPickerModel(QAbstractItemModel *source, QObject *parent)
    : QSortFilterProxyModel(parent) { setSourceModel(source); }

void ConversationPickerModel::setPeopleOnly(bool value)
{
    if (m_peopleOnly == value) return;
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    beginFilterChange();
#endif
    m_peopleOnly = value;
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    endFilterChange(Direction::Rows);
#else
    invalidateFilter();
#endif
    emit filterChanged();
}

bool ConversationPickerModel::filterAcceptsRow(int row, const QModelIndex &parent) const
{
    if (!m_peopleOnly) return true;
    const auto peer = sourceModel()->data(sourceModel()->index(row, 0, parent), Qt::UserRole).toMap().value("peer").toLongLong();
    return peer > 0 && peer < 2000000000;
}

QVariantMap ConversationPickerModel::get(int row) const
{
    return row >= 0 && row < rowCount() ? data(index(row, 0), Qt::UserRole).toMap() : QVariantMap();
}

ConversationFilterModel::ConversationFilterModel(QAbstractItemModel *source, FolderController *folders, QObject *parent)
    : QSortFilterProxyModel(parent), m_folders(folders)
{
    m_folder = folders->selected();
    m_folderRevision = folders->revision();
    setSourceModel(source);
    connect(folders, &FolderController::changed, this, [this] {
        const auto revision = m_folders->revision();
        if (revision == m_folderRevision) return;
        m_folderRevision = revision;
        const auto selected = m_folders->selected();
        if (selected.isEmpty() && m_folder.isEmpty()) return;
        beginUpdate(); m_folder = selected; endUpdate();
    });
    connect(this, &QAbstractItemModel::modelReset, this, &ConversationFilterModel::scheduleChanged);
    connect(this, &QAbstractItemModel::rowsInserted, this, &ConversationFilterModel::scheduleChanged);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &ConversationFilterModel::scheduleChanged);
    connect(this, &QAbstractItemModel::rowsMoved, this, &ConversationFilterModel::scheduleChanged);
    connect(this, &QAbstractItemModel::layoutChanged, this, &ConversationFilterModel::scheduleChanged);
    connect(this, &QAbstractItemModel::dataChanged, this, &ConversationFilterModel::scheduleChanged);
}
void ConversationFilterModel::beginUpdate()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    beginFilterChange();
#endif
}
void ConversationFilterModel::endUpdate()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    endFilterChange(Direction::Rows);
#else
    invalidateFilter();
#endif
    scheduleChanged();
}
void ConversationFilterModel::scheduleChanged()
{
    if (m_changePending) return;
    m_changePending = true;
    QTimer::singleShot(0, this, [this] { m_changePending = false; emit changed(); });
}
void ConversationFilterModel::setQuery(const QString &query)
{
    const auto normalized = query.trimmed();
    if (m_query == normalized) return;
    beginUpdate(); m_query = normalized; endUpdate(); emit filterChanged();
}
void ConversationFilterModel::setCategory(int category)
{
    if (m_category == category) return;
    beginUpdate(); m_category = category; endUpdate(); emit filterChanged();
}
void ConversationFilterModel::setUnreadOnly(bool unreadOnly)
{
    if (m_unreadOnly == unreadOnly) return;
    beginUpdate(); m_unreadOnly = unreadOnly; endUpdate(); emit filterChanged();
}
bool ConversationFilterModel::filterAcceptsRow(int row, const QModelIndex &parent) const
{
    const auto item = sourceModel()->data(sourceModel()->index(row, 0, parent), Qt::UserRole).toMap();
    if (item.value("archived").toBool() != (m_category == 3)) return false;
    if (m_category == 2 && !item.value("important").toBool()) return false;
    if (m_unreadOnly && item.value("unread").toInt() <= 0 && !item.value("markedUnread").toBool()) return false;
    if (!m_query.isEmpty() && !item.value("title").toString().contains(m_query, Qt::CaseInsensitive)) return false;
    return m_folder.isEmpty() || m_folders->contains(m_folder, item.value("peer").toString());
}
QVariantMap ConversationFilterModel::get(int row) const
{
    return row >= 0 && row < rowCount() ? data(index(row, 0), Qt::UserRole).toMap() : QVariantMap();
}
QString ConversationFilterModel::pinnedBoundaryPeer() const
{
    bool pinned = false;
    for (int i = 0; i < rowCount(); ++i) {
        const auto item = get(i);
        if (item.value("pinned").toBool()) pinned = true;
        else if (pinned) return item.value("peer").toString();
    }
    return {};
}
