#include "Methods.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>
#include <cmath>

// Public messages API 5.199, plus the v21 diff contracts used by VK.
// Each method has its own parameter form; conditional requirements are checked by VK.
static const char *specs[] = {
    "addChatUser|Добавить участника|Чаты|chat_id!:id,user_id:id,visible_messages_count:int,group_id:id",
    "addChatUsers|Добавить участников|Чаты|chat_id:id,user_ids:ids,visible_messages_count:int,group_id:id",
    "allowMessagesFromGroup|Разрешить сообщения сообщества|Сообщества|group_id!:id,key:string",
    "createChat|Создать чат|Чаты|user_ids:ids,title:string,group_id:id",
    "delete|Удалить сообщения|Сообщения|peer_id:id,cmids:ids,message_ids:ids,delete_for_all:bool,spam:bool,reason:int,group_id:id",
    "deleteChatPhoto|Удалить фотографию чата|Чаты|chat_id!:id,group_id:id",
    "deleteConversation|Удалить переписку|Диалоги|peer_id:id,user_id:id,group_id:id",
    "deleteReaction|Убрать реакцию|Реакции|peer_id!:id,cmid!:id",
    "denyMessagesFromGroup|Запретить сообщения сообщества|Сообщества|group_id!:id",
    "edit|Редактировать сообщение|Сообщения|peer_id!:id,cmid:id,message_id:id,message:text,attachment:string,keep_forward_messages:bool,keep_snippets:bool,lat:number,long:number,group_id:id,dont_parse_links:bool,disable_mentions:bool,template:json,keyboard:json",
    "editChat|Переименовать чат|Чаты|chat_id!:id,title:string,group_id:id",
    "getByConversationMessageId|Найти сообщения по CMID|История|peer_id!:id,conversation_message_ids!:ids,extended:bool,fields:strings,group_id:id",
    "getById|Найти сообщения по ID|История|message_ids:ids,cmids:ids,peer_id:id,preview_length:int,extended:bool,fields:strings,group_id:id",
    "getChat|Информация о чате|Чаты|chat_id:id,chat_ids:ids,fields:strings,name_case:string",
    "getChatPreview|Предпросмотр приглашения|Чаты|peer_id:id,link:string,fields:strings",
    "getConversationMembers|Участники чата|Чаты|peer_id!:id,offset:int,count:int,extended:bool,fields:strings,group_id:id,member_ids:ids",
    "getConversations|Список диалогов|Диалоги|offset:int,count:int,filter:string,extended:bool,start_message_id:id,fields:strings,group_id:id",
    "getConversationsById|Информация о диалогах|Диалоги|peer_ids!:ids,extended:bool,fields:strings,group_id:id",
    "getHistory|История переписки|История|peer_id:id,start_cmid:id,start_message_id:id,offset:int,count:int,user_id:id,rev:int,extended:bool,fields:strings,group_id:id",
    "getHistoryAttachments|Вложения переписки|Вложения|peer_id:id,media_type:string,attachment_types:strings,cmid:id,attachment_position:int,offset:int,count:int,extended:bool,fields:strings,max_forwards_level:int,message_video:bool,start_from:string,preserve_order:bool,photo_sizes:bool,group_id:id",
    "getImportantMessages|Важные сообщения|Сообщения|count:int,offset:int,start_message_id:id,preview_length:int,fields:strings,extended:bool,group_id:id",
    "getIntentUsers|Подписчики рассылки сообщества|Сообщества|intent!:string,subscribe_id:int,offset:int,count:int,extended:bool,name_case:string,fields:strings",
    "getInviteLink|Ссылка-приглашение в чат|Чаты|peer_id!:id,reset:bool,group_id:id",
    "getLastActivity|Последняя активность|Диалоги|user_id!:id",
    "getLongPollHistory|История событий Long Poll|Синхронизация|ts:id,pts:id,preview_length:int,onlines:bool,fields:strings,events_limit:int,msgs_limit:int,max_msg_id:id,group_id:id,lp_version:int,last_n:int,credentials:bool,extended:bool",
    "getLongPollServer|Параметры Long Poll|Синхронизация|need_pts:bool,group_id:id,lp_version:int",
    "getMessagesReactions|Реакции сообщений|Реакции|peer_id!:id,cmids!:ids",
    "getReactedPeers|Кто поставил реакцию|Реакции|peer_id!:id,cmid!:id,reaction_id:int",
    "getReactionsAssets|Доступные реакции|Реакции|client_version:int",
    "isMessagesFromGroupAllowed|Разрешены ли сообщения сообщества|Сообщества|group_id!:id,user_id!:id",
    "joinChatByInviteLink|Вступить по приглашению|Чаты|link!:string",
    "markAsAnsweredConversation|Отметить диалог отвеченным|Диалоги|peer_id!:id,answered:bool,group_id:id",
    "markAsImportant|Изменить важность сообщений|Сообщения|peer_id:id,cmids:ids,message_ids:ids,important:int",
    "markAsImportantConversation|Изменить важность диалога|Диалоги|peer_id!:id,important:bool,group_id:id",
    "markAsRead|Отметить сообщения прочитанными|Сообщения|peer_id:id,up_to_cmid:id,start_message_id:id,message_ids:ids,mark_conversation_as_read:bool,group_id:id",
    "markReactionsAsRead|Прочитать реакции|Реакции|peer_id!:id,cmids:ids",
    "muteChatMentions|Уведомления об упоминаниях|Чаты|peer_id!:id,mention_status!:int",
    "pin|Закрепить сообщение|Сообщения|peer_id!:id,cmid:id,message_id:id",
    "removeChatUser|Удалить участника / выйти|Чаты|chat_id!:id,user_id:id,member_id:id,group_id:id",
    "restore|Восстановить сообщение|Сообщения|peer_id:id,cmid:id,message_id:id,group_id:id",
    "search|Поиск сообщений|Поиск|q:string,peer_id:id,date:int,preview_length:int,offset:int,count:int,extended:bool,fields:strings,group_id:id",
    "searchConversations|Поиск диалогов|Поиск|q:string,count:int,extended:bool,fields:strings,group_id:id",
    "send|Отправить сообщение / стикер / клавиатуру|Сообщения|peer_id:id,message:text,attachment:string,reply_to:id,forward_messages:ids,forward:json,sticker_id:int,random_id:int,user_id:id,peer_ids:ids,domain:string,chat_id:id,lat:number,long:number,group_id:id,keyboard:json,template:json,payload:string,content_source:json,dont_parse_links:bool,disable_mentions:bool,intent:string,subscribe_id:int",
    "sendMessageEventAnswer|Ответить на событие кнопки|Сообщества|event_id!:string,user_id!:id,peer_id!:id,event_data:json",
    "sendReaction|Поставить реакцию|Реакции|peer_id!:id,cmid!:id,reaction_id!:int",
    "setActivity|Индикатор активности|Диалоги|peer_id:id,type:string,user_id:id,group_id:id",
    "setChatPhoto|Установить загруженную фотографию чата|Чаты|file!:string",
    "unpin|Открепить сообщение|Сообщения|peer_id!:id,group_id:id",
    "getDiff|Изменения диалогов|Синхронизация|from_version:id,to_version:id,conversations_source:string,conversations_limit:int,lp_version:int,extended_filters:strings,counter_filters:strings,supported_types:strings,nested_limit:int,group_id:id",
    "getDiffContent|Содержимое и изменения сообщений|Синхронизация|conversation_messages!:json,extended:bool,nested_limit:int,group_id:id",
    "getDiffConversationMembers|Изменения участников|Синхронизация|from_version:id,to_version:id,peer_ids:ids,limit:int,source:string,extended:bool,group_id:id",
    "markAsUnreadConversation|Отметить диалог непрочитанным|Диалоги|peer_id!:id,group_id:id",
    "changeConversationMemberRestrictions|Ограничить участника|Чаты|peer_id!:id,member_ids_long!:ids,action!:string,for:int",
    "setMemberRole|Роль участника|Чаты|peer_id!:id,member_id!:id,role!:string,group_id:id",
    "pinConversation|Закрепить диалог|Диалоги|peer_id!:id,group_id:id",
    "unpinConversation|Открепить диалог|Диалоги|peer_id!:id,group_id:id",
    "archiveConversation|Архивировать диалог|Диалоги|peer_id!:id",
    "unarchiveConversation|Извлечь диалог из архива|Диалоги|peer_id!:id",
    "account.setSilenceMode|Настройки уведомлений|Диалоги|peer_id!:id,time!:int,sound:bool,group_id:id",
    "sendMessageEvent|Нажать callback-кнопку бота|Сообщества|peer_id!:id,payload!:string,author_id!:id,cmid:id,from:string"
};

