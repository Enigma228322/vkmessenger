#pragma once
#include "Form.h"
#include <QVariantList>
#include <QVariantMap>

namespace Methods {
QVariantList catalog();
bool encode(const QString &method, const QVariantMap &values, Form &form, QString &error);
bool mutates(const QString &method);
}
