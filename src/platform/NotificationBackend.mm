#include "NotificationBackend.h"
#include <QCryptographicHash>
#import <AppKit/AppKit.h>
#import <UserNotifications/UserNotifications.h>

@interface VKNoticeDelegate : NSObject <UNUserNotificationCenterDelegate>
@property(nonatomic) NotificationBackend::Activation activation;
@end
@implementation VKNoticeDelegate
- (void)userNotificationCenter:(UNUserNotificationCenter *)center willPresentNotification:(UNNotification *)notification withCompletionHandler:(void (^)(UNNotificationPresentationOptions))completionHandler {
    completionHandler(UNNotificationPresentationOptionBanner | UNNotificationPresentationOptionList | UNNotificationPresentationOptionSound);
}
- (void)userNotificationCenter:(UNUserNotificationCenter *)center didReceiveNotificationResponse:(UNNotificationResponse *)response withCompletionHandler:(void (^)(void))completionHandler {
    if ([response.actionIdentifier isEqualToString:UNNotificationDefaultActionIdentifier]) {
        NSDictionary *data = response.notification.request.content.userInfo;
        auto callback = self.activation;
        const auto account = QString::fromNSString(data[@"account"]);
        const auto peer = QString::fromNSString(data[@"peer"]);
        const auto cmid = QString::fromNSString(data[@"cmid"]);
        dispatch_async(dispatch_get_main_queue(), ^{ if (callback) callback(account, peer, cmid); });
    }
    completionHandler();
}
@end

namespace {
QString permissionName(UNAuthorizationStatus status) {
    if (status == UNAuthorizationStatusAuthorized || status == UNAuthorizationStatusProvisional) return "granted";
    if (status == UNAuthorizationStatusNotDetermined) return "notDetermined";
    return "denied";
}
class MacNotificationBackend final : public NotificationBackend {
    UNUserNotificationCenter *m_center;
    VKNoticeDelegate *m_delegate;
public:
    MacNotificationBackend() : m_center([UNUserNotificationCenter currentNotificationCenter]), m_delegate([VKNoticeDelegate new]) { m_center.delegate = m_delegate; }
    ~MacNotificationBackend() override { m_delegate.activation = {}; if (m_center.delegate == m_delegate) m_center.delegate = nil; }
    void setActivation(Activation callback) override { m_delegate.activation = std::move(callback); }
    void permission(bool request, Completion complete) override {
        auto center = m_center;
        auto read = ^{
            [center getNotificationSettingsWithCompletionHandler:^(UNNotificationSettings *settings) {
                const auto value = permissionName(settings.authorizationStatus);
                dispatch_async(dispatch_get_main_queue(), ^{ complete(value); });
            }];
        };
        if (request) [center requestAuthorizationWithOptions:UNAuthorizationOptionAlert | UNAuthorizationOptionSound completionHandler:^(BOOL granted, NSError *error) {
            if (error) dispatch_async(dispatch_get_main_queue(), ^{ complete("error"); });
            else read();
        }];
        else read();
    }
    void deliver(const DesktopNotice &notice, Completion complete) override {
        auto content = [UNMutableNotificationContent new];
        content.title = notice.title.toNSString(); content.body = notice.body.toNSString();
        if (notice.sound) content.sound = [UNNotificationSound defaultSound];
        content.userInfo = @{@"account": notice.account.toNSString(), @"peer": notice.peer.toNSString(), @"cmid": notice.cmid.toNSString()};
        // Replace the previous banner for the same conversation during a burst.
        const auto identifier = "vkm." + QString::fromLatin1(QCryptographicHash::hash(notice.account.toUtf8(), QCryptographicHash::Sha256).toHex()) + '.' + notice.peer;
        content.threadIdentifier = identifier.toNSString();
        [m_center removeDeliveredNotificationsWithIdentifiers:@[identifier.toNSString()]];
        auto request = [UNNotificationRequest requestWithIdentifier:identifier.toNSString() content:content trigger:nil];
        [m_center addNotificationRequest:request withCompletionHandler:^(NSError *error) {
            const auto message = error ? QString("Не удалось показать уведомление. Проверьте разрешение macOS.") : QString();
            dispatch_async(dispatch_get_main_queue(), ^{ complete(message); });
        }];
    }
    void clear() override { [m_center removeAllPendingNotificationRequests]; [m_center removeAllDeliveredNotifications]; }
    void openSettings() override { [[NSWorkspace sharedWorkspace] openURL:[NSURL URLWithString:@"x-apple.systempreferences:com.apple.preference.notifications"]]; }
};
}
std::unique_ptr<NotificationBackend> createNotificationBackend() {
    if (![[NSBundle mainBundle] bundleIdentifier].length) return {};
    return std::make_unique<MacNotificationBackend>();
}
