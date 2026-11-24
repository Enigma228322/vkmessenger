#include "IdleSleepInhibitor.h"
#import <Foundation/Foundation.h>

namespace {
struct Activity final : IdleSleepInhibitor {
    id token = [[[NSProcessInfo processInfo] beginActivityWithOptions:NSActivityUserInitiated
        reason:@"VK Messenger is answering messages in enabled chats"] retain];
    ~Activity() override {
        if (token) [[NSProcessInfo processInfo] endActivity:token];
        [token release];
    }
};
}

std::unique_ptr<IdleSleepInhibitor> IdleSleepInhibitor::acquire()
{
    auto activity = std::make_unique<Activity>();
    if (!activity->token) return {};
    return activity;
}
