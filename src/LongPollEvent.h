#pragma once
#include <QJsonArray>
#include <QJsonObject>

struct LongPollEvent {
    enum Kind { Persistent, Typing, Presence, Counter, Notification, Call, Folders, Unknown, Invalid };
    Kind kind = Invalid;
    int type = -1;
    QString peer;
    QJsonArray raw;
    static LongPollEvent parse(const QJsonValue &value)
    {
        LongPollEvent event;
        if (!value.isArray()) return event;
        event.raw = value.toArray();
        if (event.raw.isEmpty() || !event.raw[0].isDouble()) return event;
        const auto &a = event.raw;
        event.type = a[0].toInt(-1);
        auto peerAt = [&event, &a](int position, qint64 offset = 0) {
            if (a.size() <= position || !a[position].isDouble() || a[position].toInteger() == 0) return false;
            event.peer = QString::number(a[position].toInteger() + offset);
            return true;
        };
        switch (event.type) {
        case 10001: case 10002: case 10003: case 10005: case 10018:
            event.kind = a.size() >= 4 && peerAt(3) ? Persistent : Invalid;
            break;
        case 10004:
            event.kind = a.size() >= 5 && peerAt(4) ? Persistent : Invalid;
            break;
        case 10006: case 10007: case 10013: case 10014:
            if (a.size() >= 3 && peerAt(1)) event.kind = Persistent;
            break;
        case 10019:
            if (a.size() >= 3 && peerAt(2)) event.kind = Persistent;
            break;
        case 601:
            if (a.size() >= 4 && peerAt(2)) event.kind = Persistent;
            break;
        case 602: case 701:
            if (a.size() >= 2 && peerAt(1)) event.kind = Persistent;
            break;
        case 1: case 2: case 3: case 4: case 5:
            // Short flag events may have no peer id: reconcile all loaded state.
            event.kind = a.size() >= 3 ? Persistent : Invalid;
            peerAt(3);
            break;
        case 6: case 7:
            if (a.size() >= 3 && peerAt(1)) event.kind = Persistent;
            break;
        case 8: case 9:
            if (a.size() >= 4 && peerAt(1)) {
                event.peer = QString::number(qAbs(a[1].toInteger()));
                event.kind = Presence;
            }
            break;
        case 10: case 11: case 12: case 13: case 14:
        case 20: case 21: case 23:
            if (a.size() >= 3 && peerAt(1)) event.kind = Persistent;
            break;
        case 51:
            if (a.size() >= 3 && peerAt(1, 2000000000)) event.kind = Persistent;
            break;
        case 52:
            if (a.size() >= 3 && peerAt(2)) event.kind = Persistent;
            break;
        case 61:
            if (a.size() >= 3 && peerAt(1)) event.kind = Typing;
            break;
        case 62:
            if (a.size() >= 3 && peerAt(2, 2000000000)) event.kind = Typing;
            break;
        case 63: case 64: case 65: case 66: case 67: case 68:
            if (a.size() >= 3 && peerAt(1)) event.kind = Typing;
            break;
        case 70:
            if (a.size() >= 3 && peerAt(1)) event.kind = Call;
            break;
        case 80:
            if (a.size() >= 2 && a[1].isDouble() && a[1].toInt(-1) >= 0) event.kind = Counter;
            break;
        case 501: case 502: case 503: case 504: case 505: case 506: case 507: case 508: case 509:
        case 10501: case 10502: case 10503: case 10504: case 10505: case 10506: case 10507: case 10508: case 10509:
            event.kind = Folders;
            break;
        case 114:
            if (a.size() >= 2 && a[1].isObject()) {
                const auto peer = a[1].toObject().value("peer_id");
                if (peer.isDouble() && peer.toInteger() != 0) {
                    event.peer = QString::number(peer.toInteger());
                    event.kind = Notification;
                }
            }
            break;
        default:
            event.kind = Unknown;
            break;
        }
        return event;
    }
};