static QString label(const QString &name)
{
    static const QHash<QString, QString> labels{
        {"peer_id", "ID диалога"}, {"peer_ids", "ID диалогов"}, {"chat_id", "ID чата (без 2000000000)"},
        {"message", "Текст"}, {"title", "Название"}, {"user_id", "ID пользователя"}, {"user_ids", "ID участников"},
        {"member_id", "ID участника"}, {"member_ids", "ID участников"}, {"group_id", "ID сообщества"},
        {"cmid", "CMID сообщения"}, {"cmids", "CMID сообщений"}, {"message_id", "ID сообщения"},
        {"message_ids", "ID сообщений"}, {"q", "Поисковый запрос"}, {"count", "Количество"}, {"offset", "Смещение"},
        {"link", "Ссылка-приглашение"}, {"attachment", "Вложения VK"}, {"delete_for_all", "Удалить у всех"},
        {"important", "Важное"}, {"reaction_id", "ID реакции"}, {"extended", "Подробные данные"},
        {"reply_to", "ID сообщения для ответа"}, {"forward", "Параметры пересылки"},
        {"reset", "Создать новую ссылку"}, {"file", "Результат загрузки фотографии"},
        {"media_type", "Тип вложений: photo, video, doc, audio…"}, {"type", "Тип активности (typing)"},
        {"keyboard", "Клавиатура (JSON)"}, {"template", "Шаблон (JSON)"}, {"payload", "Payload (JSON)"},
        {"up_to_cmid", "Прочитать до CMID"}, {"fields", "Поля профилей"}};
    return labels.value(name, name);
}

