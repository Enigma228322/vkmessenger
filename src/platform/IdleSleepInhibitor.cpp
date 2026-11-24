#include "IdleSleepInhibitor.h"
#include <QtGlobal>
#ifdef Q_OS_LINUX
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDBusUnixFileDescriptor>

namespace {
struct InhibitLock final : IdleSleepInhibitor {
    explicit InhibitLock(QDBusUnixFileDescriptor fd) : descriptor(std::move(fd)) {}
    QDBusUnixFileDescriptor descriptor;
};
}
#endif

std::unique_ptr<IdleSleepInhibitor> IdleSleepInhibitor::acquire()
{
#ifdef Q_OS_LINUX
    auto request = QDBusMessage::createMethodCall("org.freedesktop.login1", "/org/freedesktop/login1",
        "org.freedesktop.login1.Manager", "Inhibit");
    // Keep Long Poll running while the desktop is idle or locked. Leave idle locking to the desktop.
    request << QStringLiteral("sleep") << QStringLiteral("VK Messenger")
            << QStringLiteral("Automatic chat replies") << QStringLiteral("block");
    QDBusReply<QDBusUnixFileDescriptor> lock = QDBusConnection::systemBus().call(request, QDBus::Block, 2000);
    if (lock.isValid() && lock.value().isValid()) return std::make_unique<InhibitLock>(lock.value());
#endif
    return {};
}
