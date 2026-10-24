#pragma once
#include <QDateTime>
#include <QVariantList>

// Presentation only: never persist derived grouping or change message identity.
// Rules follow core/model/Convo.ts::flowHistory in the local web source.
namespace TimelinePresentation {
inline QVariantList decorate(QVariantList rows, qint64 inRead = 0)
{
    QVariantMap previous;
    bool unreadFound = false;
    const auto timestamp = [](const QVariantMap &row) {
        return row.value("sentAt", row.value("raw").toMap().value("date")).toLongLong();
    };
    const auto author = [](const QVariantMap &row) {
        const auto raw = row.value("raw").toMap();
        return raw.value("from_id", row.value("outgoing").toBool() ? QVariant("self") : row.value("sender")).toString();
    };
    const auto separates = [](const QVariantMap &row, bool older) {
        const auto raw = row.value("raw").toMap();
        if (row.value("gap").toBool() || raw.contains("action") || raw.value("expire_ttl").toInt() > 0) return true;
        for (const auto &value : row.value("attachments").toList()) {
            const auto type = value.toMap().value("type").toString();
            if (type == "gift" || type == "graffiti" || type == "call" || (older && type == "sticker")) return true;
        }
        return false;
    };
    for (auto &value : rows) {
        auto row = value.toMap();
        if (row.value("gap").toBool()) { previous = row; continue; }
        const auto sent = timestamp(row), before = timestamp(previous);
        const auto date = sent > 0 ? QDateTime::fromSecsSinceEpoch(sent).date() : QDate();
        const auto previousDate = before > 0 ? QDateTime::fromSecsSinceEpoch(before).date() : QDate();
        const bool newDate = date.isValid() && (previous.isEmpty() || date != previousDate || previous.value("gap").toBool());
        const bool unread = !unreadFound && !row.value("local").toBool() && !row.value("outgoing").toBool()
            && row.value("cmid").toLongLong() > inRead;
        unreadFound |= unread;
        const bool stack = !previous.isEmpty() && !newDate && !unread && sent > 0 && before > 0 && sent >= before && sent - before <= 300
            && !author(row).isEmpty() && author(row) == author(previous)
            && row.value("raw").toMap().value("admin_author_id") == previous.value("raw").toMap().value("admin_author_id")
            && !separates(previous, true) && !separates(row, false);
        row.insert("groupStart", !stack);
        row.insert("dateLabel", newDate ? date.toString("dd.MM.yyyy") : QString());
        row.insert("unreadStart", unread);
        value = row;
        previous = row;
    }
    return rows;
}
}
