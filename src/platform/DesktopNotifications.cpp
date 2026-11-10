#include "DesktopNotifications.h"
#include "Messenger.h"

DesktopNotifications::DesktopNotifications(Messenger &messenger, std::unique_ptr<NotificationBackend> backend, QObject *parent)
    : QObject(parent), m_messenger(messenger), m_backend(std::move(backend))
{
    QPointer<DesktopNotifications> guard(this);
    if (m_backend) {
        m_backend->setActivation([guard](QString account, QString peer, QString cmid) { if (guard) guard->activate(account, peer, cmid); });
        refreshPermission();
    }
    connect(&messenger, &Messenger::notificationRequested, this, [this](const QString &account, const QString &peer, const QString &cmid, const QString &title, const QString &body) {
        receive({account, peer, cmid, title, body});
    });
    connect(&messenger, &Messenger::changed, this, [this] {
        if (m_account != m_messenger.notificationAccount()) {
            m_account = m_messenger.notificationAccount();
            if (m_backend) m_backend->clear();
        }
        if (!m_pendingClick.isEmpty() && m_messenger.connected() && !m_messenger.notificationAccount().isEmpty()) {
            const auto click = m_pendingClick; m_pendingClick.clear(); activate(click[0], click[1], click[2]);
        }
    });
}
void DesktopNotifications::setWindow(QWindow *window) {
    m_window = window;
    if (window) connect(window, &QWindow::activeChanged, this, [this] { if (m_window && m_window->isActive()) refreshPermission(); });
    if (m_activationPending) showWindow();
}
void DesktopNotifications::setEnabled(bool value) {
    const auto revision = ++m_permissionRevision;
    m_settings.setValue("notifications/enabled", value); m_error.clear(); emit changed();
    if (!m_backend) return;
    if (!value) { m_backend->clear(); return; }
    QPointer<DesktopNotifications> guard(this);
    m_backend->permission(true, [guard, revision](QString status) { if (guard && guard->m_permissionRevision == revision) { guard->m_permission = status; emit guard->changed(); } });
}
void DesktopNotifications::refreshPermission() {
    if (!m_backend) return;
    const auto revision = ++m_permissionRevision;
    QPointer<DesktopNotifications> guard(this);
    m_backend->permission(false, [guard, revision](QString status) { if (guard && guard->m_permissionRevision == revision && guard->m_permission != status) { guard->m_permission = status; emit guard->changed(); } });
}
void DesktopNotifications::receive(const DesktopNotice &notice) {
    if (!m_backend || !enabled() || m_permission != "granted" || notice.account.isEmpty() || notice.account != m_messenger.notificationAccount() || !m_messenger.connected() || m_messenger.demo()) return;
    if (m_window && m_window->isVisible() && m_window->visibility() != QWindow::Minimized && m_window->isActive() && m_messenger.selectedPeer() == notice.peer) return;
    auto shown = notice; shown.sound = sound();
    if (!preview()) { shown.title = "VK"; shown.body = "Новое сообщение"; }
    shown.title = shown.title.left(160); shown.body = shown.body.left(600);
    QPointer<DesktopNotifications> guard(this);
    m_backend->deliver(shown, [guard, account = shown.account](QString error) { if (guard && guard->enabled() && guard->m_messenger.notificationAccount() == account) { guard->m_error = error; emit guard->changed(); } });
}
void DesktopNotifications::activate(QString account, QString peer, QString cmid) {
    if (!m_messenger.connected() || m_messenger.notificationAccount().isEmpty()) { m_pendingClick = {account, peer, cmid}; return; }
    if (!m_messenger.openNotification(account, peer, cmid)) return;
    m_activationPending = true; showWindow();
}
void DesktopNotifications::showWindow() {
    if (!m_window) return;
    m_activationPending = false;
    if (m_window->visibility() == QWindow::Minimized) m_window->showNormal(); else m_window->show();
    m_window->raise(); m_window->requestActivate();
}