QVariantList Methods::catalog()
{
    QVariantList result;
    for (auto spec : specs) {
        const auto parts = QString::fromUtf8(spec).split('|');
        QVariantList fields;
        for (const auto &field : parts[3].split(',')) {
            const auto pair = field.split(':');
            auto name = pair[0];
            const bool required = name.endsWith('!');
            if (required) name.chop(1);
            fields.append(QVariantMap{{"name", name}, {"label", label(name)}, {"type", pair[1]}, {"required", required}});
        }
        result.append(QVariantMap{{"name", parts[0].contains('.') ? parts[0] : "messages." + parts[0]}, {"title", parts[1]}, {"category", parts[2]}, {"parameters", fields}});
    }
    return result;
}

bool Methods::encode(const QString &method, const QVariantMap &values, Form &form, QString &error)
{
    QVariantMap spec;
    for (const auto &v : catalog()) if (v.toMap().value("name").toString() == method) { spec = v.toMap(); break; }
    if (spec.isEmpty()) { error = "Неизвестный метод Messages API."; return false; }
    QSet<QString> known;
    for (const auto &v : spec.value("parameters").toList()) {
        const auto field = v.toMap();
        const auto name = field.value("name").toString(), type = field.value("type").toString();
        known.insert(name);
        auto text = values.value(name).toString();
        if (type != "text") text = text.trimmed();
        if (text.isEmpty()) {
            if (field.value("required").toBool()) { error = "Заполните поле: " + label(name); return false; }
            continue;
        }
        bool ok = true;
        if (type == "id" || type == "int") text.toLongLong(&ok);
        else if (type == "number") { const auto number = text.toDouble(&ok); ok &= std::isfinite(number); }
        else if (type == "bool") {
            if (text == "true") text = "1";
            if (text == "false") text = "0";
            ok = text == "1" || text == "0";
        } else if (type == "ids") {
            QStringList ids;
            for (auto part : text.split(',', Qt::SkipEmptyParts)) {
                part = part.trimmed(); bool valid; part.toLongLong(&valid); ok &= valid; ids << part;
            }
            ok &= !ids.isEmpty(); text = ids.join(',');
        } else if (type == "json") {
            QJsonParseError parse;
            const auto doc = QJsonDocument::fromJson(text.toUtf8(), &parse);
            ok = parse.error == QJsonParseError::NoError && !doc.isNull();
            if (ok) text = QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
        }
        if (!ok) { error = "Некорректное значение поля: " + label(name); return false; }
        form.append({name, text});
    }
    for (auto it = values.begin(); it != values.end(); ++it)
        if (!known.contains(it.key()) && !it.value().toString().isEmpty()) { error = "Неизвестный параметр: " + it.key(); return false; }
    return true;
}

bool Methods::mutates(const QString &method)
{
    return !method.startsWith("messages.get") && !method.startsWith("messages.search") && !method.startsWith("messages.is");
}
