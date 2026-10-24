#pragma once
#include <QByteArray>

namespace Keychain {
QByteArray read();
bool write(const QByteArray &value);
void clear();
}
