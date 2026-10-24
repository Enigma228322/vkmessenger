#pragma once
#include <QUrl>
#include <QList>

using Form = QList<QPair<QString, QString>>;

// Encode raw values, including literal '+' and percent sequences in message text.
inline QByteArray formBody(const Form &parameters)
{
    QByteArray result;
    for (const auto &[key, value] : parameters) {
        if (!result.isEmpty()) result += '&';
        result += QUrl::toPercentEncoding(key) + '=' + QUrl::toPercentEncoding(value);
    }
    return result;
}
