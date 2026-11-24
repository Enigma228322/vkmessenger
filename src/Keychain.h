#pragma once
#include <QByteArray>

namespace Keychain {
QByteArray read(const QByteArray &account = "vk-session");
bool write(const QByteArray &value, const QByteArray &account = "vk-session");
void clear(const QByteArray &account = "vk-session");
}
