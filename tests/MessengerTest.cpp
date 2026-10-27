#include "Messenger.h"
#include "models/TimelinePresentation.h"
#include <QClipboard>
#include "Keychain.h"
#include "LottieSticker.h"
#include <QGuiApplication>
#include <QDesktopServices>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QUrlQuery>
#include <QJSValue>
#include <QBuffer>
#include <QImageReader>
#include <QPainter>
#include <QStandardPaths>
#include <QDir>
#include <QTemporaryDir>
#include <QtTest>
#include <cstring>

// Integration tests never read or change the user's macOS Keychain.
static QByteArray savedSession;
QByteArray Keychain::read() { return savedSession; }
bool Keychain::write(const QByteArray &value) { savedSession = value; return true; }
void Keychain::clear() { savedSession.clear(); }

class TokenReply : public QNetworkReply {
public:
    TokenReply(const QNetworkRequest &request, const QByteArray &body, QObject *parent)
        : QNetworkReply(parent), m_body(body)
    {
        setRequest(request);
        setUrl(request.url());
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
        if (!body.isEmpty()) QTimer::singleShot(0, this, [this] { deliver(m_body); });
    }
    void deliver(QByteArray body) {
        if (isFinished()) return;
        m_body = std::move(body); m_offset = 0;
        if (m_body == "#offline") setError(QNetworkReply::ConnectionRefusedError, "offline");
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, 200);
        setFinished(true); emit readyRead(); emit finished();
    }
    void abort() override {
        if (isFinished()) return;
        setError(OperationCanceledError, "cancelled");
        setFinished(true);
        emit finished();
    }
    qint64 bytesAvailable() const override { return m_body.size() - m_offset + QIODevice::bytesAvailable(); }
protected:
    qint64 readData(char *data, qint64 max) override {
        const auto size = qMin(max, qint64(m_body.size() - m_offset));
        if (size <= 0) return -1;
        std::memcpy(data, m_body.constData() + m_offset, size);
        m_offset += size;
        return size;
    }
private:
    QByteArray m_body;
    qint64 m_offset = 0;
};

class TokenNetwork : public QNetworkAccessManager {
public:
    QByteArray response = R"({"response":[{"id":42}]})";
    QList<QPair<QUrl, QByteArray>> requests;
    QHash<QString, QList<QByteArray>> responses;
    QHash<QString, QList<QPointer<TokenReply>>> replies;
protected:
    QNetworkReply *createRequest(Operation, const QNetworkRequest &request, QIODevice *outgoing) override {
        requests.append({request.url(), outgoing ? outgoing->readAll() : QByteArray()});
        // Hold all other requests so tests never contact a live VK endpoint.
        const auto method = request.url().path().section('/', -1);
        QByteArray data = method == "users.get" ? response : QByteArray();
        if (!responses[method].isEmpty()) data = responses[method].takeFirst();
        auto reply = new TokenReply(request, data, this);
        replies[method].append(reply);
        return reply;
    }
};

class LoginBrowserCapture : public QObject {
    Q_OBJECT
public:
    QUrl url;
public slots:
    void capture(const QUrl &value) { url = value; }
};

