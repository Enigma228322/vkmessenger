#pragma once
#include "NotificationBackend.h"
#include <QObject>
#include <QPointer>
#include <QSettings>
#include <QWindow>

class Messenger;
class DesktopNotifications : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
    Q_PROPERTY(bool preview READ preview WRITE setPreview NOTIFY changed)
    Q_PROPERTY(bool sound READ sound WRITE setSound NOTIFY changed)
    Q_PROPERTY(bool supported READ supported CONSTANT)
    Q_PROPERTY(QString permission READ permission NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
public:
    DesktopNotifications(Messenger &messenger, std::unique_ptr<NotificationBackend> backend, QObject *parent = nullptr);
    bool enabled() const { return m_settings.value("notifications/enabled", false).toBool(); }
    bool preview() const { return m_settings.value("notifications/preview", true).toBool(); }
    bool sound() const { return m_settings.value("notifications/sound", true).toBool(); }
    bool supported() const { return bool(m_backend); }
    QString permission() const { return m_permission; }
    QString error() const { return m_error; }
    void setEnabled(bool value);
    void setPreview(bool value) { m_settings.setValue("notifications/preview", value); emit changed(); }
    void setSound(bool value) { m_settings.setValue("notifications/sound", value); emit changed(); }
    void setWindow(QWindow *window);
    Q_INVOKABLE void refreshPermission();
    Q_INVOKABLE void openSystemSettings() { if (m_backend) m_backend->openSettings(); }
signals:
    void changed();
private:
    void receive(const DesktopNotice &notice);
    void activate(QString account, QString peer, QString cmid);
    void showWindow();
    Messenger &m_messenger;
    std::unique_ptr<NotificationBackend> m_backend;
    QSettings m_settings;
    QPointer<QWindow> m_window;
    QString m_permission = "unavailable", m_error, m_account;
    bool m_activationPending = false;
    QStringList m_pendingClick;
    quint64 m_permissionRevision = 0;
};
