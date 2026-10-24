#include "Methods.h"
#include "Json.h"
#include <QSet>
#include <QJsonDocument>
#include <QtTest>

class MethodsTest : public QObject {
    Q_OBJECT
private slots:
    void completePublicNamespace()
    {
        const QStringList expected = QString("addChatUser addChatUsers allowMessagesFromGroup createChat delete deleteChatPhoto deleteConversation deleteReaction denyMessagesFromGroup edit editChat getByConversationMessageId getById getChat getChatPreview getConversationMembers getConversations getConversationsById getHistory getHistoryAttachments getImportantMessages getIntentUsers getInviteLink getLastActivity getLongPollHistory getLongPollServer getMessagesReactions getReactedPeers getReactionsAssets isMessagesFromGroupAllowed joinChatByInviteLink markAsAnsweredConversation markAsImportant markAsImportantConversation markAsRead markReactionsAsRead muteChatMentions pin removeChatUser restore search searchConversations send sendMessageEventAnswer sendReaction setActivity setChatPhoto unpin").split(' ');
        QCOMPARE(expected.size(), 48);
        QSet<QString> actual;
        for (const auto &value : Methods::catalog()) {
            const auto method = value.toMap();
            const auto name = method.value("name").toString();
            QVERIFY(!actual.contains(name)); actual.insert(name);
            QVERIFY(!method.value("title").toString().isEmpty());
            QSet<QString> parameters;
            for (const auto &v : method.value("parameters").toList()) {
                const auto p = v.toMap();
                QVERIFY(!parameters.contains(p.value("name").toString())); parameters.insert(p.value("name").toString());
            }
        }
        for (const auto &method : expected) QVERIFY2(actual.contains("messages." + method), qPrintable(method));
        QVERIFY(actual.contains("messages.getDiff")); QVERIFY(actual.contains("messages.getDiffContent"));
    }
    void validatesAndPreservesInt64()
    {
        Form form; QString error;
        QVERIFY(Methods::encode("messages.sendReaction", {{"peer_id", "9007199254740993"}, {"cmid", "100"}, {"reaction_id", "1"}}, form, error));
        QVERIFY(form.contains(qMakePair(QString("peer_id"), QString("9007199254740993"))));
        form.clear(); QVERIFY(!Methods::encode("messages.sendReaction", {{"peer_id", "77"}}, form, error));
        form.clear(); QVERIFY(!Methods::encode("messages.sendReaction", {{"peer_id", "77"}, {"cmid", "x"}, {"reaction_id", "1"}}, form, error));
        form.clear(); QVERIFY(!Methods::encode("messages.send", {{"forward", "not-json"}}, form, error));
        form.clear(); QVERIFY(!Methods::encode("messages.send", {{"access_token", "injected"}}, form, error));
    }
    void largeIdsRemainExactInQmlResults()
    {
        const auto value = QJsonDocument::fromJson(R"({"peer_id":9007199254740993,"items":[{"user_id":-9007199254740993}],"count":3})").object();
        const auto result = safeJson(value).toMap();
        QCOMPARE(result.value("peer_id").toString(), QString("9007199254740993"));
        QCOMPARE(result.value("items").toList().first().toMap().value("user_id").toString(), QString("-9007199254740993"));
        QCOMPARE(result.value("count").toInt(), 3);
    }
};
QTEST_GUILESS_MAIN(MethodsTest)
#include "MethodsTest.moc"
