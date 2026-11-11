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
#elif defined(Q_OS_LINUX)
#pragma push_macro("signals")
#undef signals
#include <libsecret/secret.h>
#pragma pop_macro("signals")
#ifndef VKM_KEYCHAIN_SERVICE_ID
#define VKM_KEYCHAIN_SERVICE_ID "dev.reforged.messenger"
#endif

namespace {
const SecretSchema sessionSchema = {
    "dev.reforged.messenger", SECRET_SCHEMA_NONE,
    {{"service", SECRET_SCHEMA_ATTRIBUTE_STRING}, {"account", SECRET_SCHEMA_ATTRIBUTE_STRING}, {nullptr, SECRET_SCHEMA_ATTRIBUTE_STRING}}
};
}

QByteArray Keychain::read()
{
    GError *error = nullptr;
    gchar *password = secret_password_lookup_sync(&sessionSchema, nullptr, &error,
        "service", VKM_KEYCHAIN_SERVICE_ID, "account", "vk-session", nullptr);
    if (error) { g_error_free(error); return {}; }
    const QByteArray result = password ? QByteArray::fromBase64(password) : QByteArray();
    if (password) secret_password_free(password);
    return result;
}

bool Keychain::write(const QByteArray &value)
{
    GError *error = nullptr;
    const auto encoded = value.toBase64();
    const bool stored = secret_password_store_sync(&sessionSchema, SECRET_COLLECTION_DEFAULT,
        "VK Messenger session", encoded.constData(), nullptr, &error,
        "service", VKM_KEYCHAIN_SERVICE_ID, "account", "vk-session", nullptr);
    if (error) g_error_free(error);
    return stored;
}

void Keychain::clear()
{
    GError *error = nullptr;
    secret_password_clear_sync(&sessionSchema, nullptr, &error,
        "service", VKM_KEYCHAIN_SERVICE_ID, "account", "vk-session", nullptr);
    if (error) g_error_free(error);
}
#else
QByteArray Keychain::read() { return {}; }
bool Keychain::write(const QByteArray &) { return false; }
void Keychain::clear() {}
#endif
