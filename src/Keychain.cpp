#include "Keychain.h"
#ifdef Q_OS_MACOS
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>

static CFMutableDictionaryRef query(const QByteArray &account)
{
    auto result = CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks,
                                            &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(result, kSecClass, kSecClassGenericPassword);
    CFDictionarySetValue(result, kSecAttrService, CFSTR("dev.reforged.messenger"));
    auto name = CFStringCreateWithBytes(nullptr, reinterpret_cast<const UInt8 *>(account.constData()),
                                        account.size(), kCFStringEncodingUTF8, false);
    CFDictionarySetValue(result, kSecAttrAccount, name);
    CFRelease(name);
    return result;
}

QByteArray Keychain::read(const QByteArray &account)
{
    auto q = query(account);
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

bool Keychain::write(const QByteArray &value, const QByteArray &account)
{
    auto q = query(account);
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

void Keychain::clear(const QByteArray &account)
{
    auto q = query(account);
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

QByteArray Keychain::read(const QByteArray &account)
{
    GError *error = nullptr;
    gchar *password = secret_password_lookup_sync(&sessionSchema, nullptr, &error,
        "service", VKM_KEYCHAIN_SERVICE_ID, "account", account.constData(), nullptr);
    if (error) { g_error_free(error); return {}; }
    const QByteArray result = password ? QByteArray::fromBase64(password) : QByteArray();
    if (password) secret_password_free(password);
    return result;
}

bool Keychain::write(const QByteArray &value, const QByteArray &account)
{
    GError *error = nullptr;
    const auto encoded = value.toBase64();
    const bool stored = secret_password_store_sync(&sessionSchema, SECRET_COLLECTION_DEFAULT,
        account == "vk-session" ? "VK Messenger session" : "VK Messenger AI API key", encoded.constData(), nullptr, &error,
        "service", VKM_KEYCHAIN_SERVICE_ID, "account", account.constData(), nullptr);
    if (error) g_error_free(error);
    return stored;
}

void Keychain::clear(const QByteArray &account)
{
    GError *error = nullptr;
    secret_password_clear_sync(&sessionSchema, nullptr, &error,
        "service", VKM_KEYCHAIN_SERVICE_ID, "account", account.constData(), nullptr);
    if (error) g_error_free(error);
}
#else
QByteArray Keychain::read(const QByteArray &account) { return {}; }
bool Keychain::write(const QByteArray &, const QByteArray &) { return false; }
void Keychain::clear(const QByteArray &account) {}
#endif
