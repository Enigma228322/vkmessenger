#include "Keychain.h"
#ifdef Q_OS_MACOS
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>

static CFMutableDictionaryRef query()
{
    auto result = CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks,
                                            &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(result, kSecClass, kSecClassGenericPassword);
    CFDictionarySetValue(result, kSecAttrService, CFSTR("dev.reforged.messenger"));
    CFDictionarySetValue(result, kSecAttrAccount, CFSTR("vk-session"));
    return result;
}

QByteArray Keychain::read()
{
    auto q = query();
    CFDictionarySetValue(q, kSecReturnData, kCFBooleanTrue);
    CFDictionarySetValue(q, kSecMatchLimit, kSecMatchLimitOne);
    CFTypeRef data = nullptr;
    const auto status = SecItemCopyMatching(q, &data);
    CFRelease(q);
    if (status != errSecSuccess || !data)
        return {};
    const auto bytes = static_cast<CFDataRef>(data);
    QByteArray result(reinterpret_cast<const char *>(CFDataGetBytePtr(bytes)),
                      CFDataGetLength(bytes));
    CFRelease(data);
    return result;
}

bool Keychain::write(const QByteArray &value)
{
    auto q = query();
    auto data = CFDataCreate(nullptr, reinterpret_cast<const UInt8 *>(value.constData()), value.size());
    auto attributes = CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks,
                                                &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(attributes, kSecValueData, data);
    auto status = SecItemUpdate(q, attributes);
    if (status == errSecItemNotFound) {
        CFDictionarySetValue(q, kSecValueData, data);
        CFDictionarySetValue(q, kSecAttrAccessible, kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly);
        status = SecItemAdd(q, nullptr);
    }
    CFRelease(attributes);
    CFRelease(data);
    CFRelease(q);
    return status == errSecSuccess;
}

void Keychain::clear()
{
    auto q = query();
    SecItemDelete(q);
    CFRelease(q);
}
#else
QByteArray Keychain::read() { return {}; }
bool Keychain::write(const QByteArray &) { return false; }
void Keychain::clear() {}
#endif
