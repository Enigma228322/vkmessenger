#pragma once
#include <memory>

// Owning the returned handle keeps the system awake during user inactivity.
struct IdleSleepInhibitor {
    virtual ~IdleSleepInhibitor() = default;
    static std::unique_ptr<IdleSleepInhibitor> acquire();
};
