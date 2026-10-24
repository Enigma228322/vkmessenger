#include "storage/LocalStore.h"
#include <QJsonArray>
#include <QTemporaryDir>
#include <QtTest>

class StorageTest : public QObject {
    Q_OBJECT
private slots:
    void stateSurvivesRestartAndClearKeepsPersonalData()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto page = QJsonDocument::fromJson(R"({"conversations_info":[{"conversation":{"peer":{"id":9007199254740993}},"message":[{"conversation_message_id":2,"peer_id":9007199254740993,"text":"Сохранённое"}]}],"profiles":[{"id":9007199254740993,"first_name":"Тест"}]})").object();
        {
            LocalStore store(directory.path());
            bool committed = false;
            QString failure;
            store.commitDiff("user:1", {page}, "9007199254740999", true, [&](auto, const QString &error) { failure = error; committed = true; });
            QTRY_VERIFY(committed); QVERIFY2(failure.isEmpty(), qPrintable(failure));
            store.saveDraft("user:1", "9007199254740993", {{"text", "Черновик"}});
            store.putOutgoing("user:1", {{"local_id", "local:1"}, {"random_id", "12345"}, {"text", "Не отправлено"}, {"created", 7}}, {});
        }
        LocalStore store(directory.path());
        bool loaded = false;
        QJsonObject state;
        store.loadAccount("user:1", [&](const auto &value, const auto &error) { QVERIFY(error.isEmpty()); state = value; loaded = true; });
        QTRY_VERIFY(loaded);
        QCOMPARE(state.value("version").toString(), "9007199254740999");
        QVERIFY(state.value("conversations").toObject().contains("9007199254740993"));
        loaded = false;
        store.loadHistory("user:1", "9007199254740993", [&](const auto &value, const auto &error) { QVERIFY(error.isEmpty()); state = value; loaded = true; });
        QTRY_VERIFY(loaded); QCOMPARE(state.value("messages").toArray().size(), 1);
        loaded = false;
        store.searchMessages("user:1", "СОХР", "9007199254740993", 0, [&](const auto &value, const auto &error) { QVERIFY2(error.isEmpty(), qPrintable(error)); state = value; loaded = true; });
        QTRY_VERIFY(loaded); QCOMPARE(state.value("items").toArray().size(), 1);
        loaded = false;
        store.clearHistory("user:1", [&](auto, const auto &error) { QVERIFY(error.isEmpty()); loaded = true; });
        QTRY_VERIFY(loaded);
        loaded = false;
        store.loadDrafts("user:1", [&](const auto &value, const auto &error) { QVERIFY(error.isEmpty()); state = value; loaded = true; });
        QTRY_VERIFY(loaded); QCOMPARE(state.value("9007199254740993").toObject().value("text").toString(), "Черновик");
        loaded = false;
        store.loadOutgoing("user:1", [&](const auto &value, const auto &error) { QVERIFY(error.isEmpty()); state = value; loaded = true; });
        QTRY_VERIFY(loaded); QCOMPARE(state.value("items").toArray().first().toObject().value("random_id").toString(), "12345");
        loaded = false;
        store.loadAccount("user:1", [&](const auto &value, const auto &) { state = value; loaded = true; });
        QTRY_VERIFY(loaded); QVERIFY(state.value("conversations").toObject().isEmpty()); QVERIFY(!state.contains("version"));
        loaded = false;
        store.searchMessages("user:1", "СОХР", {}, 0, [&](const auto &value, const auto &error) { QVERIFY(error.isEmpty()); state = value; loaded = true; });
        QTRY_VERIFY(loaded); QVERIFY(state.value("items").toArray().isEmpty());
    }

    void catalogsSurviveFullDiffAndRemainAccountScoped()
    {
        QTemporaryDir directory; LocalStore store(directory.path());
        const QJsonObject catalog{{"items", QJsonArray{QJsonObject{{"id", 5}, {"name", "Работа"}}}}};
        store.saveCatalog("user:1", "folders", catalog);
        store.commitDiff("user:1", {}, "20", true, {});
        bool done = false;
        store.loadAccount("user:1", [&](const auto &value, const auto &error) { QVERIFY(error.isEmpty()); QCOMPARE(value.value("catalogs").toObject().value("folders").toObject(), catalog); done = true; });
        QTRY_VERIFY(done); done = false;
        store.loadAccount("user:2", [&](const auto &value, const auto &error) { QVERIFY(error.isEmpty()); QVERIFY(value.value("catalogs").toObject().isEmpty()); done = true; });
        QTRY_VERIFY(done);
    }

    void deletionDirtyFlagsAndAccountIsolation()
    {
        QTemporaryDir directory;
        LocalStore store(directory.path());
        const QJsonArray messages{QJsonObject{{"conversation_message_id", 1}, {"text", "One"}}, QJsonObject{{"conversation_message_id", 2}, {"text", "Two"}}};
        store.saveHistory("user:1", "77", messages, {QJsonArray{1, 2}}, false, false);
        store.saveHistory("user:2", "77", messages, {}, false, false);
        const auto page = QJsonDocument::fromJson(R"({"conversations_info":[{"conversation":{"peer":{"id":77}},"range_deleted_cmids":[{"min":1,"max":1}],"range_updated_cmids":[{"min":2,"max":2}]}]})").object();
        store.commitDiff("user:1", {page}, "20", false, {});
        bool loaded = false;
        QJsonObject state;
        store.loadHistory("user:1", "77", [&](const auto &value, const auto &error) { QVERIFY(error.isEmpty()); state = value; loaded = true; });
        QTRY_VERIFY(loaded); QCOMPARE(state.value("messages").toArray().size(), 1); QCOMPARE(state.value("dirty").toArray(), QJsonArray{2});
        loaded = false;
        store.loadHistory("user:2", "77", [&](const auto &value, const auto &) { state = value; loaded = true; });
        QTRY_VERIFY(loaded); QCOMPARE(state.value("messages").toArray().size(), 2); QVERIFY(state.value("dirty").toArray().isEmpty());
    }
};
QTEST_GUILESS_MAIN(StorageTest)
#include "StorageTest.moc"
