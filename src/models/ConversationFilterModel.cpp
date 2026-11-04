#include "ConversationFilterModel.h"
#include "messaging/FolderController.h"
#include <QTimer>

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
