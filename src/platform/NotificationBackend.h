#pragma once
#include <QString>
#include <functional>
#include <memory>

struct DesktopNotice {
    QString account, peer, cmid, title, body;
    bool sound = true;
};
class NotificationBackend {
public:
    using Activation = std::function<void(QString, QString, QString)>;
    using Completion = std::function<void(QString)>;
    virtual ~NotificationBackend() = default;
    virtual void setActivation(Activation callback) = 0;
    virtual void permission(bool request, Completion complete) = 0;
    virtual void deliver(const DesktopNotice &notice, Completion complete) = 0;
    virtual void clear() = 0;
    virtual void openSettings() = 0;
};
std::unique_ptr<NotificationBackend> createNotificationBackend();
