#pragma once
#include "Form.h"
#include <QObject>
#include <QJsonObject>
#include <QSettings>
#include <QVariantList>
#include <functional>

class Messenger;

class FolderController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList items READ items NOTIFY changed)
    Q_PROPERTY(QString selected READ selected WRITE setSelected NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(int revision READ revision NOTIFY changed)
public:
    explicit FolderController(Messenger *messenger);
    QVariantList items() const { return m_items; }
    QString selected() const { return m_selected; }
    bool busy() const { return m_busy; }
    QString error() const { return m_error; }
    int revision() const { return m_revision; }
    void setSelected(const QString &id);
    void restore(const QJsonObject &catalog);
    void reset(bool clear = true);
    void conversationsChanged() { ++m_revision; emit changed(); }
    Q_INVOKABLE void refresh();
    Q_INVOKABLE bool contains(const QString &folder, const QString &peer) const;
    Q_INVOKABLE QVariantMap get(const QString &id) const;
    Q_INVOKABLE void create(const QString &name, const QStringList &peers);
    Q_INVOKABLE void update(const QString &id, const QString &name, const QStringList &peers);
    Q_INVOKABLE void remove(const QString &id);
    Q_INVOKABLE void move(const QString &id, int direction);
signals:
    void changed();
    void completed();
private:
    void mutate(const QString &method, Form params, std::function<void(const QJsonValue &)> done);
    void publish();
    void save();
    bool validateName(const QString &name);
    Messenger *m_messenger;
    QSettings m_settings;
    QJsonObject m_catalog;
    QVariantList m_items;
    QString m_selected, m_error;
    bool m_busy = false, m_refreshPending = false;
    quint64 m_epoch = 0;
    int m_revision = 0;
};
