#pragma once
#include <QSortFilterProxyModel>
#include <QVariantMap>

class FolderController;

// Recipient choices deliberately include archived chats and ignore sidebar filters.
class ConversationPickerModel : public QSortFilterProxyModel {
    Q_OBJECT
    Q_PROPERTY(bool peopleOnly READ peopleOnly WRITE setPeopleOnly NOTIFY filterChanged)
public:
    explicit ConversationPickerModel(QAbstractItemModel *source, QObject *parent = nullptr);
    bool peopleOnly() const { return m_peopleOnly; }
    void setPeopleOnly(bool value);
    Q_INVOKABLE QVariantMap get(int row) const;
signals:
    void filterChanged();
protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override;
private:
    bool m_peopleOnly = false;
};

class ConversationFilterModel : public QSortFilterProxyModel {
    Q_OBJECT
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY filterChanged)
    Q_PROPERTY(int category READ category WRITE setCategory NOTIFY filterChanged)
    Q_PROPERTY(bool unreadOnly READ unreadOnly WRITE setUnreadOnly NOTIFY filterChanged)
    Q_PROPERTY(QString pinnedBoundaryPeer READ pinnedBoundaryPeer NOTIFY changed)
public:
    ConversationFilterModel(QAbstractItemModel *source, FolderController *folders, QObject *parent = nullptr);
    QString query() const { return m_query; }
    int category() const { return m_category; }
    bool unreadOnly() const { return m_unreadOnly; }
    void setQuery(const QString &query);
    void setCategory(int category);
    void setUnreadOnly(bool unreadOnly);
    QString pinnedBoundaryPeer() const;
    Q_INVOKABLE QVariantMap get(int row) const;
signals:
    void filterChanged();
    void changed();
protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override;
private:
    void scheduleChanged();
    void beginUpdate();
    void endUpdate();
    FolderController *m_folders;
    QString m_query, m_folder;
    int m_folderRevision = 0;
    int m_category = 0;
    bool m_unreadOnly = false, m_changePending = false;
};
