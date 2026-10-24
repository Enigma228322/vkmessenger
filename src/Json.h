#pragma once
#include <QJsonValue>
#include <QJsonObject>
#include <QJsonArray>
#include <QVariant>

// QML JavaScript numbers cannot represent every Int64. Keep large values as
// decimal strings at the UI boundary, while retaining exact integers in C++.
inline QVariant safeJson(const QJsonValue &value)
{
    if (value.isObject()) {
        QVariantMap result;
        const auto object = value.toObject();
        for (auto it = object.begin(); it != object.end(); ++it) result[it.key()] = safeJson(it.value());
        return result;
    }
    if (value.isArray()) {
        QVariantList result;
        for (const auto &item : value.toArray()) result.append(safeJson(item));
        return result;
    }
    if (value.isDouble()) {
        const auto integer = value.toInteger();
        if (integer > 9007199254740991LL || integer < -9007199254740991LL) return QString::number(integer);
    }
    return value.toVariant();
}