class MessengerTest : public QObject {
    Q_OBJECT
    QTemporaryDir m_storage;
private slots:
    void init() {
        savedSession.clear();
        QDir(m_storage.path()).removeRecursively();
        QCoreApplication::instance()->setProperty("vkmStoragePath", m_storage.path());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_storage.path() + "/settings");
    }

    void configuredVkIdProvidesProductionHttpsReturn()
    {
        Messenger messenger;
        messenger.setProperty("clientId", "54850913");
        messenger.setProperty("redirectUri", "https://153-51-114-47.sslip.io/callback");
        QQmlApplicationEngine engine; engine.rootContext()->setContextProperty("messenger", &messenger);
        engine.load(QUrl::fromLocalFile(QStringLiteral(NATIVE_QML_PATH))); QVERIFY(!engine.rootObjects().isEmpty());
        auto root = engine.rootObjects().first();
        LoginBrowserCapture browser;
        QDesktopServices::setUrlHandler("https", &browser, "capture");
        messenger.login();
        QDesktopServices::unsetUrlHandler("https");
        QVERIFY(messenger.browserLoginPending());
        QCOMPARE(browser.url.host(), QString("id.vk.ru"));
        const QUrlQuery parameters(browser.url);
        QCOMPARE(parameters.queryItemValue("client_id"), QString("54850913"));
        QCOMPARE(parameters.queryItemValue("app_id"), parameters.queryItemValue("client_id"));
        QCOMPARE(parameters.queryItemValue("sdk_type"), QString("vkid"));
        QCOMPARE(parameters.queryItemValue("v"), QString("2.6.1"));
        QCOMPARE(parameters.queryItemValue("response_type"), QString("code"));
        QCOMPARE(parameters.queryItemValue("code_challenge_method"), QString("s256"));
        QVERIFY(!parameters.hasQueryItem("code_verifier"));
        QVERIFY(!parameters.hasQueryItem("client_secret"));
        QCOMPARE(parameters.queryItemValue("redirect_uri", QUrl::FullyDecoded), QString("https://153-51-114-47.sslip.io/callback"));
        QVERIFY(!parameters.queryItemValue("state").isEmpty());
        QVERIFY(!parameters.queryItemValue("code_challenge").isEmpty());
        auto dialog = root->findChild<QObject *>("vkIdReturnDialog"); QVERIFY(dialog);
        auto field = root->findChild<QObject *>("vkIdReturnInput"); QVERIFY(field);
        auto submit = root->findChild<QObject *>("vkIdReturnSubmit"); QVERIFY(submit);
        QTRY_VERIFY(dialog->property("visible").toBool());
        QVERIFY(field->property("visible").toBool());
        QCOMPARE(field->property("echoMode").toInt(), 2);
        QVERIFY(!submit->property("enabled").toBool());
        field->setProperty("text", "https://153-51-114-47.sslip.io/callback?code=test&state=wrong&device_id=test");
        QVERIFY(submit->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(submit, "clicked"));
        QVERIFY(!messenger.authorizing());
        QVERIFY(!messenger.browserLoginPending());
        QVERIFY(!messenger.connected());
        QVERIFY(!messenger.error().isEmpty());
        QTRY_VERIFY(!dialog->property("visible").toBool());
        QCOMPARE(field->property("text").toString(), QString());
    }

    void manualTokenLoginAndPersistence()
    {
        TokenNetwork network;
        Messenger messenger(nullptr, &network);
        messenger.loginWithToken("  manual+token/%2B== \n");
        QVERIFY(messenger.authorizing());
        QVERIFY(!messenger.browserLoginPending());
        QVERIFY(!messenger.connected());
        QTRY_VERIFY(messenger.connected());
        QVERIFY(!messenger.authorizing());
        QVERIFY(!messenger.demo());
        QCOMPARE(network.requests.first().first.path(), QString("/method/users.get"));
        const QUrlQuery body(QString::fromUtf8(network.requests.first().second));
        QCOMPARE(body.queryItemValue("access_token", QUrl::FullyDecoded), QString("manual+token/%2B=="));
        const auto saved = QJsonDocument::fromJson(savedSession).object();
        QCOMPARE(saved.value("access_token").toString(), QString("manual+token/%2B=="));
        QCOMPARE(saved.value("auth_type").toString(), QString("manual_token"));
        QVERIFY(!saved.contains("client_id"));
        messenger.logout();
        QVERIFY(savedSession.isEmpty());
        QVERIFY(!messenger.connected());
    }

    void invalidTokenReturnsToLogin()
    {
        TokenNetwork network;
        network.response = R"({"error":{"error_code":5,"error_msg":"User authorization failed"}})";
        Messenger messenger(nullptr, &network);
        messenger.loginWithToken("expired-token");
        QTRY_VERIFY(!messenger.authorizing());
        QVERIFY(!messenger.connected());
        QVERIFY(messenger.error().contains("Сессия"));
        QVERIFY(savedSession.isEmpty());
        QCOMPARE(network.requests.size(), 1);
    }

    void savedManualTokenRestored()
    {
        savedSession = R"({"access_token":"saved-token","auth_type":"manual_token"})";
        TokenNetwork network;
        Messenger messenger(nullptr, &network);
        QTRY_VERIFY(messenger.connected());
        const QUrlQuery body(QString::fromUtf8(network.requests.first().second));
        QCOMPARE(body.queryItemValue("access_token"), QString("saved-token"));
    }

    void communityTokenIdentityAndGroupContext()
    {
        TokenNetwork network;
        network.responses["groups.getById"] = {R"({"response":{"groups":[{"id":555,"name":"Community"}]}})"};
        Messenger messenger(nullptr, &network);
        messenger.loginWithToken("community-token", "555");
        QTRY_VERIFY(messenger.connected());
        QCOMPARE(network.requests.first().first.path(), QString("/method/groups.getById"));
        QCOMPARE(messenger.methodDefaults().value("group_id").toString(), QString("555"));
        const auto saved = QJsonDocument::fromJson(savedSession).object();
        QCOMPARE(saved.value("user_id").toInteger(), -555);
    }

    void malformedTokenDoesNotReplaceSession()
    {
        TokenNetwork network;
        Messenger messenger(nullptr, &network);
        messenger.startDemo();
        messenger.loginWithToken("  \n ");
        QVERIFY(messenger.demo());
        messenger.loginWithToken("two tokens");
        QVERIFY(messenger.demo());
        QVERIFY(!messenger.error().isEmpty());
        QVERIFY(network.requests.isEmpty());
    }

    void qmlTokenEntryConnectsWithoutVkIdSettings()
    {
        TokenNetwork network;
        Messenger messenger(nullptr, &network);
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("messenger", &messenger);
        engine.load(QUrl::fromLocalFile(QStringLiteral(NATIVE_QML_PATH)));
        QVERIFY(!engine.rootObjects().isEmpty());
        const auto root = engine.rootObjects().first();
        auto field = root->findChild<QObject *>("accessTokenInput");
        auto button = root->findChild<QObject *>("accessTokenInputConnect");
        QVERIFY(field);
        QVERIFY(button);
        QVERIFY(field->property("visible").toBool());
        QCOMPARE(field->property("echoMode").toInt(), 2); // TextInput.Password
        QVERIFY(!button->property("enabled").toBool());
        field->setProperty("text", "pasted-token");
        QVERIFY(button->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(button, "clicked"));
        QTRY_VERIFY(messenger.connected());
        QCOMPARE(field->property("text").toString(), QString());
    }

    void demoLifecycle()
    {
        Messenger messenger;
        messenger.startDemo();
        QVERIFY(messenger.connected());
        QVERIFY(messenger.demo());
        QCOMPARE(messenger.conversations()->rowCount(), 4);
        QCOMPARE(messenger.messages()->rowCount(), 3);
        QSignalSpy sent(&messenger, &Messenger::messageSent);
        messenger.send("Привет + C++ & Qt ✨");
        QCOMPARE(sent.count(), 1);
        QCOMPARE(messenger.messages()->rowCount(), 4);
        QCOMPARE(messenger.messages()->rows.last().toMap().value("text").toString(), QString("Привет + C++ & Qt ✨"));
        messenger.selectConversation("2", "Анна");
        QCOMPARE(messenger.messages()->rowCount(), 3);
        messenger.selectConversation("1", "Команда");
        QCOMPARE(messenger.messages()->rowCount(), 4);
        messenger.send("   ");
        QCOMPARE(sent.count(), 1);
        messenger.send(QString(4097, 'a'));
        QCOMPARE(sent.count(), 1);
        QVERIFY(!messenger.error().isEmpty());
        messenger.logout();
        QVERIFY(!messenger.connected());
        QCOMPARE(messenger.messages()->rowCount(), 0);
        QCOMPARE(messenger.conversations()->rowCount(), 0);
    }

    void qmlSendAndDrafts()
    {
        Messenger messenger;
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("messenger", &messenger);
        engine.load(QUrl::fromLocalFile(QStringLiteral(NATIVE_QML_PATH)));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto root = engine.rootObjects().first();
        messenger.startDemo();
        QCoreApplication::processEvents();
        auto composer = root->findChild<QObject *>("composer");
        QVERIFY(composer);
        const auto select = [root](const QString &peer, const QString &title) {
            return QMetaObject::invokeMethod(root, "selectChat", Q_ARG(QVariant, peer), Q_ARG(QVariant, title));
        };
        QVERIFY(select("1", "Команда"));
        composer->setProperty("text", "Первый черновик");
        QVERIFY(select("2", "Анна"));
        QCOMPARE(composer->property("text").toString(), QString());
        composer->setProperty("text", "Второй черновик");
        QVERIFY(select("1", "Команда"));
        QCOMPARE(composer->property("text").toString(), QString("Первый черновик"));
        QVERIFY(QMetaObject::invokeMethod(composer, "submit"));
        QCOMPARE(messenger.messages()->rowCount(), 4);
        QCOMPARE(composer->property("text").toString(), QString());
        QVERIFY(select("2", "Анна"));
        QCOMPARE(composer->property("text").toString(), QString("Второй черновик"));
        messenger.logout();
        QCOMPARE(composer->property("text").toString(), QString());
        QVERIFY(!messenger.demo());
    }

    void diffLoadsHistoryUsingCmidsAndMarksRead()
    {
        TokenNetwork network;
        network.responses["messages.getDiff"] = {R"({"response":{"server_version":"500","credentials":{"server_lp":"lp.test/im","key":"key","ts":"100"},"conversations_info":[{"conversation":{"peer":{"id":77},"last_conversation_message_id":3,"in_read_cmid":1,"out_read_cmid":2,"unread_count":2},"message":[{"id":900,"conversation_message_id":3,"peer_id":77,"from_id":77,"date":1700000000,"text":"Last"}]}],"profiles":[{"id":77,"first_name":"Анна","last_name":"Тест"}],"counters":{"messages":2}}})"};
        network.responses["messages.getDiffContent"] = {R"({"response":{"items":[{"peer_id":77,"messages":[{"id":900,"conversation_message_id":3,"peer_id":77,"from_id":77,"date":1700000000,"text":"Last"},{"id":800,"conversation_message_id":2,"peer_id":77,"from_id":42,"out":1,"date":1700000000,"text":"Outgoing"},{"id":700,"conversation_message_id":1,"peer_id":77,"from_id":77,"date":1700000000,"text":"First"}]}]}})"};
        network.responses["messages.markAsRead"] = {R"({"response":1})"};
        Messenger messenger(nullptr, &network);
        messenger.loginWithToken("token");
        QTRY_COMPARE_WITH_TIMEOUT(messenger.conversations()->rowCount(), 1, 3000);
        QCOMPARE(messenger.conversations()->get(0).value("title").toString(), QString("Анна Тест"));
        messenger.selectConversation("77", "Анна");
        QTRY_COMPARE_WITH_TIMEOUT(messenger.messages()->rowCount(), 3, 3000);
        QCOMPARE(messenger.messages()->get(0).value("id").toString(), QString("1"));
        QCOMPARE(messenger.messages()->get(1).value("read").toBool(), true);
        QCOMPARE(messenger.messages()->get(2).value("globalId").toString(), QString("900"));
        messenger.markVisibleRead("3");
        QTRY_VERIFY_WITH_TIMEOUT(std::any_of(network.requests.begin(), network.requests.end(), [](auto r) { return r.first.path().endsWith("messages.markAsRead"); }), 3000);
        for (const auto &r : network.requests) {
            QVERIFY(!r.first.path().endsWith("messages.getHistory"));
            QVERIFY(!r.first.path().endsWith("messages.getConversations"));
            if (r.first.path().endsWith("messages.markAsRead"))
                QCOMPARE(QUrlQuery(QString::fromUtf8(r.second)).queryItemValue("up_to_cmid"), QString("3"));
        }
    }

    void diffPaginationCommitsConsistentVersion()
    {
        TokenNetwork network;
        network.responses["messages.getDiff"] = {
            R"({"response":{"server_version":"9007199254740993","credentials":{"server_lp":"lp.test/im","key":"key","ts":"100"},"conversations_source":"page2","conversations_info":[{"conversation":{"peer":{"id":77}},"message":[]}]}})",
            R"({"response":{"server_version":"9007199254740993","conversations_info":[{"conversation":{"peer":{"id":88}},"message":[]}]}})"};
        Messenger messenger(nullptr, &network);
        messenger.loginWithToken("token");
        QTRY_COMPARE_WITH_TIMEOUT(messenger.conversations()->rowCount(), 2, 4000);
        int pages = 0;
        for (const auto &r : network.requests) if (r.first.path().endsWith("messages.getDiff")) {
            const QUrlQuery params(QString::fromUtf8(r.second));
            if (++pages == 2) {
                QCOMPARE(params.queryItemValue("conversations_source"), QString("page2"));
                QCOMPARE(params.queryItemValue("to_version"), QString("9007199254740993"));
            }
        }
        QCOMPARE(pages, 2);
    }

    void invalidateAllRestartsFromFreshSnapshot()
    {
        TokenNetwork network;
        network.responses["messages.getDiff"] = {
            R"({"response":{"server_version":"500","credentials":{"server_lp":"lp.test/im","key":"key","ts":"100"},"conversations_info":[{"conversation":{"peer":{"id":77}},"message":[]}]}})",
            R"({"response":{"server_version":"501","invalidate_all":true}})",
            R"({"response":{"server_version":"600","credentials":{"server_lp":"lp.test/im","key":"new-key","ts":"200"},"conversations_info":[{"conversation":{"peer":{"id":88}},"message":[]}]}})"};
        Messenger messenger(nullptr, &network);
        messenger.loginWithToken("token");
        QTRY_COMPARE_WITH_TIMEOUT(messenger.conversations()->rowCount(), 1, 3000);
        messenger.reload();
        QTRY_COMPARE_WITH_TIMEOUT(messenger.conversations()->get(0).value("peer").toString(), QString("88"), 4000);
        int diffs = 0;
        for (const auto &r : network.requests) if (r.first.path().endsWith("messages.getDiff") && ++diffs == 3)
            QVERIFY(!QUrlQuery(QString::fromUtf8(r.second)).hasQueryItem("from_version"));
        QCOMPARE(diffs, 3);
    }

    void qmlJumpButtonAndScrollAnchor()
    {
        Messenger messenger;
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("messenger", &messenger);
        engine.load(QUrl::fromLocalFile(QStringLiteral(NATIVE_QML_PATH)));
        QVERIFY(!engine.rootObjects().isEmpty());
        messenger.startDemo();
        for (int i = 0; i < 40; ++i) messenger.send("Длинная переписка, сообщение " + QString::number(i));
        auto root = engine.rootObjects().first();
        auto history = root->findChild<QObject *>("historyList");
        auto button = root->findChild<QObject *>("jumpToEndButton");
        QVERIFY(history); QVERIFY(button);
        QTest::qWait(150);
        // Programmatic positioning does not emit the user-scroll signal.
        QVERIFY(QMetaObject::invokeMethod(history, "movementStarted"));
        QVERIFY(QMetaObject::invokeMethod(history, "positionViewAtBeginning"));
        QTRY_VERIFY(button->property("visible").toBool());
        history->setProperty("contentY", history->property("contentY").toReal() + 200);
        QTest::qWait(100);
        auto view = qobject_cast<QQuickItem *>(history);
        QVERIFY(view);
        auto content = qvariant_cast<QQuickItem *>(history->property("contentItem"));
        QVERIFY(content);
        QString anchor;
        qreal offset = 0;
        for (auto row : content->childItems()) {
            if (row->objectName() != "messageRow" || row->y() + row->height() <= history->property("contentY").toReal()) continue;
            if (anchor.isEmpty() || row->y() < offset) {
                anchor = row->property("item").toMap().value("id").toString();
                offset = row->y();
            }
        }
        QVERIFY(!anchor.isEmpty());
        offset -= history->property("contentY").toReal();
        auto rows = messenger.messages()->rows;
        rows.prepend(QVariantMap{{"id", "0"}, {"text", QString("Предыдущая история. ").repeated(20)}, {"outgoing", false}});
        messenger.historyAboutToChange();
        messenger.messages()->reconcile(rows);
        messenger.historyChanged();
        QTest::qWait(200);
        bool found = false;
        for (auto row : content->childItems()) {
            if (row->objectName() == "messageRow" && row->property("item").toMap().value("id").toString() == anchor) {
                found = true;
                QVERIFY(qAbs(row->y() - history->property("contentY").toReal() - offset) <= 1);
            }
        }
        QVERIFY(found);
        qobject_cast<QQuickWindow *>(root)->resize(900, 650);
        QTest::qWait(200);
        for (auto row : content->childItems()) {
            if (row->objectName() == "messageRow" && row->property("item").toMap().value("id").toString() == anchor) {
                QVERIFY2(qAbs(row->y() - history->property("contentY").toReal() - offset) <= 1,
                    qPrintable(QString("anchor=%1 expected=%2 actual=%3 cached=%4/%5 end=%6").arg(anchor).arg(offset).arg(row->y() - history->property("contentY").toReal()).arg(root->property("geometryAnchor").toString()).arg(root->property("geometryOffset").toReal()).arg(root->property("wasAtEnd").toBool())));
            }
        }
        QVERIFY(QMetaObject::invokeMethod(button, "clicked"));
        QTRY_VERIFY(!button->property("visible").toBool());
    }

    void pinnedPreviewAndFilteredConversationBoundary()
    {
        TokenNetwork network;
        QJsonArray conversations;
        for (int i = 0; i < 3; ++i) {
            QJsonObject settings{{"title", i == 2 ? "Обычный чат" : QString("Закреплённый %1").arg(i)}};
            if (i == 0) settings.insert("pinned_message", QJsonObject{{"conversation_message_id", 1}, {"text", QString("Длинное закреплённое сообщение\n").repeated(150)}});
            conversations.append(QJsonObject{{"conversation", QJsonObject{{"peer", QJsonObject{{"id", 77 + i}}}, {"sort_id", QJsonObject{{"major_id", i < 2 ? 16 : 0}, {"minor_id", 100 - i}}}, {"unread_count", i == 0 ? 0 : 1}, {"chat_settings", settings}}}, {"message", QJsonArray{}}});
        }
        network.responses["messages.getDiff"] = {QJsonDocument(QJsonObject{{"response", QJsonObject{{"server_version", "500"}, {"credentials", QJsonObject{{"server_lp", "lp.test/im"}, {"key", "key"}, {"ts", "100"}}}, {"conversations_info", conversations}}}}).toJson(QJsonDocument::Compact)};
        Messenger messenger(nullptr, &network);
        QQmlApplicationEngine engine; engine.rootContext()->setContextProperty("messenger", &messenger);
        engine.load(QUrl::fromLocalFile(QStringLiteral(NATIVE_QML_PATH))); QVERIFY(!engine.rootObjects().isEmpty());
        auto window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        messenger.loginWithToken("test-token"); QTRY_COMPARE(messenger.conversations()->rowCount(), 3);
        messenger.selectConversation("77", "Закреплённый чат");
        QVERIFY(messenger.conversations()->get(0).value("pinned").toBool());
        QVERIFY(messenger.conversations()->get(1).value("pinned").toBool());
        QVERIFY(!messenger.conversations()->get(2).value("pinned").toBool());
        QTRY_COMPARE(window->property("pinnedBoundaryPeer").toString(), QString("79"));
        const auto visibleDividers = [&] {
            int count = 0;
            const auto visit = [&](auto &&self, QQuickItem *item) -> void {
                if (item->objectName() == "pinnedChatsDivider" && item->isVisible()) ++count;
                for (auto child : item->childItems()) self(self, child);
            };
            visit(visit, window->contentItem()); return count;
        };
        QTRY_COMPARE(visibleDividers(), 1);
        auto bar = window->findChild<QQuickItem *>("pinnedMessageBar"); QVERIFY(bar);
        auto preview = window->findChild<QQuickItem *>("pinnedMessagePreview"); QVERIFY(preview);
        auto history = window->findChild<QQuickItem *>("historyList"); QVERIFY(history);
        for (const QSize size : {QSize(1180, 780), QSize(780, 560)}) {
            window->resize(size); QTest::qWait(100);
            QVERIFY(bar->isVisible()); QCOMPARE(bar->height(), 56);
            QVERIFY(history->height() > 150);
            QVERIFY(!preview->property("text").toString().contains('\n'));
            QVERIFY(preview->property("truncated").toBool());
            QVERIFY(preview->width() > 0);
        }
        window->setProperty("unreadOnly", true);
        QTRY_COMPARE(window->property("pinnedBoundaryPeer").toString(), QString("79"));
        auto rows = messenger.conversations()->rows;
        auto second = rows[1].toMap(); second.insert("pinned", false); rows[1] = second;
        messenger.conversations()->reconcile(rows);
        QTRY_COMPARE(window->property("pinnedBoundaryPeer").toString(), QString());
        window->setProperty("unreadOnly", false);
        QTRY_COMPARE(window->property("pinnedBoundaryPeer").toString(), QString("78"));
        window->setProperty("search", "обычный");
        QTRY_COMPARE(window->property("pinnedBoundaryPeer").toString(), QString());
        QTRY_COMPARE(visibleDividers(), 0);
    }

    void timelineGroupingBoundaries()
    {
        const auto row = [](const QString &id, int author, qint64 date, bool outgoing = false) {
            return QVariantMap{{"id", id}, {"cmid", id}, {"outgoing", outgoing}, {"raw", QVariantMap{{"from_id", QString::number(author)}, {"date", date}}}};
        };
        const auto base = QDateTime(QDate(2026, 10, 24), QTime(12, 0)).toSecsSinceEpoch();
        QVariantList input{row("1", 42, base), row("2", 42, base + 30), row("3", 42, base + 60), row("4", 43, base + 90), row("5", 43, base + 391)};
        auto output = TimelinePresentation::decorate(input, 2);
        QVERIFY(output[0].toMap().value("groupStart").toBool());
        QVERIFY(!output[1].toMap().value("groupStart").toBool());
        QVERIFY(output[2].toMap().value("groupStart").toBool()); // unread boundary
        QVERIFY(output[2].toMap().value("unreadStart").toBool());
        QVERIFY(output[3].toMap().value("groupStart").toBool()); // another author
        QVERIFY(output[4].toMap().value("groupStart").toBool()); // > five minutes
        QVERIFY(!output[0].toMap().value("dateLabel").toString().isEmpty());
        // Removing the first unread row must recompute the following row's boundary.
        input.removeAt(2);
        output = TimelinePresentation::decorate(input, 2);
        QVERIFY(output[2].toMap().value("unreadStart").toBool());
        auto sticker = row("6", 43, base + 400);
        sticker.insert("attachments", QVariantList{QVariantMap{{"type", "sticker"}}});
        output = TimelinePresentation::decorate({sticker, row("7", 43, base + 410)}, 10);
        QVERIFY(output[1].toMap().value("groupStart").toBool());
        output = TimelinePresentation::decorate({row("8", 43, base), QVariantMap{{"gap", true}}, row("10", 43, base + 5)}, 10);
        QVERIFY(output[2].toMap().value("groupStart").toBool());
        const auto midnight = QDateTime(QDate(2026, 10, 25), QTime(0, 0)).toSecsSinceEpoch();
        output = TimelinePresentation::decorate({row("11", 42, midnight - 1), row("12", 42, midnight)}, 20);
        QVERIFY(output[1].toMap().value("groupStart").toBool());
        QVERIFY(!output[1].toMap().value("dateLabel").toString().isEmpty());
    }

    void messagePopupCopiesTextAndClosesOnResize()
    {
        Messenger messenger;
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("messenger", &messenger);
        engine.load(QUrl::fromLocalFile(QStringLiteral(NATIVE_QML_PATH)));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        messenger.startDemo();
        auto popup = window->findChild<QObject *>("messageActionPopup"); QVERIFY(popup);
        auto history = window->findChild<QQuickItem *>("historyList"); QVERIFY(history);
        QVariantMap message{{"id", "1"}, {"text", "Текст + C++ & <tag>"}, {"outgoing", true}};
        QVERIFY(QMetaObject::invokeMethod(popup, "showAt", Q_ARG(QVariant, QVariant::fromValue(history)), Q_ARG(QVariant, message), Q_ARG(QVariant, QVariant::fromValue(history))));
        QTRY_VERIFY(popup->property("opened").toBool());
        QVERIFY(popup->property("x").toReal() >= 0);
        QVERIFY(popup->property("y").toReal() >= 0);
        QVERIFY(popup->property("x").toReal() + popup->property("width").toReal() <= window->width());
        QVERIFY(popup->property("y").toReal() + popup->property("height").toReal() <= window->height());
        QQuickItem *copy = nullptr;
        const auto findCopy = [&](auto &&self, QQuickItem *item) -> void {
            if (item->objectName() == "messageAction_copy") copy = item;
            for (auto child : item->childItems()) self(self, child);
        };
        findCopy(findCopy, window->contentItem()); QVERIFY(copy);
        QVERIFY(QMetaObject::invokeMethod(copy, "clicked"));
        QTRY_COMPARE(QGuiApplication::clipboard()->text(), message.value("text").toString());
        QTRY_VERIFY(!popup->property("opened").toBool());
        QVERIFY(QMetaObject::invokeMethod(popup, "open"));
        QTRY_VERIFY(popup->property("opened").toBool());
        window->resize(800, 600);
        QTRY_VERIFY(!popup->property("opened").toBool());
    }

    void inlineEditingKeepsDraftOnFailureCancelAndSuccess()
    {
        TokenNetwork network;
        const QByteArray diff = R"({"response":{"server_version":"500","credentials":{"server_lp":"lp.test/im","key":"key","ts":"100"},"conversations_info":[{"conversation":{"peer":{"id":77},"last_conversation_message_id":1},"message":[{"id":900,"conversation_message_id":1,"peer_id":77,"from_id":42,"out":1,"date":1792830000,"text":"Исходное сообщение"}]}]}})";
        network.responses["messages.getDiff"] = {diff, diff};
        network.responses["messages.getDiffContent"] = {R"({"response":{"items":[{"peer_id":77,"messages":[{"id":900,"conversation_message_id":1,"peer_id":77,"from_id":42,"out":1,"date":1792830000,"text":"Исходное сообщение"}]}]}})"};
        network.responses["messages.edit"] = {R"({"error":{"error_code":100,"error_msg":"Test edit rejected"}})", R"({"response":1})"};
        Messenger messenger(nullptr, &network);
        QQmlApplicationEngine engine; engine.rootContext()->setContextProperty("messenger", &messenger);
        engine.load(QUrl::fromLocalFile(QStringLiteral(NATIVE_QML_PATH))); QVERIFY(!engine.rootObjects().isEmpty());
        auto root = engine.rootObjects().first();
        messenger.loginWithToken("test-token"); QTRY_COMPARE(messenger.conversations()->rowCount(), 1);
        messenger.selectConversation("77", "Тестовый диалог"); QTRY_COMPARE(messenger.messages()->rowCount(), 1);
        messenger.setDraftText("77", "Мой черновик");
        auto composer = root->findChild<QObject *>("composer"); QVERIFY(composer);
        const auto edit = [&] { return QMetaObject::invokeMethod(root, "messageActions", Q_ARG(QVariant, "edit"), Q_ARG(QVariant, messenger.messages()->get(0))); };
        QVERIFY(edit());
        QCOMPARE(root->property("editingMessage").toString(), QString("1"));
        QCOMPARE(composer->property("text").toString(), QString("Исходное сообщение"));
        composer->setProperty("text", "Исправленный текст");
        QCOMPARE(messenger.draftText(), QString("Мой черновик"));
        QVERIFY(QMetaObject::invokeMethod(root, "cancelEditing"));
        QCOMPARE(composer->property("text").toString(), QString("Мой черновик"));
        QVERIFY(edit());
        composer->setProperty("text", "Исправленный текст");
        QVERIFY(QMetaObject::invokeMethod(composer, "submit"));
        QTRY_VERIFY(!messenger.actions()->busy());
        QVERIFY(!messenger.actions()->error().isEmpty());
        QCOMPARE(root->property("editingMessage").toString(), QString("1"));
        QCOMPARE(composer->property("text").toString(), QString("Исправленный текст"));
        QCOMPARE(messenger.draftText(), QString("Мой черновик"));
        QVERIFY(QMetaObject::invokeMethod(composer, "submit"));
        QTRY_COMPARE(root->property("editingMessage").toString(), QString());
        QCOMPARE(composer->property("text").toString(), QString("Мой черновик"));
        QCOMPARE(messenger.draftText(), QString("Мой черновик"));
        QVERIFY(edit());
        composer->setProperty("text", "Не отправленная правка");
        QVERIFY(QMetaObject::invokeMethod(root, "selectChat", Q_ARG(QVariant, "88"), Q_ARG(QVariant, "Другой диалог")));
        QCOMPARE(root->property("editingMessage").toString(), QString());
        messenger.selectConversation("77", "Тестовый диалог");
        QCOMPARE(messenger.draftText(), QString("Мой черновик"));
        int edits = 0;
        for (const auto &request : network.requests) {
            QVERIFY(!request.first.path().endsWith("messages.send"));
            if (request.first.path().endsWith("messages.edit")) ++edits;
        }
        QCOMPARE(edits, 2);
    }

    void profileUsesIndependentResultsAndKeepsDraft()
    {
        TokenNetwork network;
        network.responses["messages.getDiff"] = {R"({"response":{"server_version":"500","credentials":{"server_lp":"lp.test/im","key":"key","ts":"100"},"conversations_info":[{"conversation":{"peer":{"id":2000000077},"chat_settings":{"title":"Тестовая беседа"}},"message":[]}]}})"};
        network.responses["messages.getConversationMembers"] = {R"({"response":{"count":1,"items":[{"member_id":42}],"profiles":[{"id":42,"first_name":"Тестовый","last_name":"Участник"}]}})"};
        network.responses["messages.getHistoryAttachments"] = {R"({"response":{"items":[]}})"};
        Messenger messenger(nullptr, &network);
        QQmlApplicationEngine engine; engine.rootContext()->setContextProperty("messenger", &messenger);
        engine.load(QUrl::fromLocalFile(QStringLiteral(NATIVE_QML_PATH))); QVERIFY(!engine.rootObjects().isEmpty());
        auto window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        messenger.loginWithToken("test-token"); QTRY_VERIFY(messenger.connected());
        messenger.selectConversation("2000000077", "Тестовая беседа");
        messenger.setDraftText("2000000077", "Несохранённый ввод");
        auto profile = window->findChild<QObject *>("conversationProfile"); QVERIFY(profile);
        QVERIFY(QMetaObject::invokeMethod(profile, "present", Q_ARG(QVariant, QVariant::fromValue(window->contentItem()))));
        QTRY_VERIFY(profile->property("opened").toBool());
        QTRY_COMPARE(messenger.profileActions()->results().size(), 1);
        QCOMPARE(messenger.profileActions()->results().first().toMap().value("title").toString(), QString("Тестовый Участник"));
        if (qEnvironmentVariableIsSet("VKM_PROFILE_SCREENSHOT")) QVERIFY(window->grabWindow().save(qEnvironmentVariable("VKM_PROFILE_SCREENSHOT")));
        messenger.actions()->reset();
        QCOMPARE(messenger.profileActions()->results().size(), 1);
        QCOMPARE(messenger.draftText(), QString("Несохранённый ввод"));
        profile->setProperty("section", 1);
        QVERIFY(QMetaObject::invokeMethod(profile, "loadSection"));
        QTRY_VERIFY(!messenger.profileActions()->busy());
        QCOMPARE(messenger.profileActions()->results().size(), 0);
        QVERIFY(QMetaObject::invokeMethod(profile, "close"));
        QTRY_VERIFY(!profile->property("opened").toBool());
        QCOMPARE(messenger.selectedPeer(), QString("2000000077"));
        QCOMPARE(messenger.draftText(), QString("Несохранённый ввод"));
    }

    void uiMessageGeometry()
    {
        Messenger messenger;
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("messenger", &messenger);
        engine.load(QUrl::fromLocalFile(QStringLiteral(NATIVE_QML_PATH)));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window);
        messenger.startDemo();
        messenger.send("Короткое сообщение");
        messenger.send("фыв");
        messenger.send("1");
        messenger.send("C++");
        messenger.send("Длинное сообщение для проверки отступов и переноса текста. " + QString("Содержимое сообщения должно оставаться внутри пузыря. ").repeated(5));
        messenger.send("Несколько строк\nВторая строка\nТретья строка");
        auto history = window->findChild<QQuickItem *>("historyList");
        QVERIFY(history);
        // Delegates belong to the visual tree rather than the QObject tree.
        const auto collect = [](QQuickItem *parent, const QString &name) {
            QList<QQuickItem *> result;
            const auto visit = [&](auto &&self, QQuickItem *item) -> void {
                if (item->objectName() == name) result.append(item);
                for (auto child : item->childItems()) self(self, child);
            };
            visit(visit, parent);
            return result;
        };
        for (const bool flat : {true, false}) {
        window->setProperty("flatMessages", flat);
        for (const QSize size : {QSize(1440, 900), QSize(1180, 780), QSize(780, 560)}) {
            window->resize(size);
            QTest::qWait(250);
            const auto bubbles = collect(history, "messageBubble");
            QVERIFY(!bubbles.isEmpty());
            for (auto bubble : bubbles) {
                QVERIFY(bubble->width() > 0);
                QVERIFY(bubble->x() >= 15);
                QVERIFY(bubble->x() + bubble->width() <= history->width() - 15);
                for (const auto &name : {QString("messageFooter"), QString("messageTimestamp"), QString("readIndicator")}) {
                    for (auto footer : collect(bubble, name)) {
                        if (!footer->isVisible()) continue;
                        const auto position = footer->mapToItem(bubble, QPointF());
                        QVERIFY2(position.x() >= 11, qPrintable(name));
                        QVERIFY2(position.x() + footer->width() <= bubble->width() - 11 + 0.5, qPrintable(name));
                        QVERIFY2(position.y() + footer->height() <= bubble->height() - 8 + 0.5, qPrintable(name));
                    }
                }
                for (auto indicator : collect(bubble, "readIndicator")) {
                    if (!indicator->isVisible()) continue;
                    const auto timestamp = collect(bubble, "messageTimestamp").first();
                    const auto statusX = indicator->mapToItem(bubble, QPointF()).x();
                    const auto timeX = timestamp->mapToItem(bubble, QPointF()).x();
                    QVERIFY(flat ? statusX < timeX : statusX > timeX);
                }
                for (auto text : collect(bubble, "messageText")) {
                    if (!text->isVisible()) continue;
                    const auto position = text->mapToItem(bubble, QPointF());
                    QVERIFY(position.x() >= 11);
                    QVERIFY(position.y() >= 8);
                    QVERIFY(position.x() + text->width() <= bubble->width() - 11);
                    QVERIFY(position.y() + text->height() <= bubble->height() - 8);
                    QVERIFY(text->height() >= text->property("contentHeight").toReal());
                    if (QStringList{"фыв", "1", "C++"}.contains(text->property("text").toString())) QCOMPARE(text->property("lineCount").toInt(), 1);
                }
            }
            QTRY_VERIFY(history->property("atYEnd").toBool());
        }
        }
        window->setProperty("flatMessages", true);
        QTest::qWait(150);
        if (qEnvironmentVariableIsSet("VKM_SCREENSHOT")) {
            QVERIFY(window->grabWindow().save(qEnvironmentVariable("VKM_SCREENSHOT")));
        }
    }

    void reactionCatalogAndImagesRenderInMessagesAndPicker()
    {
        QFile svgFile(QFINDTESTDATA("fixtures/reaction.svg")); QVERIFY(svgFile.open(QIODevice::ReadOnly));
        const auto svg = svgFile.readAll();
        QVERIFY(QImageReader::supportedImageFormats().contains("svg"));
        const auto svgImage = QImage::fromData(svg); QVERIFY(!svgImage.isNull()); QCOMPARE(svgImage.size(), QSize(48, 48));
        const auto webp = QByteArray::fromBase64("UklGRiIAAABXRUJQVlA4IBYAAAAwAQCdASoBAAEADsD+JaQAA3AAAAAA");
#ifdef Q_OS_MACOS
        QVERIFY(QImageReader::supportedImageFormats().contains("webp"));
        const auto decoded = QImage::fromData(webp, "webp"); QVERIFY(!decoded.isNull()); QCOMPARE(decoded.size(), QSize(1, 1));
#endif
        QImage icon(48, 48, QImage::Format_ARGB32_Premultiplied); icon.fill(Qt::transparent);
        QPainter painter(&icon); painter.setPen(Qt::NoPen); painter.setBrush(QColor("#ed657b")); painter.drawEllipse(4, 4, 40, 40); painter.end();
        QByteArray png; QBuffer buffer(&png); QVERIFY(buffer.open(QIODevice::WriteOnly)); QVERIFY(icon.save(&buffer, "PNG"));
        TokenNetwork network;
        const QByteArray catalog = R"({"response":{"assets":[{"reaction_id":1,"links":{"static":"https://assets.test/base.png"}},{"reaction_id":2,"links":{"static":"https://assets.test/second.webp"}},{"reaction_id":3,"links":{"static":"https://assets.test/third.svg"}},{"reaction_id":4,"links":{"static":"https://assets.test/hidden.png"}}],"override_assets":[{"reaction_id":1,"links":{"static":"https://assets.test/override.png"}}],"reaction_ids":[1,2,3]}})";
        network.responses["messages.getReactionsAssets"] = {catalog, catalog, catalog};
        network.responses["override.png"] = {png}; network.responses["second.webp"] = {webp};
        network.responses["third.svg"] = {svg};
        network.responses["messages.getFolders"] = {R"({"response":{"items":[]}})"};
        network.responses["messages.getDiff"] = {R"({"response":{"server_version":"500","credentials":{"server_lp":"lp.test/im","key":"key","ts":"100"},"conversations_info":[{"conversation":{"peer":{"id":77},"last_conversation_message_id":1,"out_read_cmid":1},"message":[{"id":900,"conversation_message_id":1,"peer_id":77,"from_id":42,"date":1792830000,"out":1,"text":"adsda","reactions":[{"reaction_id":1,"count":1,"user_ids":[42]},{"reaction_id":2,"count":12,"user_ids":[42]}]}]}]}})"};
        network.responses["messages.getDiffContent"] = {R"({"response":{"items":[{"peer_id":77,"messages":[{"id":900,"conversation_message_id":1,"peer_id":77,"from_id":42,"date":1792830000,"out":1,"text":"adsda","reactions":[{"reaction_id":1,"count":1,"user_ids":[42]},{"reaction_id":2,"count":12,"user_ids":[42]}]}]}]}})"};
        auto diff = QJsonDocument::fromJson(network.responses["messages.getDiff"].first()).object().value("response").toObject();
        auto info = diff.value("conversations_info").toArray().first().toObject();
        auto conversation = info.value("conversation").toObject(); conversation.insert("last_conversation_message_id", 3); conversation.insert("out_read_cmid", 3); info.insert("conversation", conversation);
        auto messages = info.value("message").toArray();
        auto reactedMessage = messages[0].toObject();
        auto reactions = reactedMessage.value("reactions").toArray();
        reactions.append(QJsonObject{{"reaction_id", 3}, {"count", 2}, {"user_ids", QJsonArray{42, 77}}});
        reactedMessage.insert("reactions", reactions); messages[0] = reactedMessage;
        messages.append(QJsonObject{{"conversation_message_id", 2}, {"peer_id", 77}, {"from_id", 42}, {"date", 1792830060}, {"out", 1}, {"text", ""}, {"attachments", QJsonArray{QJsonObject{{"type", "sticker"}, {"sticker", QJsonObject{{"sticker_id", 1}, {"images", QJsonArray{QJsonObject{{"url", "https://assets.test/second.webp"}, {"width", 1}, {"height", 1}}}}}}}}}});
        messages.append(QJsonObject{{"conversation_message_id", 3}, {"peer_id", 77}, {"from_id", 42}, {"date", 1792830120}, {"out", 1}, {"text", "фыв"}});
        info.insert("message", messages); diff.insert("conversations_info", QJsonArray{info});
        network.responses["messages.getDiff"] = {QJsonDocument(QJsonObject{{"response", diff}}).toJson(QJsonDocument::Compact)};
        network.responses["messages.getDiffContent"] = {QJsonDocument(QJsonObject{{"response", QJsonObject{{"items", QJsonArray{QJsonObject{{"peer_id", 77}, {"messages", messages}}}}}}}).toJson(QJsonDocument::Compact)};
        Messenger messenger(nullptr, &network);
        QQmlApplicationEngine engine; engine.rootContext()->setContextProperty("messenger", &messenger);
        engine.load(QUrl::fromLocalFile(QStringLiteral(NATIVE_QML_PATH))); QVERIFY(!engine.rootObjects().isEmpty());
        auto window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        messenger.loginWithToken("test-token"); QTRY_COMPARE(messenger.conversations()->rowCount(), 1);
        messenger.selectConversation("77", "Реакции"); QTRY_COMPARE(messenger.messages()->rowCount(), 3);
        const auto visualItems = [window](const QString &name) {
            QList<QQuickItem *> items;
            const auto visit = [&](auto &&self, QQuickItem *item) -> void { if (item->objectName() == name) items.append(item); for (auto child : item->childItems()) self(self, child); };
            visit(visit, window->contentItem()); return items;
        };
        QTRY_COMPARE(messenger.actions()->reactionAssets().size(), 4);
        QCOMPARE(messenger.actions()->reactionImage(1), "https://assets.test/override.png");
        QCOMPARE(messenger.actions()->reactionImage(2), "https://assets.test/second.webp");
        QCOMPARE(messenger.actions()->reactionImage(3), "https://assets.test/third.svg");
        QCOMPARE(messenger.actions()->selectableReactions().size(), 3);
        QTRY_COMPARE(visualItems("reactionChip").size(), 3);
        QTRY_VERIFY([&] { const auto images = visualItems("reactionImage"); return images.size() == 3 && std::all_of(images.begin(), images.end(), [](auto image) { return image->property("status").toInt() == 1; }); }());
        const auto cachedSvg = messenger.media()->cached(QUrl("https://assets.test/third.svg"));
        QVERIFY(cachedSvg.isLocalFile()); QVERIFY(QFileInfo(cachedSvg.toLocalFile()).suffix().isEmpty());
        const auto cachedSvgImage = QImageReader(cachedSvg.toLocalFile()).read();
        QVERIFY(!cachedSvgImage.isNull()); QCOMPARE(cachedSvgImage.size(), QSize(48, 48));
        QVERIFY(cachedSvgImage.pixelColor(24, 24).alpha() > 0);
        QTRY_VERIFY(!messenger.loading());
        QTRY_VERIFY([&] { const auto images = visualItems("attachmentImage"); return images.size() == 1 && images.first()->property("status").toInt() == 1 && images.first()->height() > 100; }());
        auto chips = visualItems("reactionChip");
        QVERIFY(chips[0]->width() < 80); QCOMPARE(chips[0]->height(), 30);
        QVERIFY(chips[1]->x() > chips[0]->x()); QCOMPARE(chips[0]->y(), chips[1]->y());
        if (qEnvironmentVariableIsSet("VKM_REACTION_SCREENSHOT")) QVERIFY(window->grabWindow().save(qEnvironmentVariable("VKM_REACTION_SCREENSHOT")));
        auto picker = window->findChild<QObject *>("reactionPicker"); QVERIFY(picker); QVERIFY(QMetaObject::invokeMethod(picker, "open"));
        QTRY_VERIFY(!messenger.actions()->busy()); QTRY_COMPARE(messenger.actions()->results().size(), 3);
        QTRY_VERIFY([&] { const auto images = visualItems("reactionImage"); return images.size() >= 6 && std::all_of(images.begin(), images.end(), [](auto image) { return image->property("status").toInt() == 1; }); }());
        if (qEnvironmentVariableIsSet("VKM_PICKER_SCREENSHOT")) QVERIFY(window->grabWindow().save(qEnvironmentVariable("VKM_PICKER_SCREENSHOT")));
        QVERIFY(QMetaObject::invokeMethod(picker, "close"));
        auto popup = window->findChild<QObject *>("messageActionPopup"); QVERIFY(popup);
        auto history = window->findChild<QQuickItem *>("historyList"); QVERIFY(history);
        QVERIFY(QMetaObject::invokeMethod(popup, "showAt", Q_ARG(QVariant, QVariant::fromValue(history)), Q_ARG(QVariant, messenger.messages()->get(0)), Q_ARG(QVariant, QVariant::fromValue(history))));
        QTRY_VERIFY(popup->property("opened").toBool());
        QTRY_COMPARE(visualItems("quickReaction").size(), 3); // server-hidden ID is not offered
        QTRY_VERIFY([&] { for (auto button : visualItems("quickReaction")) { const auto image = qvariant_cast<QQuickItem *>(button->property("contentItem")); if (!image || !image->property("ready").toBool()) return false; } return true; }());
        if (qEnvironmentVariableIsSet("VKM_ACTIONS_SCREENSHOT")) QVERIFY(window->grabWindow().save(qEnvironmentVariable("VKM_ACTIONS_SCREENSHOT")));

    }

    void diffAppliesEditsDeletesAndNewMessages()
    {
        TokenNetwork network;
        network.responses["messages.getDiff"] = {
            R"({"response":{"server_version":"500","credentials":{"server_lp":"lp.test/im","key":"key","ts":"100"},"conversations_info":[{"conversation":{"peer":{"id":77},"last_conversation_message_id":3},"message":[{"id":900,"conversation_message_id":3,"peer_id":77,"text":"Third"}]}]}})",
            R"({"response":{"server_version":"501","conversations_info":[{"conversation":{"peer":{"id":77},"last_conversation_message_id":4},"message":[{"conversation_message_id":4,"peer_id":77,"text":"Fourth"}],"cmids_flags":[{"cmid":1,"updated_flags":1},{"cmid":2,"updated_flags":2}]}]}})"};
        network.responses["messages.getDiffContent"] = {
            R"({"response":{"items":[{"peer_id":77,"messages":[{"conversation_message_id":1,"text":"First"},{"conversation_message_id":2,"text":"Second"},{"conversation_message_id":3,"text":"Third"}]}]}})",
            R"({"response":{"items":[{"peer_id":77,"messages":[{"conversation_message_id":2,"text":"Edited"},{"conversation_message_id":3,"text":"Third"},{"conversation_message_id":4,"text":"Fourth"}],"requested_messages":[{"conversation_message_id":2,"text":"Edited"}]}]}})"};
        Messenger messenger(nullptr, &network);
        messenger.loginWithToken("token");
        QTRY_COMPARE_WITH_TIMEOUT(messenger.conversations()->rowCount(), 1, 3000);
        messenger.selectConversation("77", "Chat");
        QTRY_COMPARE_WITH_TIMEOUT(messenger.messages()->rowCount(), 3, 3000);
        messenger.applyLongPoll({QJsonValue(QJsonArray{10004, 4, 0, 0, 77})});
        QTRY_COMPARE_WITH_TIMEOUT(messenger.messages()->get(0).value("text").toString(), QString("Edited"), 4000);
        QCOMPARE(messenger.messages()->rowCount(), 3);
        QCOMPARE(messenger.messages()->find("1"), -1);
        QCOMPARE(messenger.messages()->get(2).value("text").toString(), QString("Fourth"));
        int diffs = 0;
        for (const auto &r : network.requests) if (r.first.path().endsWith("messages.getDiff") && ++diffs == 2)
            QCOMPARE(QUrlQuery(QString::fromUtf8(r.second)).queryItemValue("from_version"), QString("500"));
    }

    void allActionFormsCanBeOpened()
    {
        Messenger messenger;
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("messenger", &messenger);
        engine.load(QUrl::fromLocalFile(QStringLiteral(NATIVE_QML_PATH)));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto browser = engine.rootObjects().first()->findChild<QObject *>("methodBrowser");
        QVERIFY(browser);
        for (const auto &entry : messenger.methods()) {
            const auto method = entry.toMap().value("name").toString();
            QVERIFY(QMetaObject::invokeMethod(browser, "choose", Q_ARG(QVariant, method), Q_ARG(QVariant, QVariantMap())));
            QCoreApplication::processEvents();
            const auto selected = browser->property("selected");
            const auto map = selected.metaType() == QMetaType::fromType<QJSValue>() ? selected.value<QJSValue>().toVariant().toMap() : selected.toMap();
            QCOMPARE(map.value("name").toString(), method);
        }
    }

    void cacheAndOutgoingSurviveOfflineRestart()
    {
        QString randomId, localId;
        {
            TokenNetwork network;
            network.responses["messages.getDiff"] = {R"({"response":{"server_version":"500","credentials":{"server_lp":"lp.test/im","key":"key","ts":"100"},"conversations_info":[{"conversation":{"peer":{"id":77},"last_conversation_message_id":1},"message":[{"id":900,"conversation_message_id":1,"peer_id":77,"text":"Сохранённая история"}]}]}})"};
            network.responses["messages.getDiffContent"] = {R"({"response":{"items":[{"peer_id":77,"messages":[{"id":900,"conversation_message_id":1,"peer_id":77,"text":"Сохранённая история"}]}]}})"};
            network.responses["messages.send"] = {"#offline"};
            Messenger messenger(nullptr, &network);
            messenger.loginWithToken("test-token");
            QTRY_COMPARE(messenger.conversations()->rowCount(), 1);
            messenger.selectConversation("77", "Собеседник");
            QTRY_COMPARE(messenger.messages()->rowCount(), 1);
            messenger.setDraftText("77", "Новый черновик");
            messenger.sendWithOptions("Сообщение без сети", true);
            QTRY_VERIFY(messenger.offline());
            QCOMPARE(messenger.outgoingCount(), 1);
            localId = messenger.messages()->get(1).value("id").toString();
            QVERIFY(localId.startsWith("local:"));
            for (const auto &request : network.requests) if (request.first.path().endsWith("messages.send"))
                randomId = QUrlQuery(QString::fromUtf8(request.second)).queryItemValue("random_id");
            QVERIFY(!randomId.isEmpty());
            QCOMPARE(messenger.messages()->get(1).value("delivery").toString(), "uncertain");
        }
        TokenNetwork network;
        network.response = "#offline";
        Messenger messenger(nullptr, &network);
        QTRY_VERIFY(messenger.connected());
        QTRY_VERIFY(!messenger.authorizing());
        QVERIFY(messenger.offline());
        messenger.selectConversation("77", "Собеседник");
        QTRY_COMPARE(messenger.messages()->rowCount(), 2);
        QCOMPARE(messenger.messages()->get(0).value("text").toString(), "Сохранённая история");
        QCOMPARE(messenger.messages()->get(1).value("id").toString(), localId);
        QCOMPARE(messenger.draftText(), "Новый черновик");
        messenger.actions()->search("Сохранённая", "77");
        QTRY_VERIFY(!messenger.actions()->busy());
        QCOMPARE(messenger.actions()->results().size(), 1);
        QVERIFY(messenger.actions()->cachedResults());
        messenger.openMessage("77", "1");
        QTRY_COMPARE(messenger.messages()->rowCount(), 2);
        QCOMPARE(messenger.messages()->get(0).value("text").toString(), "Сохранённая история");
        network.responses["messages.send"] = {R"({"response":12345})"};
        messenger.retryOutgoing(localId);
        QTRY_VERIFY(std::any_of(network.requests.begin(), network.requests.end(), [](const auto &r) { return r.first.path().endsWith("messages.send"); }));
        for (const auto &request : network.requests) if (request.first.path().endsWith("messages.send")) {
            const auto params = QUrlQuery(QString::fromUtf8(request.second));
            QCOMPARE(params.queryItemValue("random_id"), randomId);
            QCOMPARE(params.queryItemValue("silent"), "1");
        }
        QTRY_COMPARE(messenger.messages()->get(1).value("delivery").toString(), "sent");
    }

    void foldersUsePeerPickerContractsAndSurviveOfflineRestart()
    {
        {
            TokenNetwork network;
            network.responses["messages.getDiff"] = {R"({"response":{"server_version":"500","credentials":{"server_lp":"lp.test/im","key":"key","ts":"100"},"conversations_info":[{"conversation":{"peer":{"id":77},"peer_flags":1},"message":[]},{"conversation":{"peer":{"id":88},"peer_flags":0},"message":[]}]}})"};
            network.responses["messages.getFolders"] = {R"({"response":{"items":[{"id":1,"name":"Личное","flags":8,"included_lists":["personal"],"excluded_peer_ids":[88],"included_peer_ids":[88]},{"id":2,"name":"Работа","flags":0,"included_peer_ids":[88]}],"included_lists_info":[{"type":"personal","filter":{"and":1,"xor":1}}]}})"};
            network.responses["messages.createFolder"] = {R"({"response":{"folder_id":3}})", R"({"response":{}})"};
            network.responses["messages.updateFolder"] = {R"({"error":{"error_code":15,"error_msg":"Access denied"}})", R"({"response":1})"};
            network.responses["messages.reorderFolders"] = {R"({"response":1})"};
            network.responses["messages.deleteFolder"] = {R"({"response":1})"};
            Messenger messenger(nullptr, &network);
            messenger.loginWithToken("test-token"); auto folders = messenger.folders();
            QTRY_COMPARE(folders->items().size(), 2);
            QVERIFY(folders->contains("1", "77")); // dynamic list uses peer_flags
            QVERIFY(!folders->contains("1", "88")); // exclusion wins over explicit inclusion
            QVERIFY(folders->contains("2", "88"));
            QVERIFY(!folders->get("1").value("editable").toBool());
            QSignalSpy completed(folders, &FolderController::completed);
            folders->create("Друзья", {"77"}); QTRY_COMPARE(completed.count(), 1);
            QCOMPARE(folders->items().size(), 3); QVERIFY(folders->contains("3", "77"));
            folders->update("3", "Новое", {"88"}); QTRY_VERIFY(!folders->busy());
            QVERIFY(!folders->error().isEmpty()); QVERIFY(folders->contains("3", "77")); QCOMPARE(completed.count(), 1);
            folders->update("3", "Новое", {"88"}); QTRY_COMPARE(completed.count(), 2);
            QVERIFY(folders->contains("3", "88")); QVERIFY(!folders->contains("3", "77"));
            for (const auto &request : network.requests) if (request.first.path().endsWith("messages.updateFolder")) {
                const QUrlQuery body(QString::fromUtf8(request.second));
                QCOMPARE(body.queryItemValue("add_included_peer_ids"), "88"); QCOMPARE(body.queryItemValue("remove_included_peer_ids"), "77");
            }
            folders->move("3", -1); QTRY_COMPARE(completed.count(), 3);
            QCOMPARE(folders->items()[0].toMap().value("id").toString(), "1");
            QCOMPARE(folders->items()[1].toMap().value("id").toString(), "3");
            folders->remove("2"); QTRY_COMPARE(completed.count(), 4); QCOMPARE(folders->items().size(), 2);
            folders->create("Ошибка", {"77"}); QTRY_VERIFY(!folders->busy());
            QCOMPARE(completed.count(), 4); QCOMPARE(folders->items().size(), 2); QVERIFY(!folders->error().isEmpty());
            const auto diffRequests = network.replies["messages.getDiff"].size();
            network.responses["messages.getFolders"].append(R"({"response":{"items":[{"id":1,"name":"Личное","flags":8},{"id":3,"name":"Из VK","flags":0,"included_peer_ids":[88]}]}})");
            messenger.applyLongPoll({QJsonArray{503, 3, "Из VK"}, QJsonArray{10506, 1, 3}});
            QTRY_COMPARE(folders->get("3").value("name").toString(), "Из VK");
            QCOMPARE(network.replies["messages.getDiff"].size(), diffRequests);
            QCOMPARE(network.replies["messages.getFolders"].size(), 2); // one refresh for the batch
            folders->setSelected("3");
            messenger.actions()->cacheReactionAssets(QJsonObject{{"assets", QJsonArray{QJsonObject{{"reaction_id", 7}, {"links", QJsonObject{{"static", "https://example.test/reaction.png"}}}}}}});
        }
        TokenNetwork network; network.response = "#offline";
        Messenger messenger(nullptr, &network); QTRY_VERIFY(messenger.offline());
        auto folders = messenger.folders(); QCOMPARE(folders->items().size(), 2); QCOMPARE(folders->selected(), "3");
        QVERIFY(folders->contains("3", "88"));
        QCOMPARE(messenger.actions()->reactionImage(7), "https://example.test/reaction.png");
        folders->create("Без сети", {"77"}); QVERIFY(!folders->busy()); QVERIFY(!folders->error().isEmpty()); QCOMPARE(folders->items().size(), 2);
    }

    void searchInvalidatesImmediatelyAndRestartsOnScopeChange()
    {
        TokenNetwork network;
        Messenger messenger(nullptr, &network);
        messenger.loginWithToken("test-token"); QTRY_VERIFY(messenger.connected());
        QQmlApplicationEngine engine; engine.rootContext()->setContextProperty("messenger", &messenger);
        engine.load(QUrl::fromLocalFile(QStringLiteral(NATIVE_QML_PATH))); QVERIFY(!engine.rootObjects().isEmpty());
        auto sheet = engine.rootObjects().first()->findChild<QObject *>("actionSheet"); QVERIFY(sheet);
        QVERIFY(QMetaObject::invokeMethod(sheet, "show", Q_ARG(QVariant, "search"), Q_ARG(QVariant, "77"), Q_ARG(QVariant, QStringList()), Q_ARG(QVariant, "")));
        auto query = sheet->findChild<QObject *>("actionSearch"); QVERIFY(query);
        auto scope = sheet->findChild<QObject *>("searchWithinChat"); QVERIFY(scope);
        auto actions = messenger.actions();
        const QByteArray result = R"({"response":{"items":[{"peer_id":77,"conversation_message_id":1,"text":"Found"}]}})";
        query->setProperty("text", "old");
        QVERIFY(QMetaObject::invokeMethod(query, "accepted"));
        QTRY_COMPARE(network.replies["messages.search"].size(), 1);
        query->setProperty("text", "new");
        QVERIFY(QMetaObject::invokeMethod(query, "textEdited"));
        QVERIFY(!actions->busy()); QVERIFY(actions->results().isEmpty());
        network.replies["messages.search"][0]->deliver(result);
        QVERIFY(actions->results().isEmpty()); // rejected even inside the debounce window
        QTRY_COMPARE(network.replies["messages.search"].size(), 2);
        network.replies["messages.search"][1]->deliver(result);
        QTRY_COMPARE(actions->results().size(), 1);
        scope->setProperty("checked", false);
        QVERIFY(QMetaObject::invokeMethod(scope, "toggled"));
        QVERIFY(actions->results().isEmpty());
        QTRY_COMPARE(network.replies["messages.search"].size(), 3);
        QList<QUrlQuery> searches;
        for (const auto &request : network.requests) if (request.first.path().endsWith("messages.search")) searches.append(QUrlQuery(QString::fromUtf8(request.second)));
        QCOMPARE(searches[1].queryItemValue("q"), "new");
        QCOMPARE(searches[1].queryItemValue("peer_id"), "77");
        QVERIFY(!searches[2].hasQueryItem("peer_id"));
        query->setProperty("text", "   ");
        QVERIFY(QMetaObject::invokeMethod(query, "textEdited"));
        network.replies["messages.search"][2]->deliver(result);
        QVERIFY(actions->results().isEmpty()); QVERIFY(!actions->hasMore());
        QTest::qWait(450);
        QCOMPARE(network.replies["messages.search"].size(), 3);
        query->setProperty("text", "closed");
        QVERIFY(QMetaObject::invokeMethod(query, "accepted"));
        QTRY_COMPARE(network.replies["messages.search"].size(), 4);
        QVERIFY(QMetaObject::invokeMethod(sheet, "close"));
        QTRY_VERIFY(!sheet->property("visible").toBool());
        network.replies["messages.search"][3]->deliver(result);
        QVERIFY(actions->results().isEmpty()); QVERIFY(!actions->busy());
        // Direct callers get the same blank-query invalidation.
        actions->search("direct", "77");
        QTRY_COMPARE(network.replies["messages.search"].size(), 5);
        actions->search("", "77");
        network.replies["messages.search"][4]->deliver(result);
        QVERIFY(actions->results().isEmpty()); QVERIFY(!actions->busy());
    }

    void searchPaginationKeepsServerOrderAndDeduplicatesByPeer()
    {
        TokenNetwork network;
        QJsonArray cached;
        for (int i = 1; i <= 100; ++i) cached.append(QJsonObject{{"peer_id", 77}, {"conversation_message_id", i}, {"text", "Needle cached"}});
        network.responses["messages.getDiff"] = {R"({"response":{"server_version":"500","credentials":{"server_lp":"lp.test/im","key":"key","ts":"100"},"conversations_info":[{"conversation":{"peer":{"id":77},"last_conversation_message_id":100},"message":[{"peer_id":77,"conversation_message_id":100,"text":"Needle cached"}]}]}})"};
        network.responses["messages.getDiffContent"] = {QJsonDocument(QJsonObject{{"response", QJsonObject{{"items", QJsonArray{QJsonObject{{"peer_id", 77}, {"messages", cached}}}}}}}).toJson()};
        Messenger messenger(nullptr, &network);
        messenger.loginWithToken("test-token"); QTRY_COMPARE(messenger.conversations()->rowCount(), 1);
        messenger.selectConversation("77", "Search"); QTRY_COMPARE(messenger.messages()->rowCount(), 100);
        auto actions = messenger.actions();
        actions->search("Needle", "");
        QTRY_COMPARE(network.replies["messages.search"].size(), 1);
        QTRY_VERIFY(actions->cachedResults()); QCOMPARE(actions->results().size(), 50);
        QJsonArray firstPage;
        for (int i = 201; i <= 250; ++i) firstPage.append(QJsonObject{{"peer_id", 77}, {"conversation_message_id", i}, {"text", "Needle server"}});
        network.replies["messages.search"][0]->deliver(QJsonDocument(QJsonObject{{"response", QJsonObject{{"items", firstPage}}}}).toJson());
        QVERIFY(!actions->cachedResults()); QVERIFY(actions->hasMore());
        QCOMPARE(actions->results().first().toMap().value("cmid").toString(), "201");
        actions->search("Needle", "", true);
        QTRY_COMPARE(network.replies["messages.search"].size(), 2);
        // A local second page must not be appended while waiting for the server.
        QTest::qWait(100);
        QCOMPARE(actions->results().size(), 50); QVERIFY(!actions->cachedResults());
        network.replies["messages.search"][1]->deliver(R"({"response":{"items":[{"peer_id":77,"conversation_message_id":250,"text":"Updated"},{"peer_id":88,"conversation_message_id":250,"text":"Other chat"},{"peer_id":77,"conversation_message_id":251,"text":"Next"}]}})");
        QTRY_VERIFY(!actions->busy());
        QCOMPARE(actions->results().size(), 52); QVERIFY(!actions->hasMore());
        QCOMPARE(actions->results()[49].toMap().value("text").toString(), "Updated");
        QCOMPARE(actions->results()[50].toMap().value("peer").toString(), "88");
        QCOMPARE(actions->results()[51].toMap().value("cmid").toString(), "251");
        QStringList offsets;
        for (const auto &request : network.requests) if (request.first.path().endsWith("messages.search"))
            offsets.append(QUrlQuery(QString::fromUtf8(request.second)).queryItemValue("offset"));
        QCOMPARE(offsets, (QStringList{"0", "50"}));
    }

    void nativeMessageActionsUseContextAndPreserveAttachments()
    {
        TokenNetwork network;
        network.responses["messages.getDiff"] = {R"({"response":{"server_version":"500","credentials":{"server_lp":"lp.test/im","key":"key","ts":"100"},"conversations_info":[{"conversation":{"peer":{"id":77},"last_conversation_message_id":3},"message":[{"id":900,"conversation_message_id":3,"peer_id":77,"out":1,"text":"Original","attachments":[{"type":"photo","photo":{"owner_id":42,"id":7}}]}]}]}})"};
        network.responses["messages.edit"] = {R"({"response":1})"};
        network.responses["messages.send"] = {R"({"response":901})"};
        Messenger messenger(nullptr, &network);
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("messenger", &messenger);
        engine.load(QUrl::fromLocalFile(QStringLiteral(NATIVE_QML_PATH)));
        QVERIFY(!engine.rootObjects().isEmpty());
        messenger.loginWithToken("test-token");
        QTRY_COMPARE(messenger.conversations()->rowCount(), 1);
        messenger.selectConversation("77", "Собеседник");
        auto sheet = engine.rootObjects().first()->findChild<QObject *>("actionSheet");
        QVERIFY(sheet);
        QVERIFY(QMetaObject::invokeMethod(sheet, "show", Q_ARG(QVariant, "edit"), Q_ARG(QVariant, "77"), Q_ARG(QVariant, QStringList{"3"}), Q_ARG(QVariant, "Original")));
        QVERIFY(sheet->property("visible").toBool());
        auto editor = sheet->findChild<QObject *>("actionEditor");
        QVERIFY(editor); editor->setProperty("text", "Edited");
        QVERIFY(QMetaObject::invokeMethod(sheet, "submit"));
        QTRY_VERIFY(!messenger.actions()->busy());
        bool edited = false;
        for (const auto &request : network.requests) if (request.first.path().endsWith("messages.edit")) {
            const QUrlQuery params(QString::fromUtf8(request.second));
            QCOMPARE(params.queryItemValue("peer_id"), "77");
            QCOMPARE(params.queryItemValue("cmid"), "3");
            QCOMPARE(params.queryItemValue("message"), "Edited");
            QCOMPARE(params.queryItemValue("attachment"), "photo42_7");
            edited = true;
        }
        QVERIFY(edited);
        messenger.setDraftText("88", "Сохранить черновик получателя");
        messenger.actions()->forwardMessages("77", {"3"}, "88", "Комментарий");
        QTRY_VERIFY(std::any_of(network.requests.begin(), network.requests.end(), [](const auto &r) { return r.first.path().endsWith("messages.send"); }));
        for (const auto &request : network.requests) if (request.first.path().endsWith("messages.send")) {
            const QUrlQuery params(QString::fromUtf8(request.second));
            QCOMPARE(params.queryItemValue("peer_id"), "88");
            const auto forward = QJsonDocument::fromJson(params.queryItemValue("forward", QUrl::FullyDecoded).toUtf8()).object();
            QCOMPARE(forward.value("peer_id").toInteger(), 77);
            QCOMPARE(forward.value("conversation_message_ids").toArray(), QJsonArray{3});
        }
        messenger.selectConversation("88", "Получатель");
        QCOMPARE(messenger.draftText(), "Сохранить черновик получателя");
    }

    void longPollEchoBeforeHttpKeepsIdentityAndCannotRecreateOutbox()
    {
        TokenNetwork network;
        network.responses["messages.getDiff"] = {R"({"response":{"server_version":"500","credentials":{"server_lp":"lp.test/im","key":"key","ts":"100"},"conversations_info":[{"conversation":{"peer":{"id":77},"last_conversation_message_id":1},"message":[{"conversation_message_id":1,"peer_id":77,"text":"Before"}]}]}})"};
        {
            Messenger messenger(nullptr, &network);
            messenger.loginWithToken("token");
            QTRY_COMPARE(messenger.conversations()->rowCount(), 1);
            messenger.selectConversation("77", "Chat");
            QTRY_COMPARE(messenger.messages()->rowCount(), 1);
            messenger.send("Outgoing");
            QTRY_COMPARE(network.replies["messages.send"].size(), 1);
            const auto httpReply = network.replies["messages.send"].first();
            const auto localId = messenger.messages()->get(1).value("id").toString();
            QString random;
            for (const auto &request : network.requests) if (request.first.path().endsWith("messages.send")) random = QUrlQuery(QString::fromUtf8(request.second)).queryItemValue("random_id");
            QSignalSpy removed(messenger.messages(), &QAbstractItemModel::rowsRemoved);
            QSignalSpy inserted(messenger.messages(), &QAbstractItemModel::rowsInserted);
            const auto echo = QJsonObject{{"server_version", "501"}, {"conversations_info", QJsonArray{QJsonObject{
                {"conversation", QJsonObject{{"peer", QJsonObject{{"id", 77}}}, {"last_conversation_message_id", 2}}},
                {"message", QJsonArray{QJsonObject{{"id", 902}, {"conversation_message_id", 2}, {"peer_id", 77}, {"out", 1}, {"random_id", random.toLongLong()}, {"text", "Outgoing"}}}}}}}};
            network.responses["messages.getDiff"].append(QJsonDocument(QJsonObject{{"response", echo}}).toJson(QJsonDocument::Compact));
            messenger.reload();
            QTRY_COMPARE(messenger.outgoingCount(), 0);
            QCOMPARE(messenger.messages()->rowCount(), 2);
            QCOMPARE(messenger.messages()->get(1).value("id").toString(), "2");
            QCOMPARE(messenger.messages()->get(1).value("identity").toString(), localId);
            QCOMPARE(removed.count(), 0); QCOMPARE(inserted.count(), 0);
            httpReply->deliver(R"({"response":902})");
            QTest::qWait(50);
        }
        LocalStore store(m_storage.path());
        bool loaded = false;
        QJsonObject outbox;
        store.loadOutgoing("user:42", [&](const auto &value, const auto &error) { QVERIFY(error.isEmpty()); outbox = value; loaded = true; });
        QTRY_VERIFY(loaded); QVERIFY(outbox.value("items").toArray().isEmpty());
    }

    void staleHistoryCannotOverwriteCommittingDiff()
    {
        TokenNetwork network;
        network.responses["messages.getDiff"] = {R"({"response":{"server_version":"500","credentials":{"server_lp":"lp.test/im","key":"key","ts":"100"},"conversations_info":[{"conversation":{"peer":{"id":77},"last_conversation_message_id":1},"message":[{"conversation_message_id":1,"peer_id":77,"text":"Before"}]}]}})"};
        {
            Messenger messenger(nullptr, &network);
            messenger.loginWithToken("token");
            QTRY_COMPARE(messenger.conversations()->rowCount(), 1);
            messenger.selectConversation("77", "Chat");
            QTRY_COMPARE(network.replies["messages.getDiffContent"].size(), 1);
            const auto oldHistory = network.replies["messages.getDiffContent"].first();
            messenger.reload();
            QTRY_COMPARE(network.replies["messages.getDiff"].size(), 2);
            network.replies["messages.getDiff"].last()->deliver(R"({"response":{"server_version":"501","conversations_info":[{"conversation":{"peer":{"id":77},"last_conversation_message_id":1},"message":[{"conversation_message_id":1,"peer_id":77,"text":"Fresh edit"}]}]}})");
            oldHistory->deliver(R"({"response":{"items":[{"peer_id":77,"messages":[{"conversation_message_id":1,"peer_id":77,"text":"Stale response"}]}]}})");
            QTRY_COMPARE(messenger.messages()->get(0).value("text").toString(), "Fresh edit");
        }
        LocalStore store(m_storage.path());
        bool loaded = false;
        QJsonObject history;
        store.loadHistory("user:42", "77", [&](const auto &value, const auto &error) { QVERIFY(error.isEmpty()); history = value; loaded = true; });
        QTRY_VERIFY(loaded);
        QCOMPARE(history.value("messages").toArray().first().toObject().value("text").toString(), "Fresh edit");
    }

    void demoDoesNotDeleteSavedSession()
    {
        savedSession = R"({"access_token":"keep-session","auth_type":"manual_token","user_id":42})";
        Messenger messenger;
        messenger.startDemo();
        QCOMPARE(QJsonDocument::fromJson(savedSession).object().value("access_token").toString(), "keep-session");
    }
    void isolatedSessionAndDemoLinksNeverAccessUserSession()
    {
        savedSession = R"({"access_token":"preserve-user-session"})";
        TokenNetwork network;
        Messenger messenger(nullptr, &network, m_storage.path(), false);
        messenger.startDemo();
        QVERIFY(messenger.openDeepLink(QUrl("vkm://chat/2?message=3")));
        QCOMPARE(messenger.selectedPeer(), "2");
        QVERIFY(!messenger.openDeepLink(QUrl("https://other.test/chat/77")));
        QVERIFY(!messenger.openDeepLink(QUrl("vkm://chat/not-a-peer")));
        QVERIFY(!messenger.openDeepLink(QUrl("vkm://chat/77?message=invalid")));
        messenger.logout();
        QTest::qWait(30);
        QVERIFY(network.requests.isEmpty());
        QCOMPARE(QJsonDocument::fromJson(savedSession).object().value("access_token").toString(), "preserve-user-session");
    }

    void realtimeActivityAndCounters()
    {
        Messenger messenger;
        messenger.startDemo();
        QSignalSpy events(&messenger, &Messenger::realtimeEvent);
        messenger.applyLongPoll({QJsonValue(QJsonArray{8, -1, 7, 100})});
        QCOMPARE(messenger.peerActivity(), QString("В сети"));
        messenger.applyLongPoll({QJsonValue(QJsonArray{61, 1, 1})});
        QCOMPARE(messenger.peerActivity(), QString("Печатает…"));
        messenger.applyLongPoll({QJsonValue(QJsonArray{64, 1, QJsonArray{5}, 1, 100})});
        QCOMPARE(messenger.peerActivity(), QString("Записывает голосовое сообщение…"));
        messenger.applyLongPoll({QJsonValue(QJsonArray{4, 10, 1, 1})});
        QCOMPARE(messenger.peerActivity(), QString("В сети"));
        messenger.applyLongPoll({QJsonArray{9, -1, 0, 101}, QJsonArray{80, 7}});
        QCOMPARE(messenger.peerActivity(), QString("Не в сети"));
        QCOMPARE(messenger.property("unreadCount").toInt(), 7);
        messenger.applyLongPoll({QJsonArray{99999, "extension"}, QJsonArray{4}, QJsonObject{}});
        QCOMPARE(events.count(), 9);
        messenger.logout();
        QCOMPARE(messenger.property("unreadCount").toInt(), 0);
    }

    void typingExpires()
    {
        Messenger messenger;
        messenger.startDemo();
        messenger.applyLongPoll({QJsonValue(QJsonArray{61, 1, 1})});
        QCOMPARE(messenger.peerActivity(), QString("Печатает…"));
        QTRY_COMPARE_WITH_TIMEOUT(messenger.peerActivity(), QString("Демонстрационная переписка"), 8000);
    }
};

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    app.setOrganizationName("VKTests");
    app.setApplicationName("VKTests");
    QStandardPaths::setTestModeEnabled(true);
    QQuickStyle::setStyle("Basic");
    qmlRegisterType<LottieSticker>("VK", 1, 0, "LottieSticker");
    MessengerTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "MessengerTest.moc"
