#include "Messenger.h"
#include "models/TimelinePresentation.h"
#include "platform/DeepLinkHandler.h"
#include <QClipboard>
#include "Keychain.h"
#include "LottieSticker.h"
#include "media/AvatarImage.h"
#include "media/VideoFrameOutput.h"
#include <QGuiApplication>
#include <QDesktopServices>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QAccessible>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QWheelEvent>
#include <QSignalSpy>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QUrlQuery>
#include <QJSValue>
#include <QBuffer>
#include <QAbstractItemModelTester>
#include <QElapsedTimer>
#include <QImageReader>
#include <QInputMethodEvent>
#include <QRandomGenerator>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QVideoSink>
#include <QVideoFrame>
#include <QPainter>
#include <QStandardPaths>
#include <QDir>
#include <QTemporaryDir>
#include <QtTest>
#include <cstring>
#include <vector>

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

class FakeNotificationBackend : public NotificationBackend {
public:
    Activation activation;
    QString authorization = "granted";
    QList<DesktopNotice> shown;
    int requests = 0, cleared = 0;
    void setActivation(Activation callback) override { activation = std::move(callback); }
    void permission(bool request, Completion complete) override { requests += request; complete(authorization); }
    void deliver(const DesktopNotice &notice, Completion complete) override { shown.append(notice); complete({}); }
    void clear() override { ++cleared; }
    void openSettings() override {}
};

static QUrl startApiLogin(Messenger &messenger)
{
    LoginBrowserCapture browser;
    QDesktopServices::setUrlHandler("https", &browser, "capture");
    messenger.loginApi();
    QDesktopServices::unsetUrlHandler("https");
    return browser.url;
}

static QString apiLoginReturn(const QUrl &authorize, const QString &parameters = "access_token=fixture%2Btoken%2F%252B%3D%3D&user_id=42&expires_in=3600")
{
    return "https://oauth.vk.com/blank.html#state=" + QUrlQuery(authorize).queryItemValue("state") + "&" + parameters;
}

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

    void notificationsIgnoreInitialHistoryDuplicatesMutedAndReadMessages()
    {
        TokenNetwork network;
        const auto snapshot = [](int version, int cmid, QString text, bool read = false, bool silent = false) {
            QJsonArray infos;
            for (int peer : {77, 88, 99}) {
                QJsonObject conversation{{"peer", QJsonObject{{"id", peer}}}, {"last_conversation_message_id", cmid}, {"unread_count", read ? 0 : 1}, {"in_read_cmid", read ? cmid : 0}};
                if (peer == 99) conversation.insert("push_settings", QJsonObject{{"disabled_forever", true}});
                QJsonObject message{{"peer_id", peer}, {"conversation_message_id", cmid}, {"text", text}, {"out", peer == 88 ? 1 : 0}, {"is_silent", silent}};
                infos.append(QJsonObject{{"conversation", conversation}, {"message", QJsonArray{message}}});
            }
            return QJsonDocument(QJsonObject{{"response", QJsonObject{{"server_version", QString::number(version)}, {"credentials", QJsonObject{{"server_lp", "lp.test/im"}, {"key", "key"}, {"ts", "100"}}}, {"conversations_info", infos}, {"profiles", QJsonArray{QJsonObject{{"id", 77}, {"first_name", "Анна"}}}}}}}).toJson();
        };
        network.responses["messages.getDiff"] = {snapshot(500, 1, "Начальная история")};
        Messenger messenger(nullptr, &network);
        QSignalSpy notifications(&messenger, &Messenger::notificationRequested);
        messenger.loginWithToken("test-token"); QTRY_COMPARE(messenger.conversations()->rowCount(), 3); QCOMPARE(notifications.count(), 0);
        const auto preview = [&] { return messenger.conversations()->get(messenger.conversations()->find("77")).value("preview").toString(); };
        network.responses["messages.getDiff"] = {snapshot(501, 2, "Новое сообщение")}; messenger.reload();
        QTRY_COMPARE(preview(), QString("Новое сообщение")); QCOMPARE(notifications.count(), 1);
        QCOMPARE(notifications.first()[1].toString(), "77"); QCOMPARE(notifications.first()[2].toString(), "2"); QCOMPARE(notifications.first()[3].toString(), "Анна");
        network.responses["messages.getDiff"] = {snapshot(502, 2, "Редактирование")}; messenger.reload();
        QTRY_COMPARE(preview(), QString("Редактирование")); QCOMPARE(notifications.count(), 1);
        network.responses["messages.getDiff"] = {snapshot(503, 3, "Прочитано", true)}; messenger.reload();
        QTRY_COMPARE(preview(), QString("Прочитано")); QCOMPARE(notifications.count(), 1);
        network.responses["messages.getDiff"] = {snapshot(504, 4, "Без уведомления", false, true)}; messenger.reload();
        QTRY_COMPARE(preview(), QString("Без уведомления")); QCOMPARE(notifications.count(), 1);
        network.responses["messages.getDiff"] = {snapshot(505, 5, "Следующее входящее")}; messenger.reload();
        QTRY_COMPARE(preview(), QString("Следующее входящее")); QCOMPARE(notifications.count(), 2);
        QVERIFY(!messenger.openNotification("other-account", "77", "5"));
    }

    void desktopNotificationsRespectFocusPrivacyAndActivation()
    {
        TokenNetwork network;
        network.responses["messages.getDiff"] = {R"({"response":{"server_version":"500","credentials":{"server_lp":"lp.test/im","key":"key","ts":"100"},"conversations_info":[{"conversation":{"peer":{"id":77},"last_conversation_message_id":1},"message":[{"conversation_message_id":1,"peer_id":77,"text":"Fixture"}]}]}})"};
        Messenger messenger(nullptr, &network); messenger.loginWithToken("test-token"); QTRY_COMPARE(messenger.conversations()->rowCount(), 1);
        messenger.selectConversation("77", "Анна");
        auto backend = std::make_unique<FakeNotificationBackend>(); auto fake = backend.get();
        DesktopNotifications notifications(messenger, std::move(backend));
        notifications.setEnabled(true); QCOMPARE(fake->requests, 1); QCOMPARE(notifications.permission(), "granted");
        QQuickWindow window; window.show(); window.requestActivate(); QTRY_VERIFY(window.isActive()); notifications.setWindow(&window);
        const auto account = messenger.notificationAccount();
        const auto send = [&](const QString &peer) { messenger.notificationRequested(account, peer, "2", "Анна", "Личный текст"); };
        send("77"); QCOMPARE(fake->shown.size(), 0);
        send("88"); QCOMPARE(fake->shown.size(), 1); QCOMPARE(fake->shown.last().body, "Личный текст");
        window.hide(); send("77"); QCOMPARE(fake->shown.size(), 2);
        notifications.setPreview(false); notifications.setSound(false); send("88");
        QCOMPARE(fake->shown.size(), 3); QCOMPARE(fake->shown.last().title, "VK"); QCOMPARE(fake->shown.last().body, "Новое сообщение"); QVERIFY(!fake->shown.last().sound);
        notifications.setEnabled(false); send("88"); QCOMPARE(fake->shown.size(), 3); QVERIFY(fake->cleared > 0);
        fake->authorization = "denied"; notifications.setEnabled(true); send("88"); QCOMPARE(fake->shown.size(), 3);
        fake->authorization = "granted"; notifications.refreshPermission();
        fake->activation("other-account", "88", "2"); QCOMPARE(messenger.selectedPeer(), "77");
        fake->activation(account, "88", "2"); QCOMPARE(messenger.selectedPeer(), "88"); QTRY_VERIFY(window.isVisible());
        notifications.setEnabled(false);
    }

    void notificationSettingsControlPermissionAndPrivacy()
    {
        auto backend = std::make_unique<FakeNotificationBackend>(); auto fake = backend.get();
        Messenger messenger(nullptr, nullptr, {}, false, std::move(backend)); messenger.startDemo();
        QQmlApplicationEngine engine; engine.rootContext()->setContextProperty("messenger", &messenger);
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        engine.load(QUrl::fromLocalFile(QStringLiteral(NATIVE_QML_PATH))); QVERIFY(!engine.rootObjects().isEmpty());
        auto window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window); window->resize(780, 560);
        auto dialog = window->findChild<QObject *>("notificationSettings"); QVERIFY(dialog);
        QVERIFY(QMetaObject::invokeMethod(dialog, "open")); QTRY_VERIFY(dialog->property("opened").toBool());
        const auto click = [window](QQuickItem *item) { QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint()); };
        auto enabled = dialog->findChild<QQuickItem *>("desktopNotificationsEnabled"); QVERIFY(enabled);
        auto preview = dialog->findChild<QQuickItem *>("desktopNotificationsPreview"); QVERIFY(preview);
        auto sound = dialog->findChild<QQuickItem *>("desktopNotificationsSound"); QVERIFY(sound);
        QVERIFY(!enabled->property("checked").toBool()); click(enabled); QTRY_VERIFY(messenger.notifications()->enabled()); QCOMPARE(fake->requests, 1);
        click(preview); QTRY_VERIFY(!messenger.notifications()->preview());
        click(sound); QTRY_VERIFY(!messenger.notifications()->sound());
        auto status = dialog->findChild<QQuickItem *>("desktopNotificationPermission"); QVERIFY(status);
        QVERIFY(status->property("text").toString().contains("разрешены"));
        for (bool dark : {false, true}) {
            messenger.setDarkTheme(dark); QTest::qWait(50);
            QVERIFY(dialog->property("height").toReal() < window->height() - 30);
            if (qEnvironmentVariableIsSet("VKM_NOTIFICATIONS_SCREENSHOT")) QVERIFY(window->grabWindow().save(qEnvironmentVariable("VKM_NOTIFICATIONS_SCREENSHOT") + (dark ? "-dark.png" : "-light.png")));
        }
        click(enabled); QTRY_VERIFY(!messenger.notifications()->enabled()); QVERIFY(fake->cleared > 0);
        QVERIFY(QMetaObject::invokeMethod(dialog, "close")); QCOMPARE(warnings.count(), 0);
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
        QVERIFY(!field->property("visible").toBool());
        auto manual = root->findChild<QObject *>("vkIdManualReturn"); QVERIFY(manual);
        manual->setProperty("checked", true);
        QVERIFY(field->property("visible").toBool());
        QCOMPARE(field->property("echoMode").toInt(), 2);
        QVERIFY(!submit->property("enabled").toBool());
        field->setProperty("text", "https://153-51-114-47.sslip.io/callback?code=test&state=wrong&device_id=test");
        QVERIFY(submit->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(submit, "clicked"));
        QVERIFY(messenger.authorizing());
        QVERIFY(messenger.browserLoginPending());
        QVERIFY(!messenger.connected());
        QVERIFY(!messenger.error().isEmpty());
        QVERIFY(dialog->property("visible").toBool());
        QCOMPARE(field->property("text").toString(), QString());
        QVERIFY(QMetaObject::invokeMethod(dialog, "reject"));
        QVERIFY(!messenger.authorizing()); QVERIFY(!messenger.browserLoginPending());
    }

    void apiLoginVerifiesIdentityBeforeSavingAndAcceptsCallbackOnce()
    {
        TokenNetwork network; network.response.clear();
        Messenger messenger(nullptr, &network);
        QQmlApplicationEngine engine; engine.rootContext()->setContextProperty("messenger", &messenger);
        engine.load(QUrl::fromLocalFile(QStringLiteral(NATIVE_QML_PATH))); QVERIFY(!engine.rootObjects().isEmpty());
        auto root = engine.rootObjects().first();
        auto button = root->findChild<QObject *>("vkApiLoginButton"); QVERIFY(button);
        QVERIFY(root->findChild<QObject *>("settingsVkApiLoginButton"));
        LoginBrowserCapture browser; QDesktopServices::setUrlHandler("https", &browser, "capture");
        const bool clicked = QMetaObject::invokeMethod(button, "clicked");
        QDesktopServices::unsetUrlHandler("https"); QVERIFY(clicked);
        QVERIFY(messenger.browserLoginPending()); QVERIFY(messenger.browserLoginUsesToken());
        QCOMPARE(browser.url.host(), "oauth.vk.com"); QCOMPARE(browser.url.path(), "/authorize");
        const QUrlQuery query(browser.url);
        QCOMPARE(query.queryItemValue("client_id"), "54850913");
        QCOMPARE(query.queryItemValue("response_type"), "token");
        QCOMPARE(query.queryItemValue("scope"), "messages");
        QCOMPARE(query.queryItemValue("redirect_uri", QUrl::FullyDecoded), "https://oauth.vk.com/blank.html");
        QVERIFY(!query.queryItemValue("state").isEmpty());
        QVERIFY(!query.hasQueryItem("client_secret")); QVERIFY(!query.hasQueryItem("code_challenge"));
        auto dialog = root->findChild<QObject *>("vkIdReturnDialog"); QVERIFY(dialog);
        auto field = root->findChild<QObject *>("vkIdReturnInput"); QVERIFY(field);
        auto submit = root->findChild<QObject *>("vkIdReturnSubmit"); QVERIFY(submit);
        QTRY_VERIFY(dialog->property("visible").toBool()); QVERIFY(field->property("visible").toBool());
        QCOMPARE(field->property("echoMode").toInt(), 2);
        const auto callback = apiLoginReturn(browser.url);
        field->setProperty("text", callback);
        QVERIFY(QMetaObject::invokeMethod(submit, "clicked"));
        QCOMPARE(field->property("text").toString(), QString());
        QTRY_COMPARE(network.replies["users.get"].size(), 1);
        QVERIFY(!messenger.connected()); QVERIFY(savedSession.isEmpty());
        QVERIFY(messenger.authorizing()); QVERIFY(!messenger.browserLoginPending());
        QVERIFY(dialog->property("visible").toBool());
        messenger.completeLogin(callback);
        QCOMPARE(network.replies["users.get"].size(), 1);
        QCOMPARE(QUrlQuery(QString::fromUtf8(network.requests.first().second)).queryItemValue("access_token", QUrl::FullyDecoded), "fixture+token/%2B==");
        network.replies["users.get"][0]->deliver(R"({"response":[{"id":42}]})");
        QTRY_VERIFY(messenger.connected()); QTRY_VERIFY(!dialog->property("visible").toBool());
        const auto saved = QJsonDocument::fromJson(savedSession).object();
        QCOMPARE(saved.value("auth_type").toString(), "oauth_api");
        QCOMPARE(saved.value("client_id").toString(), "54850913");
        QCOMPARE(saved.value("user_id").toInteger(), 42);
        QCOMPARE(saved.value("access_token").toString(), "fixture+token/%2B==");
        QVERIFY(saved.value("expires_at").toInteger() > QDateTime::currentSecsSinceEpoch() + 3500);
        QVERIFY(!saved.contains("refresh_token"));
        messenger.completeLogin(callback);
        QCOMPARE(network.replies["users.get"].size(), 1);
        QVERIFY(network.replies["auth"].isEmpty());
    }

    void apiLoginRejectsMalformedCallbackWithoutLosingAttempt_data()
    {
        QTest::addColumn<QString>("address");
        const QString base = "https://oauth.vk.com/blank.html";
        const QString fragment = "#state=STATE&access_token=fixture&user_id=42&expires_in=3600";
        QTest::newRow("wrong-host") << "https://example.test/blank.html" + fragment;
        QTest::newRow("http") << "http://oauth.vk.com/blank.html" + fragment;
        QTest::newRow("userinfo") << "https://user@oauth.vk.com/blank.html" + fragment;
        QTest::newRow("port") << "https://oauth.vk.com:443/blank.html" + fragment;
        QTest::newRow("path") << "https://oauth.vk.com/other" + fragment;
        QTest::newRow("query") << base + "?token=fixture" + fragment;
        QTest::newRow("no-fragment") << base + "?state=STATE&access_token=fixture&user_id=42&expires_in=3600";
        QTest::newRow("wrong-state") << base + "#state=other&access_token=fixture&user_id=42&expires_in=3600";
        QTest::newRow("duplicate-state") << base + fragment + "&state=STATE";
        QTest::newRow("duplicate-token") << base + fragment + "&access_token=other";
        QTest::newRow("duplicate-user") << base + fragment + "&user_id=42";
        QTest::newRow("duplicate-expiry") << base + fragment + "&expires_in=3600";
        QTest::newRow("mixed-error") << base + fragment + "&error=access_denied";
        QTest::newRow("missing-token") << base + "#state=STATE&user_id=42&expires_in=3600";
        QTest::newRow("token-control") << base + "#state=STATE&access_token=fixture%00&user_id=42&expires_in=3600";
        QTest::newRow("bad-user") << base + "#state=STATE&access_token=fixture&user_id=-1&expires_in=3600";
        QTest::newRow("missing-expiry") << base + "#state=STATE&access_token=fixture&user_id=42";
        QTest::newRow("negative-expiry") << base + "#state=STATE&access_token=fixture&user_id=42&expires_in=-1";
        QTest::newRow("overflow-expiry") << base + "#state=STATE&access_token=fixture&user_id=42&expires_in=9223372036854775807";
    }

    void apiLoginRejectsMalformedCallbackWithoutLosingAttempt()
    {
        QFETCH(QString, address);
        TokenNetwork network; Messenger messenger(nullptr, &network);
        const auto authorize = startApiLogin(messenger);
        address.replace("STATE", QUrlQuery(authorize).queryItemValue("state"));
        messenger.completeLogin(address);
        QVERIFY(messenger.browserLoginPending()); QVERIFY(!messenger.connected());
        QVERIFY(!messenger.error().isEmpty()); QVERIFY(network.requests.isEmpty()); QVERIFY(savedSession.isEmpty());
        messenger.completeLogin(apiLoginReturn(authorize));
        QTRY_VERIFY(messenger.connected());
    }

    void apiLoginCancellationExpiryDenialAndSwitchingFlows()
    {
        TokenNetwork network; network.response.clear(); Messenger messenger(nullptr, &network);
        auto callback = apiLoginReturn(startApiLogin(messenger));
        messenger.logout(); messenger.completeLogin(callback);
        QVERIFY(!messenger.connected()); QVERIFY(!messenger.authorizing()); QVERIFY(network.requests.isEmpty());
        callback = apiLoginReturn(startApiLogin(messenger));
        auto oauth = messenger.findChild<OAuth *>(); QVERIFY(oauth);
        QVERIFY(QMetaObject::invokeMethod(oauth, "expire"));
        messenger.completeLogin(callback); QVERIFY(!messenger.authorizing()); QVERIFY(network.requests.isEmpty());
        const auto denied = startApiLogin(messenger);
        messenger.completeLogin(apiLoginReturn(denied, "error=access_denied&error_description=secret-that-must-not-be-shown"));
        QVERIFY(!messenger.authorizing()); QVERIFY(!messenger.error().contains("secret-that")); QVERIFY(savedSession.isEmpty());
        callback = apiLoginReturn(startApiLogin(messenger));
        LoginBrowserCapture browser; QDesktopServices::setUrlHandler("https", &browser, "capture");
        messenger.login(); QDesktopServices::unsetUrlHandler("https");
        messenger.completeLogin(callback); QVERIFY(messenger.browserLoginPending()); QVERIFY(!messenger.browserLoginUsesToken());
        const auto pkceState = QUrlQuery(browser.url).queryItemValue("state");
        const auto current = startApiLogin(messenger);
        messenger.openDeepLink(QUrl("vkm://auth/callback?code=fixture&device_id=fixture&state=" + pkceState));
        QVERIFY(messenger.browserLoginUsesToken()); QVERIFY(network.requests.isEmpty());
        messenger.completeLogin(callback); QVERIFY(messenger.browserLoginPending()); QVERIFY(network.requests.isEmpty());
        callback = apiLoginReturn(current);
        messenger.completeLogin(callback); QTRY_COMPARE(network.replies["users.get"].size(), 1);
        auto pending = network.replies["users.get"][0]; messenger.logout();
        if (pending) pending->deliver(R"({"response":[{"id":42}]})");
        QVERIFY(!messenger.connected()); QVERIFY(savedSession.isEmpty());
        Messenger restarted(nullptr, &network, m_storage.path(), false);
        restarted.completeLogin(callback); QVERIFY(!restarted.authorizing()); QVERIFY(!restarted.connected());
        QVERIFY(network.replies["auth"].isEmpty());
    }

    void apiLoginDoesNotSaveUnverifiedIdentity_data()
    {
        QTest::addColumn<QByteArray>("response");
        QTest::newRow("different-user") << QByteArray(R"({"response":[{"id":99}]})");
        QTest::newRow("invalid-token") << QByteArray(R"({"error":{"error_code":5,"error_msg":"fixture-token"}})");
        QTest::newRow("network-error") << QByteArray("#offline");
    }

    void apiLoginDoesNotSaveUnverifiedIdentity()
    {
        QFETCH(QByteArray, response);
        TokenNetwork network; network.response = response; Messenger messenger(nullptr, &network);
        messenger.completeLogin(apiLoginReturn(startApiLogin(messenger)));
        QTRY_VERIFY(!messenger.authorizing()); QVERIFY(!messenger.connected()); QVERIFY(savedSession.isEmpty());
        QVERIFY(!messenger.error().isEmpty()); QVERIFY(!messenger.error().contains("fixture-token"));
        QVERIFY(network.replies["auth"].isEmpty()); QVERIFY(network.replies["messages.getDiff"].isEmpty());
    }

    void apiLoginRestoresWithoutVkIdRefreshAndHonorsExpiry_data()
    {
        QTest::addColumn<qint64>("remaining");
        QTest::newRow("valid") << qint64(3600);
        QTest::newRow("no-expiry") << qint64(0);
        QTest::newRow("expired") << qint64(-1);
        QTest::newRow("expires-while-open") << qint64(3);
    }

    void apiLoginRestoresWithoutVkIdRefreshAndHonorsExpiry()
    {
        QFETCH(qint64, remaining);
        const auto expires = remaining == 0 ? 0 : QDateTime::currentSecsSinceEpoch() + remaining;
        savedSession = QJsonDocument(QJsonObject{{"auth_type", "oauth_api"}, {"access_token", "fixture-token"},
            {"user_id", 42}, {"client_id", "54850913"}, {"expires_at", expires}}).toJson();
        TokenNetwork network; Messenger messenger(nullptr, &network);
        if (remaining < 0) {
            QTRY_VERIFY(!messenger.error().isEmpty());
            QVERIFY(!messenger.connected()); QVERIFY(savedSession.isEmpty()); QVERIFY(network.requests.isEmpty());
        } else {
            QTRY_VERIFY(messenger.connected());
            QCOMPARE(network.replies["users.get"].size(), 1);
            const auto saved = QJsonDocument::fromJson(savedSession).object();
            QCOMPARE(saved.value("auth_type").toString(), "oauth_api");
            QCOMPARE(saved.value("expires_at").toInteger(), expires);
            if (remaining == 3) {
                QTRY_VERIFY_WITH_TIMEOUT(!messenger.connected(), 5000);
                QVERIFY(savedSession.isEmpty()); QVERIFY(messenger.error().contains("истёк"));
            }
        }
        QVERIFY(network.replies["auth"].isEmpty());
    }

    void appCallbackCompletesOnceThroughFileOpenEventAndKeepsHttpsRedirect()
    {
        TokenNetwork network; Messenger messenger(nullptr, &network);
        QQmlApplicationEngine engine; engine.rootContext()->setContextProperty("messenger", &messenger);
        engine.load(QUrl::fromLocalFile(QStringLiteral(NATIVE_QML_PATH))); QVERIFY(!engine.rootObjects().isEmpty());
        auto window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        DeepLinkHandler handler(messenger); handler.setWindow(window);
        QObject receiver; receiver.installEventFilter(&handler);
        LoginBrowserCapture browser; QDesktopServices::setUrlHandler("https", &browser, "capture");
        messenger.login(); QDesktopServices::unsetUrlHandler("https");
        const QUrlQuery authorize(browser.url); const auto state = authorize.queryItemValue("state");
        QVERIFY(!state.isEmpty());
        const QUrl callback("vkm://auth/callback?code=fixture%2Bcode%2F%252B%3D%3D&device_id=device%2B%25%26%3D&state=" + state);
        window->hide(); QVERIFY(!window->isVisible());
        QFileOpenEvent event(callback); QVERIFY(QCoreApplication::sendEvent(&receiver, &event));
        QTRY_VERIFY(window->isVisible());
        QTRY_COMPARE(network.replies["auth"].size(), 1);
        QVERIFY(messenger.authorizing()); QVERIFY(!messenger.browserLoginPending());
        auto dialog = window->findChild<QObject *>("vkIdReturnDialog"); QVERIFY(dialog);
        QVERIFY(dialog->property("visible").toBool()); // progress stays visible
        const auto request = network.requests.first();
        QCOMPARE(request.first.toString(), "https://id.vk.ru/oauth2/auth");
        const QUrlQuery body(QString::fromUtf8(request.second));
        QCOMPARE(body.queryItemValue("code", QUrl::FullyDecoded), "fixture+code/%2B==");
        QCOMPARE(body.queryItemValue("device_id", QUrl::FullyDecoded), "device+%&=");
        QCOMPARE(body.queryItemValue("state"), state);
        QCOMPARE(body.queryItemValue("redirect_uri", QUrl::FullyDecoded), authorize.queryItemValue("redirect_uri", QUrl::FullyDecoded));
        QCOMPARE(OAuth::challenge(body.queryItemValue("code_verifier")), authorize.queryItemValue("code_challenge"));
        QFileOpenEvent duplicate(callback); QVERIFY(QCoreApplication::sendEvent(&receiver, &duplicate));
        QUrl manual(authorize.queryItemValue("redirect_uri", QUrl::FullyDecoded)); manual.setQuery(callback.query(QUrl::FullyEncoded), QUrl::StrictMode);
        messenger.completeLogin(manual.toString(QUrl::FullyEncoded));
        QVERIFY(messenger.authorizing()); QVERIFY(messenger.error().isEmpty());
        QCOMPARE(network.replies["auth"].size(), 1);
        network.replies["auth"][0]->deliver(QJsonDocument(QJsonObject{{"access_token", "fixture-token"}, {"refresh_token", "fixture-refresh"}, {"user_id", 42}, {"state", state}}).toJson());
        QTRY_VERIFY(messenger.connected()); QVERIFY(!messenger.authorizing());
        QTRY_VERIFY(!dialog->property("visible").toBool());
        QVERIFY(messenger.openDeepLink(callback));
        QVERIFY(messenger.error().isEmpty()); QCOMPARE(network.replies["auth"].size(), 1);
        QFileOpenEvent unrelated(QUrl("https://unrelated.test/")); QVERIFY(!QCoreApplication::sendEvent(&receiver, &unrelated));
    }

    void invalidAppCallbacksKeepTheCurrentAttemptAndCanBeRetried()
    {
        TokenNetwork network; Messenger messenger(nullptr, &network);
        LoginBrowserCapture browser; QDesktopServices::setUrlHandler("https", &browser, "capture");
        messenger.login(); QDesktopServices::unsetUrlHandler("https");
        const auto state = QUrlQuery(browser.url).queryItemValue("state");
        const QString tail = "?state=" + state + "&code=fixture&device_id=device";
        for (const auto &address : QStringList{"vkm://user@auth/callback" + tail, "vkm://auth:443/callback" + tail,
            "vkm://auth/other" + tail, "vkm://auth/callback" + tail + "#fragment",
            "vkm://auth/callback" + tail + "&state=other", "vkm://auth/callback" + tail + "&code=other",
            "vkm://auth/callback" + tail + "&device_id=other", "vkm://auth/callback?state=" + state + "&code=fixture",
            "vkm://auth/callback?state=wrong&code=fixture&device_id=device"}) {
            QVERIFY(messenger.openDeepLink(QUrl(address)));
            QVERIFY(messenger.authorizing()); QVERIFY(messenger.browserLoginPending());
            QVERIFY(!messenger.error().isEmpty()); QVERIFY(network.requests.isEmpty());
        }
        QVERIFY(messenger.openDeepLink(QUrl("vkm://auth/callback" + tail)));
        QVERIFY(messenger.authorizing()); QVERIFY(!messenger.browserLoginPending());
        QVERIFY(messenger.error().isEmpty()); QTRY_COMPARE(network.replies["auth"].size(), 1);
        network.replies["auth"][0]->deliver("#offline");
        QTRY_VERIFY(!messenger.authorizing()); QVERIFY(!messenger.connected());
        QVERIFY(messenger.error().contains("не удалось получить токен"));
        QVERIFY(messenger.openDeepLink(QUrl("vkm://auth/callback" + tail)));
        QCOMPARE(network.replies["auth"].size(), 1);
    }

    void cancelledExpiredAndSupersededAppCallbacksCannotSignIn()
    {
        TokenNetwork network; Messenger messenger(nullptr, &network);
        LoginBrowserCapture browser;
        const auto start = [&] {
            QDesktopServices::setUrlHandler("https", &browser, "capture"); messenger.login(); QDesktopServices::unsetUrlHandler("https");
            return QUrl("vkm://auth/callback?state=" + QUrlQuery(browser.url).queryItemValue("state") + "&code=fixture&device_id=device");
        };
        const auto old = start(); const auto current = start();
        QVERIFY(messenger.openDeepLink(old));
        QVERIFY(messenger.authorizing()); QVERIFY(messenger.browserLoginPending()); QVERIFY(network.requests.isEmpty());
        auto oauth = messenger.findChild<OAuth *>(); QVERIFY(oauth);
        QVERIFY(QMetaObject::invokeMethod(oauth, "expire"));
        QVERIFY(!messenger.authorizing()); QVERIFY(messenger.error().contains("истекло"));
        QVERIFY(messenger.openDeepLink(current)); QVERIFY(network.requests.isEmpty());
        QVERIFY(messenger.error().contains("Начните вход заново"));
        const auto cancelled = start(); messenger.logout();
        QVERIFY(messenger.openDeepLink(cancelled)); QVERIFY(!messenger.authorizing()); QVERIFY(network.requests.isEmpty());
        const auto denied = start();
        QUrl denial(denied); denial.setQuery("state=" + QUrlQuery(denied).queryItemValue("state") + "&error=access_denied");
        QVERIFY(messenger.openDeepLink(denial));
        QVERIFY(!messenger.authorizing()); QVERIFY(messenger.error().contains("отклонён")); QVERIFY(network.requests.isEmpty());
        const auto pending = start(); QVERIFY(messenger.openDeepLink(pending));
        QTRY_COMPARE(network.replies["auth"].size(), 1);
        auto stale = network.replies["auth"][0]; messenger.logout();
        if (stale) stale->deliver(R"({"access_token":"stale-token","user_id":42})");
        QVERIFY(!messenger.connected()); QVERIFY(!messenger.authorizing()); QVERIFY(savedSession.isEmpty());
        // A fresh process has neither state nor verifier from the old browser tab.
        Messenger restarted(nullptr, &network, m_storage.path(), false);
        QVERIFY(restarted.openDeepLink(pending)); QVERIFY(!restarted.authorizing()); QVERIFY(!restarted.connected());
        QVERIFY(restarted.error().contains("Начните вход заново"));
    }

    void profileTypeFailureFallsBackAndReportsMethodAndCode_data()
    {
        QTest::addColumn<bool>("publicAllowed");
        QTest::newRow("web-profile-unavailable-public-allowed") << true;
        QTest::newRow("public-profile-also-unavailable") << false;
    }
    void profileTypeFailureFallsBackAndReportsMethodAndCode()
    {
        QFETCH(bool, publicAllowed);
        // Deliberately synthetic code: classification must not assume an
        // undocumented mapping between this message and a fixed error number.
        const QByteArray denied = R"({"error":{"error_code":999001,"error_msg":"Method is not available for this profile type: method is unavailable with current profile type","request_params":[{"key":"access_token","value":"do-not-display"}]}})";
        TokenNetwork network;
        network.responses["messages.getDiff"] = {denied};
        network.responses["messages.getConversations"] = {publicAllowed ? QByteArray(R"({"response":{"count":0,"items":[]}})") : denied};
        Messenger messenger(nullptr, &network); messenger.loginWithToken("fixture-token");
        QTRY_COMPARE(network.replies["messages.getConversations"].size(), 1);
        if (publicAllowed) {
            QTRY_COMPARE(network.replies["messages.getLongPollServer"].size(), 1);
            QVERIFY(messenger.error().isEmpty());
        } else {
            QTRY_VERIFY(messenger.error().contains("типа профиля"));
            QVERIFY(messenger.error().contains("messages.getConversations")); QVERIFY(messenger.error().contains("999001"));
            QVERIFY(!messenger.error().contains("fixture-token")); QVERIFY(!messenger.error().contains("do-not-display"));
            QVERIFY(!messenger.canWrite());
            messenger.selectConversation("77", "Cached conversation");
            messenger.reload(); messenger.typing(); QTest::qWait(3200);
            QCOMPARE(network.replies["messages.getHistory"].size(), 0);
            QCOMPARE(network.replies["messages.setActivity"].size(), 0);
            QCOMPARE(network.replies["messages.getLongPollServer"].size(), 0);
        }
        QVERIFY(messenger.connected()); QVERIFY(!savedSession.isEmpty());
        QCOMPARE(network.replies["messages.getDiff"].size(), 1);
        QCOMPARE(network.replies["messages.getConversations"].size(), 1);
    }

    void apiDiagnosticsDoNotExposeServerEchoedCredentials()
    {
        TokenNetwork network;
        const QJsonObject error{{"error_code", 100},
            {"error_msg", "Invalid request: access_token=fixture-secret https://callback.test/?code=fixture-code"},
            {"request_params", QJsonArray{QJsonObject{{"key", "access_token"}, {"value", "fixture-secret"}}}}};
        network.responses["messages.getHistory"] = {QJsonDocument(QJsonObject{{"error", error}}).toJson()};
        VkApiClient api(&network); api.setToken("fixture-secret");
        ApiResult result; bool done = false;
        api.request("messages.getHistory", {}, [&](const ApiResult &value) { result = value; done = true; });
        QTRY_VERIFY(done);
        QVERIFY(result.error.contains("messages.getHistory")); QVERIFY(result.error.contains("100"));
        QVERIFY(!result.error.contains("fixture-secret")); QVERIFY(!result.error.contains("fixture-code"));
        QVERIFY(!result.error.contains("callback.test"));
        QVERIFY(!result.methodUnavailable());
    }

    void vkIdCallbackFallsBackToPublicMessagesApi()
    {
        TokenNetwork network;
        network.responses["messages.getDiff"] = {R"({"error":{"error_code":3,"error_msg":"Unknow method passed"}})"};
        const QByteArray conversations = R"({"response":{"count":1,"items":[{"conversation":{"peer":{"id":77},"unread_count":1},"last_message":{"id":9300,"conversation_message_id":3,"peer_id":77,"from_id":77,"text":"Last"}}],"profiles":[{"id":77,"first_name":"Тестовый","last_name":"Собеседник"}]}})";
        network.responses["messages.getConversations"] = {conversations, conversations};
        network.responses["messages.getLongPollServer"] = {R"({"response":{"server":"lp.test/im","key":"fixture-key","ts":"100"}})"};
        const QByteArray history = R"({"response":{"items":[{"id":9300,"conversation_message_id":3,"from_id":77,"text":"Last"},{"id":9200,"conversation_message_id":2,"from_id":77,"text":"Middle"},{"id":9100,"conversation_message_id":1,"from_id":77,"text":"First"}]}})";
        network.responses["messages.getHistory"] = {history, history, history};
        network.responses["messages.getByConversationMessageId"] = {R"({"response":{"items":[{"id":9200,"conversation_message_id":2}]}})"};
        Messenger messenger(nullptr, &network);
        LoginBrowserCapture browser;
        QDesktopServices::setUrlHandler("https", &browser, "capture");
        messenger.login();
        QDesktopServices::unsetUrlHandler("https");
        QVERIFY(messenger.browserLoginPending());
        const QUrlQuery authorize(browser.url);
        const auto state = authorize.queryItemValue("state");
        QVERIFY(!state.isEmpty());
        network.responses["auth"] = {QJsonDocument(QJsonObject{{"access_token", "fixture-vkid-token"}, {"refresh_token", "fixture-refresh"},
            {"user_id", 42}, {"expires_in", 3600}, {"state", state}}).toJson()};
        QUrl callback(authorize.queryItemValue("redirect_uri", QUrl::FullyDecoded));
        QUrlQuery parameters; parameters.addQueryItem("code", "fixture+code/value=="); parameters.addQueryItem("state", state);
        parameters.addQueryItem("device_id", "fixture-device"); parameters.addQueryItem("type", "code_v2"); callback.setQuery(parameters);
        messenger.completeLogin(callback.toString());
        QTRY_VERIFY(messenger.connected()); QVERIFY(!messenger.authorizing());
        QTRY_COMPARE(messenger.conversations()->rowCount(), 1);
        QVERIFY(messenger.error().isEmpty());
        QCOMPARE(messenger.conversations()->get(0).value("title").toString(), "Тестовый Собеседник");
        bool exchanged = false;
        for (const auto &request : network.requests) if (request.first.path() == "/oauth2/auth") {
            exchanged = true; QCOMPARE(request.first.host(), "id.vk.ru");
            const QUrlQuery body(QString::fromUtf8(request.second));
            QCOMPARE(body.queryItemValue("grant_type"), "authorization_code");
            QCOMPARE(body.queryItemValue("code", QUrl::FullyDecoded), "fixture+code/value==");
            QCOMPARE(body.queryItemValue("device_id"), "fixture-device");
            QCOMPARE(body.queryItemValue("state"), state);
            QCOMPARE(body.queryItemValue("client_id"), authorize.queryItemValue("client_id"));
            QCOMPARE(OAuth::challenge(body.queryItemValue("code_verifier")), authorize.queryItemValue("code_challenge"));
            QVERIFY(!body.hasQueryItem("client_secret"));
        }
        QVERIFY(exchanged);
        QCOMPARE(QJsonDocument::fromJson(savedSession).object().value("access_token").toString(), "fixture-vkid-token");
        messenger.selectConversation("77", "Search");
        QTRY_COMPARE(messenger.messages()->rowCount(), 3);
        QCOMPARE(messenger.messages()->get(0).value("text").toString(), "First");
        QTRY_COMPARE(network.replies["im"].size(), 1);
        const QUrlQuery poll(network.replies["im"][0]->url());
        QCOMPARE(poll.queryItemValue("version"), "3"); QCOMPARE(poll.queryItemValue("act"), "a_check");
        QCOMPARE(poll.queryItemValue("ts"), "100");
        messenger.openMessage("77", "2");
        QTRY_COMPARE(network.replies["messages.getByConversationMessageId"].size(), 1);
        QTRY_VERIFY(!messenger.loading());
        bool globalAnchor = false;
        for (const auto &request : network.requests) if (request.first.path().endsWith("messages.getHistory")) {
            const QUrlQuery body(QString::fromUtf8(request.second));
            if (body.hasQueryItem("start_message_id")) { QCOMPARE(body.queryItemValue("start_message_id"), "9200"); globalAnchor = true; }
        }
        QVERIFY(globalAnchor);
        network.replies["im"][0]->deliver(R"({"ts":"101","updates":[[4,9400,0,77,1700000000,"New",{}]]})");
        QTRY_COMPARE(network.replies["messages.getConversations"].size(), 2);
        QTRY_VERIFY(!messenger.loading());
        QCOMPARE(network.replies["messages.getDiff"].size(), 1);
        QCOMPARE(network.replies["messages.getDiffContent"].size(), 0);
        QVERIFY(messenger.error().isEmpty());
    }

    void publicConversationPagesArePublishedTogetherAndDiscardedOnLogout()
    {
        TokenNetwork network;
        network.responses["messages.getDiff"] = {R"({"error":{"error_code":3,"error_msg":"Unknown method passed"}})"};
        QJsonArray items;
        for (int i = 1; i <= 200; ++i) items.append(QJsonObject{{"conversation", QJsonObject{{"peer", QJsonObject{{"id", i}}}}},
            {"last_message", QJsonObject{{"id", 10000 + i}, {"conversation_message_id", 1}, {"peer_id", i}, {"text", "Fixture"}}}});
        network.responses["messages.getConversations"] = {QJsonDocument(QJsonObject{{"response", QJsonObject{{"count", 201}, {"items", items}}}}).toJson()};
        Messenger messenger(nullptr, &network);
        messenger.loginWithToken("fixture-token");
        QTRY_COMPARE(network.replies["messages.getConversations"].size(), 2);
        QCOMPARE(messenger.conversations()->rowCount(), 0); // no partial snapshot
        network.replies["messages.getConversations"][1]->deliver(R"({"response":{"count":201,"items":[{"conversation":{"peer":{"id":201}},"last_message":{"id":10201,"conversation_message_id":1,"peer_id":201,"text":"Last page"}}]}})");
        QTRY_COMPARE(messenger.conversations()->rowCount(), 201);
        QStringList offsets;
        for (const auto &request : network.requests) if (request.first.path().endsWith("messages.getConversations"))
            offsets.append(QUrlQuery(QString::fromUtf8(request.second)).queryItemValue("offset"));
        QCOMPARE(offsets, (QStringList{"0", "200"}));
        messenger.reload();
        QTRY_COMPARE(network.replies["messages.getConversations"].size(), 3);
        // Start another two-page snapshot, then switch sessions while it waits.
        network.replies["messages.getConversations"][2]->deliver(QJsonDocument(QJsonObject{{"response", QJsonObject{{"count", 201}, {"items", items}}}}).toJson());
        QTRY_COMPARE(network.replies["messages.getConversations"].size(), 4);
        QCOMPARE(messenger.conversations()->rowCount(), 201);
        auto stale = network.replies["messages.getConversations"][3];
        messenger.logout();
        if (stale) stale->deliver(R"({"response":{"count":201,"items":[]}})");
        QTest::qWait(100);
        QVERIFY(!messenger.connected()); QCOMPARE(messenger.conversations()->rowCount(), 0);
    }

    void unavailableMessagesAccessDoesNotRepeatLoginOrUnknownMethod()
    {
        TokenNetwork network;
        network.responses["messages.getDiff"] = {R"({"error":{"error_code":3,"error_msg":"Unknow method passed"}})"};
        network.responses["messages.getConversations"] = {R"({"error":{"error_code":15,"error_msg":"Access denied"}})"};
        Messenger messenger(nullptr, &network);
        messenger.loginWithToken("fixture-token");
        QTRY_VERIFY(messenger.error().contains("недоступно чтение сообщений"));
        QVERIFY(messenger.connected()); QVERIFY(!messenger.authorizing());
        QVERIFY(!savedSession.isEmpty());
        QVERIFY(!messenger.error().contains("Unknow"));
        QTest::qWait(3200);
        messenger.reload(); QTest::qWait(250);
        QCOMPARE(network.replies["messages.getDiff"].size(), 1);
        QCOMPARE(network.replies["messages.getConversations"].size(), 1);
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

    void itemModelBatchesHistoryChanges()
    {
        ItemModel model;
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        QSignalSpy inserts(&model, &QAbstractItemModel::rowsInserted), removes(&model, &QAbstractItemModel::rowsRemoved);
        QSignalSpy moves(&model, &QAbstractItemModel::rowsMoved), changes(&model, &QAbstractItemModel::dataChanged);
        QSignalSpy resets(&model, &QAbstractItemModel::modelReset), revisions(&model, &ItemModel::revisionChanged);
        QVariantList rows;
        for (int i = 0; i < 10000; ++i) rows.append(QVariantMap{{"id", QString::number(i + 1)}, {"text", "Сообщение"}});
        model.reconcile(rows);
        QCOMPARE(model.rows, rows); QCOMPARE(inserts.count(), 1); QCOMPARE(resets.count(), 0);
        QCOMPARE(inserts.first()[1].toInt(), 0); QCOMPARE(inserts.first()[2].toInt(), 9999);
        QPersistentModelIndex retained(model.index(5000, 0));
        rows.remove(0, 500);
        model.reconcile(rows);
        QCOMPARE(removes.count(), 1); QCOMPARE(moves.count(), 0); QCOMPARE(retained.row(), 4500);
        QCOMPARE(retained.data(Qt::UserRole).toMap().value("id").toString(), QString("5001"));
        QVariantList older;
        for (int i = 0; i < 200; ++i) older.append(QVariantMap{{"id", QString("older:%1").arg(i)}});
        rows = older + rows; inserts.clear();
        model.reconcile(rows);
        QCOMPARE(inserts.count(), 1); QCOMPARE(moves.count(), 0); QCOMPARE(retained.row(), 4700);
        for (int i = 200; i < 400; ++i) { auto row = rows[i].toMap(); row["text"] = "Обновлено"; rows[i] = row; }
        model.reconcile(rows);
        QCOMPARE(changes.count(), 1);
        QCOMPARE(changes.first()[0].value<QModelIndex>().row(), 200);
        QCOMPARE(changes.first()[1].value<QModelIndex>().row(), 399);
        QCOMPARE(model.rows, rows); QCOMPARE(revisions.count(), 4); QCOMPARE(resets.count(), 0);
        model.reconcile(rows); QCOMPARE(revisions.count(), 4);
        qInfo() << "10,000 initial rows: one insertion; removing a 500-row prefix: one removal, zero moves; prepending 200 rows: one insertion";
    }

    void itemModelKeepsIdentityThroughMixedUpdates()
    {
        ItemModel model;
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        QRandomGenerator random(20261109);
        for (int step = 0; step < 150; ++step) {
            QHash<QString, QPersistentModelIndex> retained;
            for (int i = 0; i < model.rowCount(); ++i) retained.insert(model.get(i).value("identity").toString(), model.index(i, 0));
            QList<int> ids; for (int i = 0; i < 70; ++i) ids.append(i);
            for (int i = ids.size() - 1; i > 0; --i) ids.swapItemsAt(i, random.bounded(i + 1));
            QVariantList rows; QSet<QString> wanted;
            const int count = random.bounded(50);
            for (int i = 0; i < count; ++i) {
                const auto identity = QString("local:%1").arg(ids[i]); wanted.insert(identity);
                rows.append(QVariantMap{{"identity", identity}, {"id", QString::number(step * 100 + ids[i])}, {"text", QString::number(step)}});
            }
            model.reconcile(rows); QCOMPARE(model.rows, rows);
            for (auto it = retained.begin(); it != retained.end(); ++it) {
                QCOMPARE(it.value().isValid(), wanted.contains(it.key()));
                if (it.value().isValid()) QCOMPARE(it.value().data(Qt::UserRole).toMap().value("identity").toString(), it.key());
            }
        }
        // Incomplete, duplicate and numeric identities retain the compatible path.
        for (const auto &rows : {QVariantList{QVariantMap{{"text", "Без ID"}}, QVariantMap{{"text", "Второе"}}},
                 QVariantList{QVariantMap{{"id", "1"}}, QVariantMap{{"id", "1"}, {"text", "Повтор"}}},
                 QVariantList{QVariantMap{{"id", 2}}, QVariantMap{{"id", 1}}}}) {
            model.reconcile(rows); QCOMPARE(model.rows, rows);
        }
    }

    void conversationFilterTracksLiveChanges()
    {
        Messenger messenger;
        auto source = messenger.conversations();
        auto filtered = messenger.filteredConversations();
        QAbstractItemModelTester tester(filtered, QAbstractItemModelTester::FailureReportingMode::QtTest);
        QVariantList rows{
            QVariantMap{{"id", "1"}, {"peer", "1"}, {"title", "Анна"}, {"pinned", true}, {"important", true}},
            QVariantMap{{"id", "2"}, {"peer", "2"}, {"title", "Работа"}, {"pinned", true}, {"unread", 2}},
            QVariantMap{{"id", "3"}, {"peer", "3"}, {"title", "Анна — работа"}, {"markedUnread", true}, {"important", true}},
            QVariantMap{{"id", "4"}, {"peer", "4"}, {"title", "Архив"}, {"archived", true}, {"unread", 1}},
            QVariantMap{{"id", "5"}, {"peer", "5"}, {"title", "Друзья"}}
        };
        source->replace(rows);
        QCoreApplication::processEvents();
        QSignalSpy unnecessaryUpdates(filtered, &ConversationFilterModel::changed);
        messenger.folders()->conversationsChanged();
        QCoreApplication::processEvents();
        QCOMPARE(unnecessaryUpdates.count(), 0);
        QCOMPARE(filtered->rowCount(), 4);
        QCOMPARE(filtered->pinnedBoundaryPeer(), "3");
        filtered->setQuery("  АННА  ");
        QCOMPARE(filtered->rowCount(), 2);
        QCOMPARE(filtered->get(0).value("peer").toString(), "1");
        filtered->setUnreadOnly(true);
        QCOMPARE(filtered->rowCount(), 1);
        QCOMPARE(filtered->get(0).value("peer").toString(), "3");
        QVERIFY(filtered->pinnedBoundaryPeer().isEmpty());
        filtered->setCategory(2);
        auto first = rows[0].toMap(); first.insert("unread", 4); rows[0] = first;
        source->reconcile(rows);
        QCOMPARE(filtered->rowCount(), 2);
        QCOMPARE(filtered->pinnedBoundaryPeer(), "3");
        first.insert("title", "Другое название"); rows[0] = first; source->reconcile(rows);
        QCOMPARE(filtered->rowCount(), 1);
        QCOMPARE(filtered->get(0).value("peer").toString(), "3");
        first.insert("title", "Анна"); rows[0] = first; source->reconcile(rows);
        QCOMPARE(filtered->rowCount(), 2);
        std::reverse(rows.begin(), rows.end()); source->reconcile(rows);
        QCOMPARE(filtered->get(0).value("peer").toString(), "3");
        QCOMPARE(filtered->get(1).value("peer").toString(), "1");
        filtered->setQuery(""); filtered->setCategory(3);
        QCOMPARE(filtered->rowCount(), 1);
        QCOMPARE(filtered->get(0).value("peer").toString(), "4");
        source->replace({}); QCOMPARE(filtered->rowCount(), 0);
        QVERIFY(filtered->get(-1).isEmpty()); QVERIFY(filtered->get(0).isEmpty());
    }

    void conversationChoicesTrackSourceIndependentlyOfSidebar()
    {
        Messenger messenger;
        auto source = messenger.conversations();
        auto choices = messenger.conversationChoices();
        QAbstractItemModelTester tester(choices, QAbstractItemModelTester::FailureReportingMode::QtTest);
        QVariantList rows{
            QVariantMap{{"id", "1"}, {"peer", "1"}, {"title", "Анна"}},
            QVariantMap{{"id", "2"}, {"peer", "2"}, {"title", "Архив"}, {"archived", true}},
            QVariantMap{{"id", "-3"}, {"peer", "-3"}, {"title", "Сообщество"}},
            QVariantMap{{"id", "2000000001"}, {"peer", "2000000001"}, {"title", "Беседа"}}
        };
        source->replace(rows);
        messenger.filteredConversations()->setQuery("Не совпадает");
        messenger.filteredConversations()->setUnreadOnly(true);
        QCOMPARE(messenger.filteredConversations()->rowCount(), 0);
        QCOMPARE(choices->rowCount(), 4);
        choices->setPeopleOnly(true);
        QCOMPARE(choices->rowCount(), 2);
        QCOMPARE(choices->get(1).value("title").toString(), "Архив");
        QPersistentModelIndex retained(choices->index(1, 0));
        auto archived = rows[1].toMap(); archived["title"] = "Новое имя"; rows[1] = archived;
        rows.removeFirst();
        rows.append(QVariantMap{{"id", "5"}, {"peer", "5"}, {"title", "Новый участник"}});
        source->reconcile(rows);
        QCOMPARE(choices->rowCount(), 2);
        QVERIFY(retained.isValid());
        QCOMPARE(retained.data(Qt::UserRole).toMap().value("title").toString(), "Новое имя");
        QCOMPARE(choices->get(1).value("peer").toString(), "5");
        choices->setPeopleOnly(false);
        QCOMPARE(choices->rowCount(), 4);
        source->replace({}); QCOMPARE(choices->rowCount(), 0);
        QVERIFY(choices->get(-1).isEmpty()); QVERIFY(choices->get(0).isEmpty());
    }

    void recipientPickerUsesLiveModelAndKeyboard()
    {
        Messenger messenger; messenger.startDemo();
        QQmlApplicationEngine engine; engine.rootContext()->setContextProperty("messenger", &messenger);
        QSignalSpy qmlWarnings(&engine, &QQmlEngine::warnings);
        engine.load(QUrl::fromLocalFile(QStringLiteral(NATIVE_QML_PATH))); QVERIFY(!engine.rootObjects().isEmpty());
        auto window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        window->resize(780, 560); window->requestActivate(); QTRY_VERIFY(window->isActive());
        auto sheet = window->findChild<QObject *>("actionSheet"); QVERIFY(sheet);
        sheet->setProperty("reducedMotion", true);
        auto results = sheet->findChild<QQuickItem *>("actionResults"); QVERIFY(results);
        auto query = sheet->findChild<QQuickItem *>("actionSearch"); QVERIFY(query);
        auto count = sheet->findChild<QQuickItem *>("recipientSelectionCount"); QVERIFY(count);
        auto reset = sheet->findChild<QQuickItem *>("clearRecipientSelection"); QVERIFY(reset);
        QVariantList rows; rows.reserve(10000);
        for (int i = 0; i < 10000; ++i) {
            const auto peer = QString::number(i < 5000 ? i + 1 : i < 7500 ? 2000000001LL + i : -i);
            rows.append(QVariantMap{{"id", peer}, {"peer", peer}, {"title", QString("Собеседник %1").arg(i + 1)}, {"preview", "Последнее сообщение"}, {"time", ""}, {"archived", i % 2 == 0}});
        }
        messenger.conversations()->replace(rows);
        messenger.filteredConversations()->setQuery("Не совпадает");
        const auto show = [&](const QString &mode) {
            return QMetaObject::invokeMethod(sheet, "show", Q_ARG(QVariant, mode), Q_ARG(QVariant, "77"), Q_ARG(QVariant, QStringList{"3"}), Q_ARG(QVariant, "Сообщение для пересылки"));
        };
        QVERIFY(show("forward"));
        QTRY_COMPARE(results->property("count").toInt(), 10000);
        QCOMPARE(qvariant_cast<QObject *>(results->property("model")), messenger.conversationChoices());
        const auto content = results->property("contentItem").value<QQuickItem *>(); QVERIFY(content);
        QTRY_VERIFY(!content->childItems().isEmpty());
        QVERIFY(content->childItems().size() < 50);
        QTest::keyClick(window, Qt::Key_Down);
        QTRY_VERIFY(results->hasActiveFocus()); QCOMPARE(results->property("currentIndex").toInt(), 0);
        QTest::keyClick(window, Qt::Key_Space);
        QTRY_COMPARE(sheet->property("selectedPeers").value<QJSValue>().toVariant().toList(), QVariantList{"1"});
        QTRY_VERIFY(count->isVisible()); QCOMPARE(count->property("text").toString(), "Выбрано: 1");
        QTest::keyClick(window, Qt::Key_Down); QTest::keyClick(window, Qt::Key_Return);
        QTRY_COMPARE(count->property("text").toString(), "Выбрано: 2");
        QTest::keyClick(window, Qt::Key_Escape); QTRY_VERIFY(query->hasActiveFocus());
        QVERIFY(sheet->property("visible").toBool());
        for (bool dark : {false, true}) {
            messenger.setDarkTheme(dark); QTest::qWait(30);
            QVERIFY(results->height() > 60); QVERIFY(results->width() > 500);
            if (qEnvironmentVariableIsSet("VKM_RECIPIENT_SCREENSHOT"))
                QVERIFY(window->grabWindow().save(qEnvironmentVariable("VKM_RECIPIENT_SCREENSHOT") + (dark ? "-dark.png" : "-light.png")));
        }
        QVERIFY(QMetaObject::invokeMethod(reset, "clicked")); QTRY_VERIFY(!count->isVisible());
        QVERIFY(show("create")); QTRY_COMPARE(results->property("count").toInt(), 5000);
        QVERIFY(messenger.conversationChoices()->peopleOnly());
        QTest::keyClick(window, Qt::Key_Up); QTRY_COMPARE(results->property("currentIndex").toInt(), 4999);
        QTest::keyClick(window, Qt::Key_Return); QTRY_COMPARE(count->property("text").toString(), "Выбрано: 1");
        auto updated = rows[0].toMap(); updated["title"] = "Обновлённое имя"; rows[0] = updated;
        messenger.conversations()->reconcile(rows);
        QCOMPARE(messenger.conversationChoices()->get(0).value("title").toString(), "Обновлённое имя");
        QVERIFY(show("add")); QTRY_COMPARE(results->property("count").toInt(), 5000);
        QVERIFY(show("peers")); QTRY_COMPARE(results->property("count").toInt(), 10000);
        query->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Down); QTest::keyClick(window, Qt::Key_Return);
        QTRY_VERIFY(!sheet->property("visible").toBool()); QCOMPARE(messenger.selectedPeer(), "1");
        QTRY_COMPARE(results->property("count").toInt(), 0);
        QCOMPARE(qmlWarnings.count(), 0);
    }

    void actionSearchAndStickerGridReleaseHiddenMedia()
    {
        TokenNetwork network;
        network.responses["messages.getDiff"] = {R"({"response":{"server_version":"500","credentials":{"server_lp":"lp.test/im","key":"key","ts":"100"},"conversations_info":[{"conversation":{"peer":{"id":77},"last_conversation_message_id":1},"message":[{"conversation_message_id":1,"peer_id":77,"text":"Fixture"}]}]}})"};
        network.responses["users.search"] = {R"({"response":{"items":[{"id":1,"first_name":"Anna"},{"id":-3,"first_name":"Group"},{"id":2000000001,"first_name":"Chat"},{"id":2,"first_name":"Anne"}]}})"};
        QJsonArray stickers;
        for (int i = 1; i <= 300; ++i)
            stickers.append(QJsonObject{{"sticker_id", i}, {"images", QJsonArray{
                QJsonObject{{"url", QString("https://assets.test/sticker-%1-small").arg(i)}, {"width", 256}, {"height", 256}},
                QJsonObject{{"url", QString("https://assets.test/sticker-%1-large").arg(i)}, {"width", 1024}, {"height", 1024}}
            }}});
        const auto pack = QJsonObject{{"stickers", stickers}, {"title", "Fixture"}};
        network.responses["store.getProducts"] = {QJsonDocument(QJsonObject{{"response", QJsonObject{{"items", QJsonArray{pack}}}}}).toJson()};
        Messenger messenger(nullptr, &network);
        QQmlApplicationEngine engine; engine.rootContext()->setContextProperty("messenger", &messenger);
        QSignalSpy qmlWarnings(&engine, &QQmlEngine::warnings);
        engine.load(QUrl::fromLocalFile(QStringLiteral(NATIVE_QML_PATH))); QVERIFY(!engine.rootObjects().isEmpty());
        auto window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        messenger.loginWithToken("test-token"); QTRY_COMPARE(messenger.conversations()->rowCount(), 1);
        auto sheet = window->findChild<QObject *>("actionSheet"); QVERIFY(sheet); sheet->setProperty("reducedMotion", true);
        auto results = sheet->findChild<QQuickItem *>("actionResults"); QVERIFY(results);
        auto query = sheet->findChild<QQuickItem *>("actionSearch"); QVERIFY(query);
        auto grid = sheet->findChild<QQuickItem *>("actionStickers"); QVERIFY(grid);
        const auto show = [&](const QString &mode) { return QMetaObject::invokeMethod(sheet, "show", Q_ARG(QVariant, mode), Q_ARG(QVariant, "77"), Q_ARG(QVariant, QStringList{}), Q_ARG(QVariant, "")); };
        QVERIFY(show("create"));
        query->setProperty("text", "Ann"); QVERIFY(QMetaObject::invokeMethod(query, "accepted"));
        QTRY_COMPARE(results->property("count").toInt(), 2);
        const auto content = results->property("contentItem").value<QQuickItem *>(); QVERIFY(content);
        QTRY_VERIFY(results->property("contentHeight").toReal() > 100);
        QVERIFY(results->property("contentHeight").toReal() < 150);
        QVERIFY(QMetaObject::invokeMethod(sheet->findChild<QObject *>("clearActionSearch"), "clicked"));
        QTRY_COMPARE(results->property("count").toInt(), 1); QCOMPARE(query->property("text").toString(), "");
        QVERIFY(messenger.actions()->results().isEmpty());
        QVERIFY(show("stickers")); QTRY_COMPARE(grid->property("count").toInt(), 300);
        const auto gridContent = grid->property("contentItem").value<QQuickItem *>(); QVERIFY(gridContent);
        const auto requests = [&] { int count = 0; for (const auto &r : network.requests) if (r.first.host() == "assets.test") ++count; return count; };
        QTRY_VERIFY(requests() > 0); QVERIFY(requests() < 60);
        for (const auto &r : network.requests) if (r.first.host() == "assets.test") QVERIFY(r.first.path().endsWith("-small"));
        QVERIFY(gridContent->childItems().size() < 60);
        grid->setProperty("contentY", 1800); QTest::qWait(30);
        const auto oldRequestCount = requests();
        QVERIFY(show("edit")); QTRY_COMPARE(grid->property("count").toInt(), 0);
        QCOMPARE(results->property("count").toInt(), 0); QTest::qWait(30);
        QCOMPARE(requests(), oldRequestCount);
        QVERIFY(QMetaObject::invokeMethod(sheet, "close"));
        QCOMPARE(qmlWarnings.count(), 0);
    }

    void largeConversationListFiltersWithoutHiddenRows()
    {
        Messenger messenger;
        messenger.startDemo();
        QQmlApplicationEngine engine; engine.rootContext()->setContextProperty("messenger", &messenger);
        engine.load(QUrl::fromLocalFile(QStringLiteral(NATIVE_QML_PATH))); QVERIFY(!engine.rootObjects().isEmpty());
        auto window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        auto chats = window->findChild<QQuickItem *>("conversationList"); QVERIFY(chats);
        auto emptyState = window->findChild<QQuickItem *>("conversationEmptyState"); QVERIFY(emptyState);
        window->setProperty("search", "переписка 9999");
        QVariantList rows; rows.reserve(10000);
        for (int i = 0; i < 10000; ++i) rows.append(QVariantMap{{"id", QString::number(i + 1)}, {"peer", QString::number(i + 1)}, {"title", QString("Переписка %1").arg(i)}, {"preview", ""}, {"time", ""}});
        QElapsedTimer timer; timer.start();
        messenger.conversations()->replace(rows);
        QTRY_COMPARE(chats->property("count").toInt(), 1);
        QCOMPARE(messenger.filteredConversations()->get(0).value("peer").toString(), "10000");
        qInfo() << "10,000 conversations, filtered model ready in" << timer.elapsed() << "ms";
        QTRY_VERIFY(chats->property("contentHeight").toReal() <= 66);
        QVERIFY(!emptyState->isVisible());
        window->setProperty("search", "Несуществующая переписка");
        QTRY_COMPARE(chats->property("count").toInt(), 0);
        QTRY_VERIFY(emptyState->isVisible());
        QVERIFY(QMetaObject::invokeMethod(window, "resetConversationFilters"));
        QTRY_COMPARE(chats->property("count").toInt(), 10000);
        QTRY_VERIFY(!emptyState->isVisible());
        const auto content = chats->property("contentItem").value<QQuickItem *>(); QVERIFY(content);
        QTRY_VERIFY(content->childItems().size() < 50);
    }

    void draftAttachmentStripKeepsChatAndGlobalIndexes()
    {
        Messenger messenger;
        QQmlApplicationEngine engine; engine.rootContext()->setContextProperty("messenger", &messenger);
        engine.load(QUrl::fromLocalFile(QStringLiteral(NATIVE_QML_PATH))); QVERIFY(!engine.rootObjects().isEmpty());
        auto window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        messenger.startDemo(); window->requestActivate(); QTRY_VERIFY(window->isActive());
        QJsonArray pending;
        for (int i = 0; i < 12; ++i) pending.append(QJsonObject{{"peer", i % 3 == 0 ? "2" : "1"}, {"attachment", (i == 2 ? "photo42_" : "doc42_") + QString::number(i)}, {"name", QString("Документ %1 с длинным именем <файл>.pdf").arg(i)}});
        messenger.transfers()->restoreState(QJsonObject{{"pending", pending}});
        auto strip = window->findChild<QQuickItem *>("draftAttachmentStrip"); QVERIFY(strip);
        auto history = window->findChild<QQuickItem *>("historyList"); QVERIFY(history);
        auto composer = window->findChild<QQuickItem *>("composer"); QVERIFY(composer);
        auto send = window->findChild<QQuickItem *>("sendMessageButton"); QVERIFY(send);
        const auto chips = [&] {
            QList<QQuickItem *> items;
            const auto visit = [&](auto &&self, QQuickItem *item) -> void { if (item->objectName() == "draftAttachmentChip") items.append(item); for (auto child : item->childItems()) self(self, child); };
            visit(visit, strip); return items;
        };
        QTRY_VERIFY(strip->isVisible()); QTRY_COMPARE(strip->property("count").toInt(), 8);
        QVERIFY(send->isEnabled());
        strip->forceActiveFocus();
        QTest::keyClick(window, Qt::Key_End); QTRY_COMPARE(strip->property("currentIndex").toInt(), 7);
        QTRY_VERIFY(strip->property("contentWidth").toReal() > strip->width());
        QTRY_VERIFY(strip->property("contentX").toReal() > 0);
        QTest::keyClick(window, Qt::Key_Home); QTRY_COMPARE(strip->property("currentIndex").toInt(), 0); QTRY_COMPARE(strip->property("contentX").toReal(), 0.0);
        QTest::keyClick(window, Qt::Key_Right); QTRY_COMPARE(strip->property("currentIndex").toInt(), 1);
        auto current = strip->property("currentItem").value<QQuickItem *>(); QVERIFY(current);
        auto second = current->findChild<QObject *>("removeDraftAttachment"); QVERIFY(second);
        QVERIFY(QMetaObject::invokeMethod(second, "clicked"));
        QTRY_COMPARE(strip->property("count").toInt(), 7);
        const auto saved = messenger.transfers()->savedState().value("pending").toArray();
        QCOMPARE(saved.size(), 11);
        QVERIFY(!messenger.transfers()->attachments("1").contains("photo42_2"));
        QCOMPARE(messenger.transfers()->attachments("2").size(), 4);
        QVERIFY(composer->hasActiveFocus());
        composer->setProperty("text", "Черновик с вложениями");
        QVERIFY(QMetaObject::invokeMethod(window, "selectChat", Q_ARG(QVariant, "2"), Q_ARG(QVariant, "Анна")));
        QTRY_COMPARE(strip->property("count").toInt(), 4);
        QVERIFY(QMetaObject::invokeMethod(window, "selectChat", Q_ARG(QVariant, "1"), Q_ARG(QVariant, "Команда")));
        QTRY_COMPARE(strip->property("count").toInt(), 7); QCOMPARE(composer->property("text").toString(), QString("Черновик с вложениями"));
        const QVariantMap editable{{"id", "2"}, {"outgoing", true}, {"text", "Правка"}};
        QVERIFY(QMetaObject::invokeMethod(window, "messageActions", Q_ARG(QVariant, "edit"), Q_ARG(QVariant, editable)));
        QTRY_VERIFY(!strip->isVisible());
        QTRY_COMPARE(strip->property("count").toInt(), 0);
        QVERIFY(QMetaObject::invokeMethod(window, "cancelEditing"));
        QTRY_VERIFY(strip->isVisible()); QCOMPARE(composer->property("text").toString(), QString("Черновик с вложениями"));
        window->resize(780, 560); QTest::qWait(150);
        QVERIFY(strip->height() <= 60); QVERIFY(history->height() >= 150);
        QVERIFY(strip->property("contentWidth").toReal() > strip->width());
        if (qEnvironmentVariableIsSet("VKM_ATTACHMENTS_SCREENSHOT")) {
            for (const bool dark : {true, false}) {
                messenger.setDarkTheme(dark); QTest::qWait(200);
                QVERIFY(window->grabWindow().save(qEnvironmentVariable("VKM_ATTACHMENTS_SCREENSHOT") + (dark ? "-dark.png" : "-light.png")));
            }
        }
        messenger.transfers()->reset(); QTRY_VERIFY(!strip->isVisible());
        // Restored drafts may contain many files across chats; create only the
        // viewport and a bounded reuse pool, retaining original removal indexes.
        QJsonArray many;
        for (int i = 0; i < 1000; ++i) {
            many.append(QJsonObject{{"peer", "2"}, {"attachment", QString("doc99_%1").arg(i)}, {"name", "Другой диалог.pdf"}});
            many.append(QJsonObject{{"peer", "1"}, {"attachment", QString("doc42_%1").arg(i)}, {"name", QString("Файл %1.pdf").arg(i)}});
        }
        messenger.transfers()->restoreState(QJsonObject{{"pending", many}});
        QTRY_COMPARE(strip->property("count").toInt(), 1000); QTest::qWait(100); QVERIFY(chips().size() < 15);
        strip->forceActiveFocus(); QTest::keyClick(window, Qt::Key_End);
        QTRY_COMPARE(strip->property("currentIndex").toInt(), 999); QTRY_VERIFY(strip->property("currentItem").value<QQuickItem *>());
        QTest::qWait(100); QVERIFY(chips().size() < 15);
        QTest::keyClick(window, Qt::Key_Delete);
        QTRY_COMPARE(strip->property("count").toInt(), 999); QVERIFY(composer->hasActiveFocus());
        QVERIFY(!messenger.transfers()->attachments("1").contains("doc42_999"));
        QCOMPARE(messenger.transfers()->attachments("2").size(), 1000);
        strip->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Escape); QVERIFY(composer->hasActiveFocus());
        messenger.transfers()->reset(); QTRY_COMPARE(strip->property("count").toInt(), 0);
    }

    void composerShowsLengthLimitAndPreservesOversizedDraft()
    {
        Messenger messenger; messenger.startDemo();
        QQmlApplicationEngine engine; engine.rootContext()->setContextProperty("messenger", &messenger);
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        engine.load(QUrl::fromLocalFile(QStringLiteral(NATIVE_QML_PATH))); QVERIFY(!engine.rootObjects().isEmpty());
        auto window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        window->resize(780, 560); window->requestActivate(); QTRY_VERIFY(window->isActive());
        auto composer = window->findChild<QQuickItem *>("composer"); QVERIFY(composer);
        auto send = window->findChild<QQuickItem *>("sendMessageButton"); QVERIFY(send);
        auto counter = window->findChild<QQuickItem *>("composerCharacterCount"); QVERIFY(counter);
        auto hint = window->findChild<QQuickItem *>("composerHint"); QVERIFY(hint);
        composer->forceActiveFocus(); composer->setProperty("text", QString(3499, 'a')); QVERIFY(!counter->isVisible());
        composer->setProperty("text", QString(3500, 'a')); QVERIFY(counter->isVisible());
        QCOMPARE(counter->property("text").toString(), "3500 / 4096");
        composer->setProperty("text", QString(4096, 'a')); QVERIFY(send->isEnabled()); QVERIFY(!composer->property("overLimit").toBool());
        const QString oversized = QString::fromUtf8("Текст ") + QString(4091, 'a');
        QCOMPARE(oversized.size(), 4097);
        composer->setProperty("text", oversized); QVERIFY(!send->isEnabled()); QVERIFY(composer->property("overLimit").toBool());
        QCOMPARE(hint->property("text").toString(), "Сократите сообщение на 1 симв.");
        const auto count = messenger.messages()->rowCount();
        QVERIFY(QMetaObject::invokeMethod(composer, "submitWithMode", Q_ARG(QVariant, true)));
        QTest::keyClick(window, Qt::Key_Return);
        QCOMPARE(messenger.messages()->rowCount(), count); QCOMPARE(messenger.draftText(), oversized);
        if (qEnvironmentVariableIsSet("VKM_COMPOSER_LIMIT_SCREENSHOT")) {
            for (const bool dark : {false, true}) {
                messenger.setDarkTheme(dark); QTest::qWait(80);
                QVERIFY(window->grabWindow().save(qEnvironmentVariable("VKM_COMPOSER_LIMIT_SCREENSHOT") + (dark ? "-dark.png" : "-light.png")));
            }
        }
        QVERIFY(QMetaObject::invokeMethod(window, "selectChat", Q_ARG(QVariant, "2"), Q_ARG(QVariant, "Анна")));
        QVERIFY(QMetaObject::invokeMethod(window, "selectChat", Q_ARG(QVariant, "1"), Q_ARG(QVariant, "Команда")));
        QCOMPARE(composer->property("text").toString(), oversized); QVERIFY(!send->isEnabled());
        const QVariantMap editable{{"id", "2"}, {"outgoing", true}, {"text", oversized}};
        QVERIFY(QMetaObject::invokeMethod(window, "messageActions", Q_ARG(QVariant, "edit"), Q_ARG(QVariant, editable)));
        QVERIFY(!send->isEnabled()); QVERIFY(QMetaObject::invokeMethod(composer, "submit"));
        QCOMPARE(window->property("editingMessage").toString(), "2");
        QVERIFY(QMetaObject::invokeMethod(window, "cancelEditing")); QCOMPARE(composer->property("text").toString(), oversized);
        composer->setProperty("text", QString(4096, 'a')); QVERIFY(send->isEnabled());
        composer->setProperty("text", QString::fromUtf8("😀")); QVERIFY(!composer->property("overLimit").toBool()); QVERIFY(send->isEnabled());
        composer->setProperty("text", " \n\t "); QVERIFY(!send->isEnabled());
        QCOMPARE(warnings.size(), 0);
    }

    void cacheDialogFitsAndClearsMediaWithoutRemovingDrafts()
    {
        QImage image(64, 64, QImage::Format_RGB32); image.fill(Qt::blue);
        QByteArray png; QBuffer buffer(&png); QVERIFY(buffer.open(QIODevice::WriteOnly)); QVERIFY(image.save(&buffer, "PNG"));
        TokenNetwork network; network.responses["cache-screen-photo"] = {png};
        Messenger messenger(nullptr, &network); messenger.startDemo();
        messenger.setDraftText("1", "Черновик остаётся после очистки");
        const QUrl photo("https://assets.test/cache-screen-photo");
        messenger.media()->source(photo); QTRY_VERIFY(!messenger.media()->cached(photo).isEmpty());
        QQmlApplicationEngine engine; engine.rootContext()->setContextProperty("messenger", &messenger);
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        engine.load(QUrl::fromLocalFile(QStringLiteral(NATIVE_QML_PATH))); QVERIFY(!engine.rootObjects().isEmpty());
        auto window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        window->resize(780, 560); window->requestActivate(); QTRY_VERIFY(window->isActive());
        auto dialog = window->findChild<QObject *>("cacheDialog"); QVERIFY(dialog);
        QVERIFY(QMetaObject::invokeMethod(dialog, "open")); QTRY_VERIFY(dialog->property("opened").toBool());
        QVERIFY(dialog->property("width").toReal() <= 732); QVERIFY(dialog->property("height").toReal() <= 512);
        auto scroll = dialog->findChild<QQuickItem *>("cacheScroll"); QVERIFY(scroll);
        QTRY_VERIFY(scroll->height() > 200); QVERIFY(scroll->width() <= dialog->property("width").toReal());
        auto media = dialog->findChild<QObject *>("cacheMediaSize"); QVERIFY(media); QVERIFY(!media->property("text").toString().isEmpty());
        auto limit = dialog->findChild<QObject *>("cacheLimit"); QVERIFY(limit);
        limit->setProperty("currentIndex", 0); QVERIFY(QMetaObject::invokeMethod(limit, "activated", Q_ARG(int, 0)));
        QCOMPARE(messenger.media()->limit(), 268435456LL);
        auto clear = dialog->findChild<QQuickItem *>("clearMediaCache"); QVERIFY(clear);
        QVERIFY(QMetaObject::invokeMethod(clear, "clicked"));
        QVERIFY(messenger.media()->clearing()); QVERIFY(!clear->isEnabled());
        auto progress = dialog->findChild<QQuickItem *>("cacheClearing"); QVERIFY(progress); QVERIFY(progress->isVisible());
        QTRY_VERIFY(!messenger.media()->clearing()); QVERIFY(clear->isEnabled()); QVERIFY(!progress->isVisible());
        QVERIFY(messenger.media()->cached(photo).isEmpty()); QVERIFY(messenger.media()->clearError().isEmpty());
        QCOMPARE(messenger.draftText(), "Черновик остаётся после очистки");
        if (qEnvironmentVariableIsSet("VKM_CACHE_SCREENSHOT")) {
            for (const bool dark : {false, true}) {
                messenger.setDarkTheme(dark); QTest::qWait(220);
                QVERIFY(window->grabWindow().save(qEnvironmentVariable("VKM_CACHE_SCREENSHOT") + (dark ? "-dark.png" : "-light.png")));
            }
        }
        auto flick = scroll->property("contentItem").value<QQuickItem *>(); QVERIFY(flick);
        QTRY_VERIFY(flick->property("contentHeight").toReal() > flick->height());
        flick->setProperty("contentY", flick->property("contentHeight").toReal() - flick->height());
        auto outgoing = dialog->findChild<QQuickItem *>("cacheOutgoingCount"); QVERIFY(outgoing);
        QTRY_VERIFY(outgoing->mapToItem(scroll, QPointF()).y() >= 0 && outgoing->mapToItem(scroll, QPointF()).y() < scroll->height());
        QVERIFY(QMetaObject::invokeMethod(dialog, "close")); QTRY_VERIFY(!dialog->property("visible").toBool());
        QCOMPARE(warnings.size(), 0);
    }

    void composerDoesNotSendDuringInputMethodComposition()
    {
        Messenger messenger;
        QQmlApplicationEngine engine; engine.rootContext()->setContextProperty("messenger", &messenger);
        engine.load(QUrl::fromLocalFile(QStringLiteral(NATIVE_QML_PATH))); QVERIFY(!engine.rootObjects().isEmpty());
        auto window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        messenger.startDemo(); window->requestActivate(); QTRY_VERIFY(window->isActive());
        auto composer = window->findChild<QQuickItem *>("composer"); QVERIFY(composer);
        auto send = window->findChild<QQuickItem *>("sendMessageButton"); QVERIFY(send);
        composer->forceActiveFocus(); composer->setProperty("text", "Начало ");
        const int count = messenger.messages()->rowCount();
        QInputMethodEvent preedit(QStringLiteral("未確定"), {}); QCoreApplication::sendEvent(composer, &preedit);
        QVERIFY(composer->property("inputMethodComposing").toBool()); QVERIFY(!send->isEnabled());
        QVERIFY(QMetaObject::invokeMethod(composer, "submitWithMode", Q_ARG(QVariant, true)));
        QCOMPARE(messenger.messages()->rowCount(), count);
        QTest::keyClick(window, Qt::Key_Return); QCOMPARE(messenger.messages()->rowCount(), count);
        QInputMethodEvent commit; commit.setCommitString(QStringLiteral("確認")); QCoreApplication::sendEvent(composer, &commit);
        QTRY_VERIFY(!composer->property("inputMethodComposing").toBool()); QVERIFY(send->isEnabled());
        QTest::keyClick(window, Qt::Key_Return, Qt::ShiftModifier);
        QCOMPARE(messenger.messages()->rowCount(), count); QVERIFY(composer->property("text").toString().contains('\n'));
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_COMPARE(messenger.messages()->rowCount(), count + 1);
        QVERIFY(messenger.messages()->get(count).value("text").toString().contains(QStringLiteral("確認")));
        QTRY_VERIFY(composer->property("text").toString().isEmpty());
    }

    void conversationSearchDebouncesAndSupportsKeyboard()
    {
        Messenger messenger; messenger.startDemo();
        QQmlApplicationEngine engine; engine.rootContext()->setContextProperty("messenger", &messenger);
        engine.load(QUrl::fromLocalFile(QStringLiteral(NATIVE_QML_PATH))); QVERIFY(!engine.rootObjects().isEmpty());
        auto window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        window->requestActivate(); QTRY_VERIFY(window->isActive());
        auto search = window->findChild<QQuickItem *>("conversationSearch"); QVERIFY(search);
        auto chats = window->findChild<QQuickItem *>("conversationList"); QVERIFY(chats);
        auto composer = window->findChild<QQuickItem *>("composer"); QVERIFY(composer);
        const auto originalPeer = messenger.selectedPeer();
        composer->setProperty("text", "Сохранённый черновик");
        search->forceActiveFocus();
        QSignalSpy filters(messenger.filteredConversations(), &ConversationFilterModel::filterChanged);
        for (const auto &text : {"А", "Ан", "Анна"}) search->setProperty("text", text);
        QCOMPARE(filters.count(), 0);
        QTRY_COMPARE(messenger.filteredConversations()->query(), QString("Анна"));
        QCOMPARE(filters.count(), 1);
        QCOMPARE(chats->property("count").toInt(), 1);
        QTest::keyClick(window, Qt::Key_Down);
        QVERIFY(chats->hasActiveFocus());
        QCOMPARE(messenger.selectedPeer(), originalPeer); // moving focus does not open a chat
        if (qEnvironmentVariableIsSet("VKM_KEYBOARD_SCREENSHOT")) {
            for (const bool dark : {true, false}) {
                messenger.setDarkTheme(dark); QTest::qWait(200);
                QVERIFY(window->grabWindow().save(qEnvironmentVariable("VKM_KEYBOARD_SCREENSHOT") + (dark ? "-dark.png" : "-light.png")));
            }
        }
        QTest::keyClick(window, Qt::Key_Return);
        QCOMPARE(messenger.selectedPeer(), QString("2"));
        QVERIFY(composer->hasActiveFocus());
        QVERIFY(QMetaObject::invokeMethod(window, "selectChat", Q_ARG(QVariant, originalPeer), Q_ARG(QVariant, "Команда")));
        QCOMPARE(composer->property("text").toString(), QString("Сохранённый черновик"));

        search->forceActiveFocus(); search->setProperty("text", "Команда");
        QTest::keyClick(window, Qt::Key_Return); // Enter flushes a pending search immediately
        QCOMPARE(messenger.filteredConversations()->query(), QString("Команда"));
        QCOMPARE(messenger.selectedPeer(), originalPeer);
        search->forceActiveFocus(); search->setProperty("text", "Не существует");
        QTest::keyClick(window, Qt::Key_Return);
        QCOMPARE(chats->property("count").toInt(), 0);
        QCOMPARE(messenger.selectedPeer(), originalPeer);
        QVERIFY(search->hasActiveFocus());
        QTest::keyClick(window, Qt::Key_Escape);
        QCOMPARE(messenger.filteredConversations()->query(), QString());
        QCOMPARE(chats->property("count").toInt(), 4); // clearing never waits for debounce
        QTest::keyClick(window, Qt::Key_Up);
        QVERIFY(chats->hasActiveFocus()); QCOMPARE(chats->property("currentIndex").toInt(), 3);
        QTest::keyClick(window, Qt::Key_Home); QCOMPARE(chats->property("currentIndex").toInt(), 0);
        QTest::keyClick(window, Qt::Key_Down); QCOMPARE(chats->property("currentIndex").toInt(), 1);
        QTest::keyClick(window, Qt::Key_End); QCOMPARE(chats->property("currentIndex").toInt(), 3);
        QTest::keyClick(window, Qt::Key_Escape); QVERIFY(search->hasActiveFocus());

        search->setProperty("text", "Ан");
        QInputMethodEvent preedit(QStringLiteral("на"), {});
        QCoreApplication::sendEvent(search, &preedit);
        QVERIFY(search->property("inputMethodComposing").toBool());
        QTest::qWait(200);
        QCOMPARE(messenger.filteredConversations()->query(), QString());
        QInputMethodEvent commit; commit.setCommitString(QStringLiteral("на"));
        QCoreApplication::sendEvent(search, &commit);
        QTRY_COMPARE(messenger.filteredConversations()->query(), search->property("text").toString());
    }

    void historyViewportKeepsReadBoundaryAndAnchor()
    {
        Messenger messenger; messenger.startDemo();
        QQmlApplicationEngine engine; engine.rootContext()->setContextProperty("messenger", &messenger);
        engine.load(QUrl::fromLocalFile(QStringLiteral(NATIVE_QML_PATH))); QVERIFY(!engine.rootObjects().isEmpty());
        auto window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        auto history = window->findChild<QQuickItem *>("historyList"); QVERIFY(history);
        QVariantList rows;
        constexpr qint64 first = 9007199254740993LL;
        for (int i = 0; i < 3000; ++i) rows.append(QVariantMap{{"id", QString::number(first + i)}, {"cmid", QString::number(first + i)}, {"text", i == 1000 ? QString("Длинное сообщение\n").repeated(80) : QString("Сообщение %1").arg(i)}, {"time", "12:00"}});
        rows.append(QVariantMap{{"id", "gap"}, {"gap", true}, {"cmid", QString::number(first + 3000)}});
        rows.append(QVariantMap{{"id", "local"}, {"local", true}, {"cmid", QString::number(first + 3001)}, {"text", "В очереди"}});
        messenger.messages()->replace(rows);
        QTest::qWait(150);
        QVariant read;
        QVERIFY(QMetaObject::invokeMethod(window, "visibleReadCmid", Q_RETURN_ARG(QVariant, read)));
        QCOMPARE(read.toString(), QString::number(first + 2999)); // gap and local outbox cannot advance the boundary
        window->setProperty("wasAtEnd", false);
        QVERIFY(QMetaObject::invokeMethod(history, "positionViewAtIndex", Q_ARG(int, 1000), Q_ARG(int, 0)));
        QTest::qWait(100);
        QVERIFY(QMetaObject::invokeMethod(window, "visibleReadCmid", Q_RETURN_ARG(QVariant, read)));
        QCOMPARE(read.toString(), QString::number(first + 1000)); // taller than the viewport
        history->setProperty("contentY", history->property("contentY").toReal() + 200);
        QTest::qWait(100);
        QVERIFY(QMetaObject::invokeMethod(window, "rememberGeometryAnchor"));
        QCOMPARE(window->property("geometryAnchor").toString(), QString::number(first + 1000));
        QCOMPARE(qRound(window->property("geometryOffset").toReal()), 200);
        QVERIFY(QMetaObject::invokeMethod(window, "visibleReadCmid", Q_RETURN_ARG(QVariant, read)));
        QCOMPARE(read.toString(), QString::number(first + 1000));

        QVERIFY(QMetaObject::invokeMethod(history, "positionViewAtIndex", Q_ARG(int, 1700), Q_ARG(int, 0)));
        QTest::qWait(100);
        QVERIFY(QMetaObject::invokeMethod(window, "rememberGeometryAnchor"));
        QCOMPARE(window->property("geometryAnchor").toString(), QString::number(first + 1700));
        QVariant visible;
        QVERIFY(QMetaObject::invokeMethod(window, "visibleHistoryRows", Q_RETURN_ARG(QVariant, visible)));
        const auto visibleRows = visible.value<QJSValue>().toVariant().toList();
        QVERIFY(!visibleRows.isEmpty()); QVERIFY(visibleRows.size() < 30);
        const auto content = history->property("contentItem").value<QQuickItem *>(); QVERIFY(content);
        QVERIFY(content->childItems().size() < 50);
        qInfo() << "3,002 messages," << content->childItems().size() << "created items," << visibleRows.size() << "visible rows";
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

    void historyTailCountsOnlyNewIncoming()
    {
        Messenger messenger;
        constexpr qint64 first = 9007199254740993LL;
        QVariantList rows;
        for (int i = 0; i < 10000; ++i)
            rows.append(QVariantMap{{"id", QString::number(first + i)}, {"cmid", QString::number(first + i)}});
        const auto boundary = QString::number(first + 9999);
        rows.append(QVariantMap{{"id", QString::number(first + 10000)}, {"cmid", QString::number(first + 10000)}, {"outgoing", true}});
        rows.append(QVariantMap{{"id", QString::number(first + 10001)}, {"cmid", QString::number(first + 10001)}});
        rows.append(QVariantMap{{"id", "gap"}, {"cmid", QString::number(first + 10002)}, {"gap", true}});
        rows.append(QVariantMap{{"id", QString::number(first + 10003)}, {"cmid", QString::number(first + 10003)}});
        rows.append(QVariantMap{{"id", "local"}, {"cmid", QString::number(first + 10004)}, {"local", true}});
        messenger.messages()->replace(rows);
        QCOMPARE(messenger.latestMessageCmid(), QString::number(first + 10003));
        QCOMPARE(messenger.newIncomingMessageCount(boundary), 2);
        QCOMPARE(messenger.newIncomingMessageCount(QString::number(first + 10001)), 1);
        QCOMPARE(messenger.newIncomingMessageCount(messenger.latestMessageCmid()), 0);
        for (const auto &invalid : {QString(), QString("local"), QString("-1"), QString("0")})
            QCOMPARE(messenger.newIncomingMessageCount(invalid), 0);
        messenger.messages()->replace({rows.last(), QVariantMap{{"id", "gap"}, {"gap", true}}});
        QVERIFY(messenger.latestMessageCmid().isEmpty());
        QCOMPARE(messenger.newIncomingMessageCount(boundary), 0);
        messenger.messages()->replace({}); QVERIFY(messenger.latestMessageCmid().isEmpty());
    }

    void historyNewMessageBadgeKeepsAnchorAndIgnoresOutbox()
    {
        Messenger messenger; messenger.startDemo();
        QQmlApplicationEngine engine; engine.rootContext()->setContextProperty("messenger", &messenger);
        QSignalSpy qmlWarnings(&engine, &QQmlEngine::warnings);
        engine.load(QUrl::fromLocalFile(QStringLiteral(NATIVE_QML_PATH))); QVERIFY(!engine.rootObjects().isEmpty());
        auto window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        window->resize(780, 560);
        auto history = window->findChild<QQuickItem *>("historyList"); QVERIFY(history);
        auto button = window->findChild<QQuickItem *>("jumpToEndButton"); QVERIFY(button);
        auto badge = button->findChild<QQuickItem *>("historyNewMessageBadge"); QVERIFY(badge);
        auto count = badge->findChild<QQuickItem *>("historyNewMessageCount"); QVERIFY(count);
        constexpr qint64 first = 9007199254740993LL;
        QVariantList rows;
        for (int i = 0; i < 3000; ++i)
            rows.append(QVariantMap{{"id", QString::number(first + i)}, {"cmid", QString::number(first + i)}, {"text", QString("Сообщение %1").arg(i)}, {"time", "12:00"}});
        rows.append(QVariantMap{{"id", "local:queued"}, {"local", true}, {"outgoing", true}, {"text", "В очереди"}, {"delivery", "queued"}, {"time", "12:01"}});
        messenger.messages()->replace(rows); QTest::qWait(100);
        QVERIFY(QMetaObject::invokeMethod(history, "movementStarted"));
        QVERIFY(QMetaObject::invokeMethod(history, "positionViewAtIndex", Q_ARG(int, 1500), Q_ARG(int, 0)));
        history->setProperty("contentY", history->property("contentY").toReal() + 15);
        QTRY_VERIFY(button->isVisible()); QVERIFY(!badge->isVisible());
        QVERIFY(QMetaObject::invokeMethod(window, "rememberGeometryAnchor"));
        const auto anchor = window->property("geometryAnchor").toString(); QVERIFY(!anchor.isEmpty());
        const auto offset = window->property("geometryOffset").toReal();
        const auto apply = [&] { messenger.historyAboutToChange(); messenger.messages()->reconcile(rows); messenger.historyChanged(); };
        rows.insert(rows.size() - 1, QVariantMap{{"id", QString::number(first + 3000)}, {"cmid", QString::number(first + 3000)}, {"outgoing", true}, {"text", "Отправлено с другого устройства"}, {"time", "12:01"}});
        rows.insert(rows.size() - 1, QVariantMap{{"id", QString::number(first + 3001)}, {"cmid", QString::number(first + 3001)}, {"text", "Новое входящее"}, {"time", "12:01"}});
        rows.insert(rows.size() - 1, QVariantMap{{"id", "gap:fixture"}, {"gap", true}, {"cmid", QString::number(first + 3002)}});
        rows.insert(rows.size() - 1, QVariantMap{{"id", QString::number(first + 3003)}, {"cmid", QString::number(first + 3003)}, {"text", "Ещё одно входящее"}, {"time", "12:01"}});
        apply(); QTRY_COMPARE(window->property("newMessages").toInt(), 2);
        QTRY_VERIFY(badge->isVisible()); QCOMPARE(count->property("text").toString(), "2");
        auto older = rows[0].toMap(); older["text"] = "Исправлено"; rows[0] = older;
        auto pending = rows.last().toMap(); pending["delivery"] = "failed"; rows.last() = pending;
        rows.prepend(QVariantMap{{"id", QString::number(first - 1)}, {"cmid", QString::number(first - 1)}, {"text", "Предыдущая история"}, {"time", "11:59"}});
        apply(); QTest::qWait(60); QCOMPARE(window->property("newMessages").toInt(), 2);
        QVERIFY(QMetaObject::invokeMethod(window, "rememberGeometryAnchor"));
        QCOMPARE(window->property("geometryAnchor").toString(), anchor);
        QVERIFY(qAbs(window->property("geometryOffset").toReal() - offset) <= 1);
        for (int i = 0; i < 123; ++i)
            rows.insert(rows.size() - 1, QVariantMap{{"id", QString::number(first + 3004 + i)}, {"cmid", QString::number(first + 3004 + i)}, {"text", "Новое сообщение"}, {"time", "12:02"}});
        apply(); QTRY_COMPARE(window->property("newMessages").toInt(), 125);
        QCOMPARE(count->property("text").toString(), "99+");
        const auto accessible = QAccessible::queryAccessibleInterface(button); QVERIFY(accessible);
        QCOMPARE(accessible->text(QAccessible::Name), QString("К последним сообщениям. Новых сообщений: 125"));
        QCOMPARE(button->width(), 48); QCOMPARE(button->height(), 48);
        for (bool dark : {false, true}) {
            messenger.setDarkTheme(dark); QTest::qWait(30);
            if (qEnvironmentVariableIsSet("VKM_HISTORY_BADGE_SCREENSHOT"))
                QVERIFY(window->grabWindow().save(qEnvironmentVariable("VKM_HISTORY_BADGE_SCREENSHOT") + (dark ? "-dark.png" : "-light.png")));
        }
        window->requestActivate(); QTRY_VERIFY(window->isActive());
        button->forceActiveFocus(Qt::TabFocusReason); QTRY_VERIFY(button->property("visualFocus").toBool());
        QTest::keyClick(window, Qt::Key_Space);
        QTRY_COMPARE(window->property("newMessages").toInt(), 0);
        QTRY_VERIFY(!button->isVisible()); QVERIFY(!badge->isVisible());
        const auto content = history->property("contentItem").value<QQuickItem *>(); QVERIFY(content);
        QVERIFY(content->childItems().size() < 50);
        QCOMPARE(qmlWarnings.count(), 0);
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

    void searchAndComposerControlsWorkInBothThemes();
    void scrollingShowsDateThenFadesWithoutChangingLayout();
    void senderProfileOpensWithoutLeavingGroupAndWritesToPerson();
    void missingGroupAvatarsLoadInOneRequest();

    void profileUsesIndependentResultsAndKeepsDraft()
    {
        TokenNetwork network;
        network.responses["messages.getDiff"] = {R"({"response":{"server_version":"500","credentials":{"server_lp":"lp.test/im","key":"key","ts":"100"},"conversations_info":[{"conversation":{"peer":{"id":2000000077},"chat_settings":{"title":"Тестовая беседа"}},"message":[]}]}})"};
        network.responses["messages.getConversationMembers"] = {R"({"response":{"count":1,"items":[{"member_id":42,"is_admin":true}],"profiles":[{"id":42,"first_name":"Тестовый","last_name":"Участник","online":1,"last_seen":{"time":1700000000}}]}})"};
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
        QCOMPARE(messenger.profileActions()->memberCount(), 1);
        QVERIFY(messenger.profileActions()->results().first().toMap().value("online").toBool());
        QVERIFY(messenger.profileActions()->results().first().toMap().value("is_admin").toBool());
        QCOMPARE(messenger.profileActions()->results().first().toMap().value("last_seen").toMap().value("time").toLongLong(), 1700000000LL);
        if (qEnvironmentVariableIsSet("VKM_PROFILE_SCREENSHOT")) QVERIFY(window->grabWindow().save(qEnvironmentVariable("VKM_PROFILE_SCREENSHOT")));
        messenger.actions()->reset();
        QCOMPARE(messenger.profileActions()->results().size(), 1);
        QCOMPARE(messenger.draftText(), QString("Несохранённый ввод"));
        profile->setProperty("section", 1);
        QVERIFY(QMetaObject::invokeMethod(profile, "loadSection"));
        QTRY_VERIFY(!messenger.profileActions()->busy());
        QCOMPARE(messenger.profileActions()->results().size(), 0);
        QCOMPARE(profile->property("memberCount").toInt(), 1);
        QVERIFY(QMetaObject::invokeMethod(profile, "close"));
        QTRY_VERIFY(!profile->property("opened").toBool());
        QCOMPARE(messenger.selectedPeer(), QString("2000000077"));
        QCOMPARE(messenger.draftText(), QString("Несохранённый ввод"));
    }

    void profileDocksAndFiltersLoadedMembers()
    {
        TokenNetwork network;
        network.responses["messages.getDiff"] = {R"({"response":{"server_version":"500","credentials":{"server_lp":"lp.test/im","key":"key","ts":"100"},"conversations_info":[{"conversation":{"peer":{"id":2000000077},"chat_settings":{"title":"Тестовая беседа"}},"message":[]}]}})"};
        QJsonArray members, profiles;
        for (int i = 0; i < 200; ++i) {
            members.append(QJsonObject{{"member_id", 100 + i}});
            profiles.append(QJsonObject{{"id", 100 + i}, {"first_name", i == 199 ? "Нужный" : "Участник"}, {"last_name", QString::number(i)}});
        }
        network.responses["messages.getConversationMembers"] = {QJsonDocument(QJsonObject{{"response", QJsonObject{{"count", 201}, {"items", members}, {"profiles", profiles}}}}).toJson()};
        QJsonArray messages;
        for (int i = 0; i < 100; ++i) messages.append(QJsonObject{{"conversation_message_id", i + 1}, {"peer_id", 2000000077}, {"from_id", 100}, {"date", 1700000000 + i * 60}, {"text", QString("Сообщение %1. ").arg(i).repeated(10)}});
        network.responses["messages.getDiffContent"] = {QJsonDocument(QJsonObject{{"response", QJsonObject{{"items", QJsonArray{QJsonObject{{"peer_id", 2000000077}, {"messages", messages}}}}}}}).toJson()};
        Messenger messenger(nullptr, &network);
        QQmlApplicationEngine engine; engine.rootContext()->setContextProperty("messenger", &messenger);
        engine.load(QUrl::fromLocalFile(QStringLiteral(NATIVE_QML_PATH))); QVERIFY(!engine.rootObjects().isEmpty());
        auto window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        window->resize(1440, 900); window->requestActivate();
        messenger.loginWithToken("test-token"); QTRY_COMPARE(messenger.conversations()->rowCount(), 1);
        messenger.selectConversation("2000000077", "Тестовая беседа");
        QTRY_COMPARE(messenger.messages()->rowCount(), 100); QTRY_VERIFY(!messenger.loading());
        auto composer = window->findChild<QQuickItem *>("composer"); QVERIFY(composer);
        composer->setProperty("text", "Черновик рядом с профилем");
        auto popup = window->findChild<QObject *>("conversationProfile"); QVERIFY(popup);
        auto history = window->findChild<QQuickItem *>("historyList"); QVERIFY(history);
        auto column = window->findChild<QQuickItem *>("chatColumn"); QVERIFY(column);
        auto results = popup->findChild<QQuickItem *>("profileResults"); QVERIFY(results);
        auto empty = popup->findChild<QQuickItem *>("profileEmptyState"); QVERIFY(empty);
        QTest::qWait(150);
        window->setProperty("wasAtEnd", false);
        QVERIFY(QMetaObject::invokeMethod(history, "positionViewAtIndex", Q_ARG(int, 40), Q_ARG(int, 0)));
        history->setProperty("contentY", history->property("contentY").toReal() + 15);
        QTest::qWait(100);
        QVERIFY(QMetaObject::invokeMethod(window, "rememberGeometryAnchor"));
        const auto anchor = window->property("geometryAnchor").toString();
        const auto offset = window->property("geometryOffset").toReal();
        QCOMPARE(anchor, QString("41")); QVERIFY(qAbs(offset - 15) <= 1);
        const auto fullWidth = history->width();
        QVERIFY(QMetaObject::invokeMethod(popup, "present", Q_ARG(QVariant, QVariant::fromValue(window->contentItem()))));
        QTRY_VERIFY(popup->property("opened").toBool());
        QTRY_VERIFY(popup->property("docked").toBool());
        QTRY_COMPARE(results->property("count").toInt(), 200);
        QTRY_COMPARE(qRound(history->width()), qRound(fullWidth - popup->property("width").toReal()));
        QVERIFY(column->mapToScene(QPointF(column->width(), 0)).x() <= window->width() - popup->property("width").toReal() + 1);
        QTRY_COMPARE(window->property("geometryAnchor").toString(), anchor);
        QTRY_VERIFY(qAbs(window->property("geometryOffset").toReal() - offset) <= 1);
        popup->setProperty("query", "  НУЖНЫЙ  ");
        QTRY_COMPARE(results->property("count").toInt(), 1);
        QTRY_VERIFY(results->property("contentHeight").toReal() < 1500);
        QVERIFY(!empty->isVisible());
        popup->setProperty("query", "Не существует");
        QTRY_COMPARE(results->property("count").toInt(), 0);
        QTRY_VERIFY(empty->isVisible());
        QCOMPARE(empty->property("text").toString(), QString("Ничего не найдено в загруженном списке"));
        QVERIFY(messenger.profileActions()->hasMore());
        auto clear = popup->findChild<QObject *>("clearProfileSearch"); QVERIFY(clear);
        QVERIFY(QMetaObject::invokeMethod(clear, "clicked"));
        QTRY_COMPARE(results->property("count").toInt(), 200);
        popup->setProperty("query", "Нужный");
        auto search = popup->findChild<QQuickItem *>("profileSearch"); QVERIFY(search); search->forceActiveFocus();
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_COMPARE(results->property("count").toInt(), 200);
        QVERIFY(popup->property("opened").toBool());
        if (qEnvironmentVariableIsSet("VKM_DOCKED_PROFILE_SCREENSHOT")) {
            for (const bool dark : {true, false}) {
                messenger.setDarkTheme(dark); QTest::qWait(200);
                QVERIFY(window->grabWindow().save(qEnvironmentVariable("VKM_DOCKED_PROFILE_SCREENSHOT") + (dark ? "-dark.png" : "-light.png")));
            }
        }
        window->resize(980, 700); QTest::qWait(150);
        QVERIFY(!popup->property("docked").toBool());
        QVERIFY(history->width() > 650); // narrow windows retain the overlay instead of squeezing the chat
        QCOMPARE(messenger.draftText(), QString("Черновик рядом с профилем"));
        QVERIFY(QMetaObject::invokeMethod(popup, "close"));
        QTRY_VERIFY(!popup->property("visible").toBool());
        QTRY_COMPARE(results->property("count").toInt(), 0);
        QVERIFY(messenger.profileActions()->results().isEmpty());
    }

    void profileCreatesOnlyCurrentSectionAndDebouncesMemberSearch()
    {
        QImage image(64, 64, QImage::Format_RGB32); image.fill(QColor("#71aaeb"));
        QByteArray png; QBuffer buffer(&png); QVERIFY(buffer.open(QIODevice::WriteOnly)); QVERIFY(image.save(&buffer, "PNG"));
        TokenNetwork network;
        network.responses["messages.getDiff"] = {R"({"response":{"server_version":"500","credentials":{"server_lp":"lp.test/im","key":"key","ts":"100"},"conversations_info":[{"conversation":{"peer":{"id":2000000077},"chat_settings":{"title":"Команда проекта"}},"message":[]}]}})"};
        QJsonArray members, profiles, photos;
        for (int i = 0; i < 200; ++i) {
            members.append(QJsonObject{{"member_id", 1000 + i}, {"is_admin", i == 0}});
            profiles.append(QJsonObject{{"id", 1000 + i}, {"first_name", i == 199 ? "Нужный" : "Участник"}, {"last_name", QString::number(i)}, {"photo_100", QString("https://assets.test/profile-avatar-%1").arg(i)}, {"online", 1}});
            network.responses[QString("profile-avatar-%1").arg(i)] = {png};
        }
        for (int i = 0; i < 50; ++i) {
            const auto url = QString("https://assets.test/profile-photo-%1").arg(i);
            const QJsonArray sizes{QJsonObject{{"url", url}, {"width", 360}, {"height", 360}}};
            const QJsonObject photo{{"text", QString("Фотография %1").arg(i + 1)}, {"sizes", sizes}};
            photos.append(QJsonObject{{"cmid", i + 1}, {"attachment", QJsonObject{{"type", "photo"}, {"photo", photo}}}});
            network.responses[QString("profile-photo-%1").arg(i)] = {png};
        }
        network.responses["messages.getConversationMembers"] = {QJsonDocument(QJsonObject{{"response", QJsonObject{{"count", 200}, {"items", members}, {"profiles", profiles}}}}).toJson()};
        const QJsonArray links{QJsonObject{{"cmid", 51}, {"attachment", QJsonObject{{"type", "link"}, {"link", QJsonObject{{"url", "https://example.test"}, {"title", "План проекта"}, {"description", "Задачи команды"}}}}}}};
        network.responses["messages.getHistoryAttachments"] = {
            QJsonDocument(QJsonObject{{"response", QJsonObject{{"items", photos}}}}).toJson(),
            QJsonDocument(QJsonObject{{"response", QJsonObject{{"items", links}}}}).toJson(), R"({"response":{"items":[]}})"};
        Messenger messenger(nullptr, &network); messenger.setReduceMotion(true);
        QQmlApplicationEngine engine; engine.rootContext()->setContextProperty("messenger", &messenger);
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        engine.load(QUrl::fromLocalFile(QStringLiteral(NATIVE_QML_PATH))); QVERIFY(!engine.rootObjects().isEmpty());
        auto window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        window->resize(780, 560); window->requestActivate(); QTRY_VERIFY(window->isActive());
        messenger.loginWithToken("test-token"); QTRY_COMPARE(messenger.conversations()->rowCount(), 1);
        messenger.selectConversation("2000000077", "Команда проекта"); messenger.setDraftText("2000000077", "Черновик команды");
        auto panel = window->findChild<QObject *>("conversationProfile"); QVERIFY(panel);
        auto results = panel->findChild<QQuickItem *>("profileResults"); QVERIFY(results);
        auto search = panel->findChild<QQuickItem *>("profileSearch"); QVERIFY(search);
        auto navigation = panel->findChild<QQuickItem *>("profileMemberNavigation"); QVERIFY(navigation);
        const auto named = [&](const QString &name) {
            QList<QQuickItem *> items;
            const auto visit = [&](auto &&self, QQuickItem *item) -> void { if (item->objectName() == name) items.append(item); for (auto child : item->childItems()) self(self, child); };
            visit(visit, results); return items;
        };
        QVERIFY(QMetaObject::invokeMethod(panel, "present", Q_ARG(QVariant, QVariant::fromValue(window->contentItem()))));
        QTRY_COMPARE(results->property("count").toInt(), 200);
        QTRY_VERIFY(!named("profileMemberButton").isEmpty());
        QVERIFY(named("profileMemberButton").size() < 35);
        QVERIFY(named("profileAttachmentContent").isEmpty()); QVERIFY(named("profilePhotoThumbnail").isEmpty());
        const auto avatarRequests = [&] { return std::count_if(network.requests.begin(), network.requests.end(), [](const auto &request) { return request.first.path().contains("profile-avatar-"); }); };
        QTest::qWait(100); QVERIFY(avatarRequests() < 40);
        QSignalSpy queries(panel, SIGNAL(queryChanged()));
        search->forceActiveFocus();
        for (const auto text : {"Н", "Ну", "Нужный"}) { search->setProperty("text", text); QVERIFY(QMetaObject::invokeMethod(search, "textEdited")); }
        QCOMPARE(queries.size(), 0); QCOMPARE(results->property("count").toInt(), 200);
        QTRY_COMPARE(panel->property("query").toString(), "Нужный"); QTRY_COMPARE(results->property("count").toInt(), 1); QCOMPARE(queries.size(), 1);
        search->setProperty("text", "Участник");
        QInputMethodEvent preedit(QString::fromUtf8("候"), {}); QCoreApplication::sendEvent(search, &preedit);
        QVERIFY(search->property("inputMethodComposing").toBool());
        QVERIFY(QMetaObject::invokeMethod(search, "textEdited"));
        QTest::qWait(200); QCOMPARE(panel->property("query").toString(), "Нужный");
        QInputMethodEvent commit; commit.setCommitString(" "); QCoreApplication::sendEvent(search, &commit);
        QTRY_VERIFY(!search->property("inputMethodComposing").toBool());
        QTRY_COMPARE(panel->property("query").toString(), search->property("text").toString());
        QVERIFY(QMetaObject::invokeMethod(panel->findChild<QObject *>("clearProfileSearch"), "clicked"));
        QCOMPARE(panel->property("query").toString(), ""); QTRY_COMPARE(results->property("count").toInt(), 200);
        QTest::keyClick(window, Qt::Key_Down); QTRY_VERIFY(navigation->hasActiveFocus()); QVERIFY(!search->hasActiveFocus()); QCOMPARE(results->property("currentIndex").toInt(), 0);
        QTest::keyClick(window, Qt::Key_End); QCOMPARE(results->property("currentIndex").toInt(), 199);
        QTest::keyClick(window, Qt::Key_Home); QCOMPARE(results->property("currentIndex").toInt(), 0);
        QTest::keyClick(window, Qt::Key_Escape); QTRY_VERIFY(search->hasActiveFocus()); QVERIFY(panel->property("opened").toBool());
        if (qEnvironmentVariableIsSet("VKM_PROFILE_MEMBERS_SCREENSHOT")) {
            QVERIFY(QMetaObject::invokeMethod(results, "positionViewAtIndex", Q_ARG(int, 0), Q_ARG(int, 0)));
            QVERIFY(QMetaObject::invokeMethod(panel, "focusMembers", Q_ARG(QVariant, 1)));
            for (const bool dark : {false, true}) {
                messenger.setDarkTheme(dark); QTest::qWait(80);
                QVERIFY(window->grabWindow().save(qEnvironmentVariable("VKM_PROFILE_MEMBERS_SCREENSHOT") + (dark ? "-dark.png" : "-light.png")));
            }
            search->forceActiveFocus();
        }
        panel->setProperty("section", 1); QVERIFY(QMetaObject::invokeMethod(panel, "loadSection"));
        QTRY_COMPARE(results->property("count").toInt(), 17);
        QTRY_VERIFY(!named("profilePhotoThumbnail").isEmpty());
        QVERIFY(named("profileMemberButton").isEmpty()); QVERIFY(named("profileMemberMenuButton").isEmpty()); QVERIFY(named("profileAttachmentContent").isEmpty());
        auto photo = named("profilePhotoThumbnail").first(); QTRY_VERIFY(photo->height() > 100); QVERIFY(photo->width() > 100);
        panel->setProperty("section", 4); QVERIFY(QMetaObject::invokeMethod(panel, "loadSection"));
        QTRY_COMPARE(results->property("count").toInt(), 1); QTRY_VERIFY(!named("profileAttachmentContent").isEmpty());
        QVERIFY(named("profileMemberButton").isEmpty()); QVERIFY(named("profilePhotoThumbnail").isEmpty());
        QVERIFY(named("profileAttachmentContent").first()->height() > 0);
        network.responses["messages.getConversationMembers"] = {QJsonDocument(QJsonObject{{"response", QJsonObject{{"count", 200}, {"items", members}, {"profiles", profiles}}}}).toJson()};
        panel->setProperty("section", 0); QVERIFY(QMetaObject::invokeMethod(panel, "loadSection"));
        QTRY_COMPARE(results->property("count").toInt(), 200);
        search->forceActiveFocus(); search->setProperty("text", "Нужный"); QVERIFY(QMetaObject::invokeMethod(search, "textEdited"));
        QTest::keyClick(window, Qt::Key_Down); QTRY_COMPARE(results->property("count").toInt(), 1); QTRY_VERIFY(navigation->hasActiveFocus());
        QTest::keyClick(window, Qt::Key_Return); QTRY_VERIFY(panel->property("person").toBool()); QCOMPARE(panel->property("peer").toString(), "1199");
        QCOMPARE(messenger.selectedPeer(), "2000000077"); QCOMPARE(messenger.draftText(), "Черновик команды");
        QVERIFY(QMetaObject::invokeMethod(panel, "close")); QTRY_COMPARE(results->property("count").toInt(), 0);
        QCOMPARE(warnings.size(), 0);
    }

    void cachedAvatarLoadsOnDemandAndReusesCache()
    {
        QImage image(64, 64, QImage::Format_RGB32); image.fill(Qt::blue);
        QByteArray png; QBuffer buffer(&png); QVERIFY(buffer.open(QIODevice::WriteOnly)); QVERIFY(image.save(&buffer, "PNG"));
        TokenNetwork network; network.responses["avatar-on-demand"] = {png};
        Messenger messenger(nullptr, &network); messenger.startDemo();
        QQmlEngine engine; engine.rootContext()->setContextProperty("messenger", &messenger);
        QQmlComponent component(&engine, QUrl::fromLocalFile(QFileInfo(QStringLiteral(NATIVE_QML_PATH)).dir().filePath("CachedAvatar.qml")));
        std::unique_ptr<QObject> avatar(component.createWithInitialProperties({{"active", false}, {"remoteSource", QUrl("https://assets.test/avatar-on-demand")}}));
        QVERIFY2(avatar, qPrintable(component.errorString()));
        QTest::qWait(50); QVERIFY(network.replies["avatar-on-demand"].isEmpty());
        avatar->setProperty("active", true); QTRY_COMPARE(avatar->property("status").toInt(), 1);
        QCOMPARE(network.replies["avatar-on-demand"].size(), 1);
        avatar->setProperty("active", false); QVERIFY(avatar->property("source").toUrl().isEmpty()); QCOMPARE(avatar->property("status").toInt(), 0);
        avatar->setProperty("active", true); QTRY_COMPARE(avatar->property("status").toInt(), 1);
        QCOMPARE(network.replies["avatar-on-demand"].size(), 1);
    }

    void stickerImagesLoadOnDemandAndRecover()
    {
        TokenNetwork network;
        network.responses["lazy-raster"] = {"#offline"}; network.responses["lazy-lottie"] = {"#offline"};
        Messenger messenger(nullptr, &network); messenger.startDemo();
        QQmlEngine engine; engine.rootContext()->setContextProperty("messenger", &messenger);
        QQmlComponent component(&engine, QUrl::fromLocalFile(QFileInfo(QStringLiteral(NATIVE_QML_PATH)).dir().filePath("AttachmentView.qml")));
        QVariantList attachments{
            QVariantMap{{"type", "sticker"}, {"sticker", QVariantMap{{"images", QVariantList{QVariantMap{{"url", "https://assets.test/lazy-raster"}}}}}}},
            QVariantMap{{"type", "sticker"}, {"sticker", QVariantMap{{"animation_url", "https://assets.test/lazy-lottie"}}}}
        };
        std::unique_ptr<QObject> object(component.createWithInitialProperties({{"loadImages", false}, {"attachments", attachments}}));
        QVERIFY2(object, qPrintable(component.errorString()));
        auto view = qobject_cast<QQuickItem *>(object.get()); QVERIFY(view);
        QQuickWindow window; view->setParentItem(window.contentItem()); view->setWidth(400); view->setHeight(440); window.resize(400, 440); window.show();
        const auto visualItems = [&](const QString &name) {
            QList<QQuickItem *> items;
            const auto visit = [&](auto &&self, QQuickItem *item) -> void { if (item->objectName() == name) items.append(item); for (auto child : item->childItems()) self(self, child); };
            visit(visit, view); return items;
        };
        QTest::qWait(100);
        QVERIFY(network.requests.isEmpty());
        view->setProperty("loadImages", true);
        QTRY_COMPARE(network.replies["lazy-raster"].size(), 1); QTRY_COMPARE(network.replies["lazy-lottie"].size(), 1);
        const auto retries = visualItems("retrySticker"); QCOMPARE(retries.size(), 2);
        QTRY_VERIFY(retries[0]->isVisible() && retries[1]->isVisible());
        QImage image(32, 32, QImage::Format_ARGB32_Premultiplied); image.fill(Qt::blue);
        QByteArray png; QBuffer buffer(&png); QVERIFY(buffer.open(QIODevice::WriteOnly)); QVERIFY(image.save(&buffer, "PNG"));
        QFile lottie(QFINDTESTDATA("fixtures/sticker.json")); QVERIFY(lottie.open(QIODevice::ReadOnly));
        network.responses["lazy-raster"] = {"invalid-image", png}; network.responses["lazy-lottie"] = {lottie.readAll()};
        for (auto retry : retries) QVERIFY(QMetaObject::invokeMethod(retry, "clicked"));
        auto animations = view->findChildren<LottieSticker *>();
        // Repeater delegates belong to the visual tree, so inspect it for native animation items.
        if (animations.isEmpty()) {
            const auto visit = [&](auto &&self, QQuickItem *item) -> void { if (auto animation = qobject_cast<LottieSticker *>(item)) animations.append(animation); for (auto child : item->childItems()) self(self, child); };
            visit(visit, view);
        }
        QCOMPARE(animations.size(), 2);
        QTRY_VERIFY(std::any_of(animations.begin(), animations.end(), [](auto animation) { return animation->ready(); }));
        const auto rasters = visualItems("attachmentImage"); QCOMPARE(rasters.size(), 2);
        QTRY_VERIFY(std::any_of(rasters.begin(), rasters.end(), [](auto raster) { return raster->property("status").toInt() == 3; }));
        for (auto retry : retries) if (retry->isVisible()) QVERIFY(QMetaObject::invokeMethod(retry, "clicked"));
        QTRY_VERIFY(!retries[0]->isVisible() && !retries[1]->isVisible());
        QTRY_VERIFY(std::any_of(rasters.begin(), rasters.end(), [](auto raster) { return raster->property("status").toInt() == 1; }));
        view->setProperty("loadImages", false);
        for (auto raster : rasters) QVERIFY(raster->property("source").toUrl().isEmpty());
        for (auto animation : animations) { QVERIFY(animation->source().isEmpty()); QVERIFY(!animation->ready()); }
        view->setProperty("loadImages", true);
        QTRY_VERIFY(std::any_of(animations.begin(), animations.end(), [](auto animation) { return animation->ready(); }));
        QCOMPARE(network.replies["lazy-raster"].size(), 3); QCOMPARE(network.replies["lazy-lottie"].size(), 2); // reuse disk cache
        auto animation = *std::find_if(animations.begin(), animations.end(), [](auto animation) { return animation->ready(); });
        QSignalSpy sourceChanges(animation, &LottieSticker::sourceChanged);
        network.responses["unrelated-image"] = {png};
        messenger.media()->source(QUrl("https://assets.test/unrelated-image"));
        QTRY_VERIFY(!messenger.media()->cached(QUrl("https://assets.test/unrelated-image")).isEmpty());
        QCOMPARE(sourceChanges.count(), 0);
    }

    void personalProfileShowsIdentityAndClosesOnPeerChange()
    {
        TokenNetwork network;
        network.responses["messages.getDiff"] = {R"({"response":{"server_version":"500","credentials":{"server_lp":"lp.test/im","key":"key","ts":"100"},"conversations_info":[{"conversation":{"peer":{"id":77},"push_settings":{"disabled_forever":true}},"message":[]}],"profiles":[{"id":77,"first_name":"Анна","last_name":"Тест","domain":"anna_test","last_seen":{"time":1700000000}}]}})"};
        network.responses["messages.getHistoryAttachments"] = {R"({"response":{"items":[{"cmid":3,"attachment":{"type":"photo","photo":{"id":1,"owner_id":77,"sizes":[]}}},{"cmid":4,"attachment":{"type":"photo","photo":{"id":2,"owner_id":77,"sizes":[]}}}]}})"};
        Messenger messenger(nullptr, &network);
        QQmlApplicationEngine engine; engine.rootContext()->setContextProperty("messenger", &messenger);
        engine.load(QUrl::fromLocalFile(QStringLiteral(NATIVE_QML_PATH))); QVERIFY(!engine.rootObjects().isEmpty());
        auto window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        messenger.loginWithToken("test-token"); QTRY_VERIFY(messenger.connected());
        messenger.selectConversation("77", "Анна Тест");
        QTRY_COMPARE(messenger.conversationProfile().value("domain").toString(), QString("anna_test"));
        QVERIFY(messenger.conversationProfile().value("muted").toBool());
        messenger.setDraftText("77", "Черновик");
        auto profile = window->findChild<QObject *>("conversationProfile"); QVERIFY(profile);
        QVERIFY(QMetaObject::invokeMethod(profile, "present", Q_ARG(QVariant, QVariant::fromValue(window->contentItem()))));
        QTRY_VERIFY(profile->property("opened").toBool());
        QCOMPARE(profile->property("section").toInt(), 1);
        QVERIFY(!profile->property("chat").toBool());
        QTRY_COMPARE(messenger.profileActions()->results().size(), 2);
        if (qEnvironmentVariableIsSet("VKM_PERSONAL_PROFILE_SCREENSHOT")) QVERIFY(window->grabWindow().save(qEnvironmentVariable("VKM_PERSONAL_PROFILE_SCREENSHOT")));
        QCOMPARE(messenger.draftText(), QString("Черновик"));
        messenger.selectConversation("88", "Другой собеседник");
        QTRY_VERIFY(!profile->property("opened").toBool());
        QTRY_VERIFY(messenger.profileActions()->results().isEmpty());
    }

    void menusStayReadableInBothThemes()
    {
        Messenger messenger;
        const bool previousDark = messenger.darkTheme();
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("messenger", &messenger);
        engine.load(QUrl::fromLocalFile(QStringLiteral(NATIVE_QML_PATH)));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        messenger.startDemo(); window->requestActivate();
        QTRY_VERIFY(window->isActive());
        auto menu = window->findChild<QObject *>("navigationMenu"); QVERIFY(menu);
        for (bool dark : {true, false}) {
            messenger.setDarkTheme(dark);
            QVERIFY(QMetaObject::invokeMethod(menu, "open"));
            QTRY_VERIFY(menu->property("opened").toBool());
            auto content = menu->property("contentItem").value<QQuickItem *>(); QVERIFY(content);
            auto listContent = content->property("contentItem").value<QQuickItem *>(); QVERIFY(listContent);
            QList<QQuickItem *> entries;
            for (auto item : listContent->childItems()) if (item->property("text").isValid()) entries.append(item);
            QCOMPARE(entries.size(), 5);
            auto item = entries.at(1);
            const auto center = item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
            QTest::mouseMove(window, center);
            QTRY_VERIFY(item->property("highlighted").toBool());
            auto background = item->property("background").value<QQuickItem *>(); QVERIFY(background);
            QTRY_COMPARE(background->property("color").value<QColor>(), QColor(dark ? "#2c2c2c" : "#f0f2f5"));
            auto label = item->property("contentItem").value<QQuickItem *>(); QVERIFY(label);
            QCOMPARE(label->property("color").value<QColor>(), QColor(dark ? "#e1e3e6" : "#222222"));
            if (qEnvironmentVariableIsSet("VKM_MENU_SCREENSHOT")) QVERIFY(window->grabWindow().save(qEnvironmentVariable("VKM_MENU_SCREENSHOT") + (dark ? "-dark.png" : "-light.png")));
            QTest::keyClick(window, Qt::Key_Down);
            QCOMPARE(menu->property("currentIndex").toInt(), 2);
            QTest::keyClick(window, Qt::Key_Escape);
            QTRY_VERIFY(!menu->property("visible").toBool());
        }
        messenger.setDarkTheme(previousDark);
    }

    void conversationHoverFollowsPointerWithoutSelecting()
    {
        Messenger messenger;
        const bool reducedMotion = messenger.reduceMotion();
        messenger.setReduceMotion(false);
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("messenger", &messenger);
        engine.load(QUrl::fromLocalFile(QStringLiteral(NATIVE_QML_PATH)));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        messenger.startDemo();
        window->requestActivate();
        QTRY_VERIFY(window->isActive());
        auto chats = window->findChild<QQuickItem *>("conversationList"); QVERIFY(chats);
        auto highlight = window->findChild<QQuickItem *>("conversationHover"); QVERIFY(highlight);
        const auto point = [&](int row) { return chats->mapToScene(QPointF(chats->width() / 2, row * 66 + 33)).toPoint(); };
        QTest::mouseMove(window, point(1));
        QTRY_COMPARE(highlight->opacity(), 1.0);
        QTRY_COMPARE(highlight->y(), 66.0);
        QTest::mouseMove(window, point(3));
        QTest::qWait(40);
        QVERIFY(highlight->y() > 66.0 && highlight->y() < 198.0);
        // Reverse direction before the first transition finishes.
        QTest::mouseMove(window, point(2));
        QTRY_COMPARE(highlight->y(), 132.0);
        if (qEnvironmentVariableIsSet("VKM_HOVER_SCREENSHOT")) QVERIFY(window->grabWindow().save(qEnvironmentVariable("VKM_HOVER_SCREENSHOT")));
        QCOMPARE(messenger.selectedPeer(), QString("1"));
        messenger.setReduceMotion(true);
        QTest::mouseMove(window, point(1));
        QCOMPARE(highlight->y(), 66.0);
        QTest::mouseMove(window, QPoint(window->width() - 10, 100));
        QCOMPARE(highlight->opacity(), 0.0);
        messenger.setReduceMotion(reducedMotion);
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

    void conversationHeaderShowsCurrentAvatarAndTracksUpdates()
    {
        QImage image(32, 32, QImage::Format_ARGB32_Premultiplied); image.fill(Qt::blue);
        QByteArray png; QBuffer buffer(&png); QVERIFY(buffer.open(QIODevice::WriteOnly)); QVERIFY(image.save(&buffer, "PNG"));
        TokenNetwork network;
        network.responses["header-first"] = {png}; network.responses["header-second"] = {png}; network.responses["header-updated"] = {png};
        Messenger messenger(nullptr, &network); messenger.startDemo();
        auto rows = messenger.conversations()->rows;
        for (int i = 0; i < rows.size(); ++i) {
            auto row = rows[i].toMap(); row.insert("id", row.value("peer"));
            if (i < 2) row.insert("avatar", i == 0 ? "https://assets.test/header-first" : "https://assets.test/header-second");
            rows[i] = row;
        }
        messenger.conversations()->reconcile(rows);
        QQmlApplicationEngine engine; engine.rootContext()->setContextProperty("messenger", &messenger);
        engine.load(QUrl::fromLocalFile(QStringLiteral(NATIVE_QML_PATH))); QVERIFY(!engine.rootObjects().isEmpty());
        auto root = engine.rootObjects().first();
        auto avatar = root->findChild<QObject *>("conversationHeaderAvatar"); QVERIFY(avatar);
        auto picture = avatar->findChild<QObject *>("avatarImage"); QVERIFY(picture);
        QCOMPARE(avatar->property("source").toString(), "https://assets.test/header-first");
        QTRY_COMPARE(picture->property("status").toInt(), 1);
        QVERIFY(picture->property("visible").toBool());

        messenger.selectConversation("2", "Анна Смирнова");
        QCOMPARE(avatar->property("source").toString(), "https://assets.test/header-second");
        QTRY_COMPARE(picture->property("status").toInt(), 1);
        QCOMPARE(picture->property("source").toUrl(), messenger.media()->cached(QUrl("https://assets.test/header-second")));

        auto updated = rows[1].toMap(); updated.insert("avatar", "https://assets.test/header-updated"); rows[1] = updated;
        std::reverse(rows.begin(), rows.end());
        messenger.conversations()->reconcile(rows); // model change without Messenger::changed
        QCOMPARE(avatar->property("source").toString(), "https://assets.test/header-updated");
        QTRY_COMPARE(picture->property("status").toInt(), 1);
        QCOMPARE(picture->property("source").toUrl(), messenger.media()->cached(QUrl("https://assets.test/header-updated")));

        messenger.selectConversation("3", "Дизайн и интерфейсы");
        QVERIFY(avatar->property("source").toString().isEmpty());
        QTRY_VERIFY(picture->property("source").toUrl().isEmpty());
        QVERIFY(!picture->property("visible").toBool());
        QCOMPARE(avatar->property("name").toString(), "Дизайн и интерфейсы");
        messenger.selectConversation("missing", "Новая переписка");
        QVERIFY(avatar->property("source").toString().isEmpty());
    }

    void messageContentLoadsOnlyWhenPresent()
    {
        TokenNetwork network;
        Messenger messenger(nullptr, &network); messenger.startDemo();
        QQmlApplicationEngine engine; engine.rootContext()->setContextProperty("messenger", &messenger);
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        engine.load(QUrl::fromLocalFile(QStringLiteral(NATIVE_QML_PATH))); QVERIFY(!engine.rootObjects().isEmpty());
        auto window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        const auto named = [window](const QString &name) {
            QList<QQuickItem *> result;
            const auto collect = [&](auto &&self, QQuickItem *item) -> void { if (item->objectName() == name) result.append(item); for (auto child : item->childItems()) self(self, child); };
            collect(collect, window->contentItem()); return result;
        };
        QVariantList plain;
        for (int i = 1; i <= 3000; ++i) plain.append(QVariantMap{{"id", QString::number(i)}, {"text", "Обычное сообщение"}, {"time", "12:00"}});
        messenger.messages()->replace(plain);
        auto history = window->findChild<QQuickItem *>("historyList"); QVERIFY(history);
        QTRY_COMPARE(history->property("count").toInt(), 3000);
        QTRY_VERIFY(named("messageRow").size() > 0); QVERIFY(named("messageRow").size() < 50);
        for (const auto &name : {"messageReplyPreview", "messageAttachments", "messageBotKeyboard", "forwardedMessages"}) QCOMPARE(named(name).size(), 0);
        const QVariantList attachments{QVariantMap{{"type", "doc"}, {"doc", QVariantMap{{"title", "Документ.pdf"}, {"url", "https://assets.test/document"}}}}};
        const QVariantMap keyboard{{"buttons", QVariantList{QVariantList{QVariantMap{{"action", QVariantMap{{"type", "location"}}}}}}}};
        QVariantMap row{{"id", "1"}, {"text", "Содержимое сообщения"}, {"time", "12:00"}, {"attachments", attachments}, {"replyAuthor", "Автор ответа"},
            {"raw", QVariantMap{{"from_id", "77"}, {"reply_message", QVariantMap{{"text", "Ответ на сообщение"}}}, {"keyboard", keyboard}, {"fwd_messages", QVariantList{QVariantMap{{"text", "Пересланный текст"}}}}}}};
        messenger.messages()->replace({row});
        for (const auto &name : {"messageReplyPreview", "messageAttachments", "messageBotKeyboard", "forwardedMessages"}) QTRY_COMPARE(named(name).size(), 1);
        QCOMPARE(named("messageReplyPreview").first()->property("summary").toString(), "Ответ на сообщение");
        QCOMPARE(named("messageBotKeyboard").first()->property("cmid").toString(), "1");
        QCOMPARE(named("messageBotKeyboard").first()->property("peer").toString(), messenger.selectedPeer());
        auto bot = named("messageBotKeyboard").first(); QSignalSpy location(bot, SIGNAL(locationRequested()));
        const QVariantMap locationAction{{"type", "location"}};
        QVERIFY(QMetaObject::invokeMethod(bot, "click", Q_ARG(QVariant, locationAction)));
        QCOMPARE(location.count(), 1);
        auto dialog = window->findChild<QObject *>("locationDialog"); QVERIFY(dialog);
        QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
        for (const auto &name : {"messageReplyPreview", "messageAttachments", "messageBotKeyboard", "forwardedMessages"}) QTRY_VERIFY(named(name).first()->height() > 0);
        messenger.messages()->reconcile({plain.first()});
        for (const auto &name : {"messageReplyPreview", "messageAttachments", "messageBotKeyboard", "forwardedMessages"}) QTRY_COMPARE(named(name).size(), 0);
        if (qEnvironmentVariableIsSet("VKM_FORWARD_TIMELINE_SCREENSHOT")) {
            window->resize(780, 560);
            auto sample = plain.first().toMap(); sample["text"] = "Пересылаю планы на выходные";
            sample["raw"] = QVariantMap{{"fwd_messages", QVariantList{
                QVariantMap{{"text", "Встречаемся в субботу утром."}, {"date", 1794258000}},
                QVariantMap{{"text", "Принесу фотографии и список идей."}, {"date", 1794258300}}
            }}};
            messenger.messages()->reconcile({sample});
            QTRY_COMPARE(named("forwardedMessageCard").size(), 2);
            for (bool dark : {false, true}) {
                messenger.setDarkTheme(dark); QTest::qWait(60);
                QVERIFY(window->grabWindow().save(qEnvironmentVariable("VKM_FORWARD_TIMELINE_SCREENSHOT") + (dark ? "-dark.png" : "-light.png")));
            }
        }
        QVERIFY(network.requests.isEmpty()); QCOMPARE(warnings.count(), 0);
    }

    void forwardedMessagesExpandInBatchesAndGateMedia()
    {
        TokenNetwork network;
        QImage image(32, 24, QImage::Format_RGB32); image.fill(QColor("#6a99cf"));
        QByteArray png; QBuffer buffer(&png); QVERIFY(buffer.open(QIODevice::WriteOnly)); QVERIFY(image.save(&buffer, "PNG"));
        QVariantList messages;
        for (int i = 0; i < 100; ++i) {
            network.responses[QString("forward-%1").arg(i)] = {png};
            messages.append(QVariantMap{{"text", QString("Пересланный текст %1 <b>без HTML</b>").arg(i + 1)}, {"date", 1794258000},
                {"attachments", QVariantList{QVariantMap{{"type", "photo"}, {"photo", QVariantMap{{"sizes", QVariantList{QVariantMap{{"url", QString("https://assets.test/forward-%1").arg(i)}, {"width", 320}, {"height", 240}}}}}}}}}});
        }
        Messenger messenger(nullptr, &network); messenger.startDemo();
        QQmlEngine engine; engine.rootContext()->setContextProperty("messenger", &messenger);
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        QQmlComponent component(&engine, QUrl::fromLocalFile(QFileInfo(QStringLiteral(NATIVE_QML_PATH)).dir().filePath("ForwardedMessages.qml")));
        std::unique_ptr<QObject> object(component.createWithInitialProperties({{"messages", messages}, {"messageKey", "first"}, {"loadImages", false}, {"viewportHeight", 300}}));
        QVERIFY2(object, qPrintable(component.errorString()));
        auto view = qobject_cast<QQuickItem *>(object.get()); QVERIFY(view);
        QQuickWindow window; window.resize(400, 700); view->setParentItem(window.contentItem()); view->setWidth(352); view->setX(24); view->setY(24); window.show();
        const auto named = [view](const QString &name) {
            QList<QQuickItem *> result;
            const auto collect = [&](auto &&self, QQuickItem *item) -> void { if (item->objectName() == name) result.append(item); for (auto child : item->childItems()) self(self, child); };
            collect(collect, view); return result;
        };
        QTRY_COMPARE(named("forwardedMessageCard").size(), 3); QVERIFY(network.requests.isEmpty());
        QTRY_VERIFY(named("forwardedMessageAttachments").first()->height() > 200);
        const auto photo = named("attachmentOpen").first(); const auto firstCard = named("forwardedMessageCard").first();
        QVERIFY(photo->mapToItem(firstCard, QPointF(0, photo->height())).y() <= firstCard->height());
        const auto secondCard = named("forwardedMessageCard")[1];
        QVERIFY(secondCard->y() >= firstCard->y() + firstCard->height());
        auto more = named("showMoreForwarded").first(); auto collapse = named("collapseForwarded").first();
        QCOMPARE(view->property("remaining").toInt(), 97);
        QVERIFY(named("forwardedMessageText").first()->property("text").toString().contains("<b>без HTML</b>"));
        QCOMPARE(named("forwardedMessageText").first()->property("textFormat").toInt(), 0); // TextEdit.PlainText
        view->setProperty("loadImages", true); QTRY_VERIFY(network.requests.size() > 0); QVERIFY(network.requests.size() <= 3);
        const auto initialRequests = network.requests.size();
        QVERIFY(QMetaObject::invokeMethod(more, "clicked")); QTRY_COMPARE(named("forwardedMessageCard").size(), 8);
        QTest::qWait(30); QCOMPARE(network.requests.size(), initialRequests);
        view->setProperty("viewportTop", -1500); QTRY_VERIFY(network.requests.size() > initialRequests); QVERIFY(network.requests.size() <= 8);
        // Updating the same message must retain the expanded state.
        auto edited = messages.first().toMap(); edited["text"] = "Исправленный текст"; messages[0] = edited;
        view->setProperty("messages", messages); QCOMPARE(named("forwardedMessageCard").size(), 8);
        QVERIFY(QMetaObject::invokeMethod(collapse, "clicked")); QTRY_COMPARE(named("forwardedMessageCard").size(), 3);
        QVERIFY(more->hasActiveFocus());
        QVERIFY(QMetaObject::invokeMethod(more, "clicked")); QTRY_COMPARE(named("forwardedMessageCard").size(), 8);
        view->setProperty("messageKey", "second"); QTRY_COMPARE(named("forwardedMessageCard").size(), 3);
        QVERIFY(QMetaObject::invokeMethod(more, "clicked")); QTRY_COMPARE(named("forwardedMessageCard").size(), 8);
        view->setProperty("loadImages", false); view->setProperty("viewportTop", 0);
        view->setProperty("messages", QVariantList{QVariantMap{{"text", "Планы на выходные: встречаемся в субботу утром."}, {"date", 1794258000}}, QVariantMap{{"text", "Принесу фотографии и небольшой список идей."}, {"date", 1794258300}}});
        QTRY_COMPARE(named("forwardedMessageCard").size(), 2); QVERIFY(!more->isVisible()); QVERIFY(!collapse->isVisible());
        QCOMPARE(named("forwardedMessageAttachments").size(), 0);
        for (bool dark : {false, true}) {
            messenger.setDarkTheme(dark); window.setColor(dark ? QColor("#222222") : QColor("white")); QTest::qWait(30);
            if (qEnvironmentVariableIsSet("VKM_FORWARDED_SCREENSHOT")) QVERIFY(window.grabWindow().save(qEnvironmentVariable("VKM_FORWARDED_SCREENSHOT") + (dark ? "-dark.png" : "-light.png")));
        }
        QCOMPARE(warnings.count(), 0);
    }

    void delayedLinkRevealReloadsTheSameMessageAndRejectsStaleHistory()
    {
        TokenNetwork network;
        const QJsonObject bare{{"id", 8642292}, {"conversation_message_id", 358}, {"peer_id", -218837624}, {"from_id", 42}, {"date", 1794323860LL}, {"text", "google.com"}, {"out", 1}};
        network.responses["messages.getDiff"] = {QJsonDocument(QJsonObject{{"response", QJsonObject{{"server_version", "500"}, {"credentials", QJsonObject{{"server_lp", "lp.test/im"}, {"key", "key"}, {"ts", "100"}}}, {"conversations_info", QJsonArray{QJsonObject{{"conversation", QJsonObject{{"peer", QJsonObject{{"id", -218837624}}}, {"last_conversation_message_id", 358}, {"in_read_cmid", 358}, {"out_read_cmid", 358}}}, {"message", QJsonArray{bare}}}}}}}}).toJson()};
        QImage image(640, 360, QImage::Format_RGB32); image.fill(QColor("#dce9f8"));
        QPainter painter(&image); painter.setPen(QColor("#3679d1")); auto font = painter.font(); font.setPixelSize(64); painter.setFont(font); painter.drawText(image.rect(), Qt::AlignCenter, "Preview"); painter.end();
        QByteArray png; QBuffer buffer(&png); QVERIFY(buffer.open(QIODevice::WriteOnly)); QVERIFY(image.save(&buffer, "PNG")); network.responses["link-image"] = {png};
        Messenger messenger(nullptr, &network); messenger.loginWithToken("test-token"); QTRY_COMPARE(messenger.conversations()->rowCount(), 1);
        messenger.selectConversation("-218837624", "Тестовая переписка"); QTRY_COMPARE(network.replies["messages.getDiffContent"].size(), 1);
        QTRY_COMPARE(messenger.messages()->rowCount(), 1);
        QPersistentModelIndex retained(messenger.messages()->index(0, 0)); QSignalSpy resets(messenger.messages(), &QAbstractItemModel::modelReset);
        QSignalSpy notifications(&messenger, &Messenger::notificationRequested);
        QQmlApplicationEngine engine; engine.rootContext()->setContextProperty("messenger", &messenger);
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        engine.load(QUrl::fromLocalFile(QStringLiteral(NATIVE_QML_PATH))); QVERIFY(!engine.rootObjects().isEmpty());
        auto window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window); window->resize(780, 560);
        const auto cards = [&] {
            QList<QQuickItem *> found;
            const auto visit = [&](auto &&self, QQuickItem *item) -> void { if (item->objectName() == "linkPreview") found.append(item); for (auto child : item->childItems()) self(self, child); };
            visit(visit, window->contentItem()); return found;
        };
        QVERIFY(cards().isEmpty());
        const QJsonObject fields{{"attach1", ""}, {"attach1_type", "link"}, {"attach1_url", "http://google.com"}, {"attach1_title", "Google"}, {"attach1_desc", "Поиск информации в интернете: веб-страницы, картинки, видео."}, {"attach1_photo", "2000004599_457557768"}};
        messenger.applyLongPoll({QJsonValue(QJsonArray{10018, 358, 2, -218837624, 1794323860LL, "google.com", QJsonObject{{"title", " ... "}}, fields, -1175014194, 8642292, 0})});
        const auto content = [](const QJsonObject &message) { return QJsonDocument(QJsonObject{{"response", QJsonObject{{"items", QJsonArray{QJsonObject{{"peer_id", -218837624}, {"messages", QJsonArray{message}}}}}}}}).toJson(); };
        network.replies["messages.getDiffContent"][0]->deliver(content(bare)); // older request started before the LP event
        QTRY_COMPARE(network.replies["messages.getDiffContent"].size(), 2);
        QJsonObject link{{"url", "http://google.com"}, {"title", "Google"}, {"description", "Поиск информации в интернете: веб-страницы, картинки, видео."}, {"photo", QJsonObject{{"owner_id", 2000004599}, {"id", 457557768}, {"sizes", QJsonArray{QJsonObject{{"url", "https://assets.test/link-image"}, {"width", 640}, {"height", 360}}}}}}};
        auto revealed = bare; revealed.insert("attachments", QJsonArray{QJsonObject{{"type", "link"}, {"link", link}}});
        network.replies["messages.getDiffContent"][1]->deliver(content(revealed));
        QTRY_VERIFY(!messenger.loading()); QTRY_COMPARE(cards().size(), 1);
        auto thumbnail = cards().first()->findChild<QQuickItem *>("linkPreviewImage"); QVERIFY(thumbnail); QTRY_COMPARE(thumbnail->property("status").toInt(), 1);
        QCOMPARE(messenger.messages()->rowCount(), 1); QVERIFY(retained.isValid()); QCOMPARE(resets.count(), 0); QCOMPARE(notifications.count(), 0);
        QCOMPARE(messenger.messages()->get(0).value("id").toString(), "358"); QCOMPARE(messenger.messages()->get(0).value("text").toString(), "google.com");
        QVERIFY(messenger.messages()->get(0).value("outgoing").toBool());
        const auto request = network.requests;
        bool targeted = false;
        for (const auto &r : request) if (r.first.path().endsWith("messages.getDiffContent")) {
            const QUrlQuery params(QString::fromUtf8(r.second));
            const auto items = QJsonDocument::fromJson(params.queryItemValue("conversation_messages", QUrl::FullyDecoded).toUtf8()).array();
            if (!items.isEmpty() && items[0].toObject().value("updated_cmids").toArray().contains(358)) targeted = true;
        }
        QVERIFY(targeted);
        for (bool dark : {false, true}) {
            messenger.setDarkTheme(dark); QTest::qWait(40);
            if (qEnvironmentVariableIsSet("VKM_LINK_PREVIEW_SCREENSHOT")) QVERIFY(window->grabWindow().save(qEnvironmentVariable("VKM_LINK_PREVIEW_SCREENSHOT") + (dark ? "-dark.png" : "-light.png")));
        }
        link.insert("title", "Google — обновлённое превью"); revealed.insert("attachments", QJsonArray{QJsonObject{{"type", "link"}, {"link", link}}});
        network.responses["messages.getDiffContent"].append(content(revealed));
        messenger.applyLongPoll({QJsonValue(QJsonArray{18, 8642292, 2, -218837624})});
        QTRY_COMPARE(network.replies["messages.getDiffContent"].size(), 3);
        QTRY_COMPARE(cards().first()->findChild<QQuickItem *>("linkPreviewTitle")->property("text").toString(), QString("Google — обновлённое превью"));
        QTest::qWait(160); QCOMPARE(network.replies["messages.getDiffContent"].size(), 3); QCOMPARE(network.replies["messages.getDiff"].size(), 1);
        link.insert("title", "Google — повторное раскрытие"); revealed.insert("attachments", QJsonArray{QJsonObject{{"type", "link"}, {"link", link}}});
        network.responses["messages.getDiffContent"].append(content(revealed));
        messenger.applyLongPoll({QJsonArray{10018, 358, 2, -218837624}, QJsonArray{10018, 358, 2, -218837624}});
        QTRY_COMPARE(cards().first()->findChild<QQuickItem *>("linkPreviewTitle")->property("text").toString(), QString("Google — повторное раскрытие"));
        QTest::qWait(160); QCOMPARE(network.replies["messages.getDiffContent"].size(), 4);
        QCOMPARE(messenger.messages()->rowCount(), 1); QCOMPARE(resets.count(), 0); QCOMPARE(notifications.count(), 0); QCOMPARE(warnings.count(), 0);
    }

    void initialApiMessageRendersLinkPreviewWithoutLongPoll()
    {
        TokenNetwork network;
        QImage image(64, 36, QImage::Format_RGB32); image.fill(Qt::green);
        QByteArray png; QBuffer buffer(&png); QVERIFY(buffer.open(QIODevice::WriteOnly)); QVERIFY(image.save(&buffer, "PNG")); network.responses["api-link-image"] = {png};
        const QJsonObject link{{"url", "https://example.test/article"}, {"title", "Статья из API"}, {"description", "Описание из attachments.link"}, {"photo", QJsonObject{{"sizes", QJsonArray{QJsonObject{{"url", "https://assets.test/api-link-image"}, {"width", 640}, {"height", 360}}}}}}};
        const QJsonObject message{{"conversation_message_id", 1}, {"peer_id", 77}, {"text", "Статья"}, {"attachments", QJsonArray{QJsonObject{{"type", "link"}, {"link", link}}}}};
        network.responses["messages.getDiff"] = {QJsonDocument(QJsonObject{{"response", QJsonObject{{"server_version", "500"}, {"credentials", QJsonObject{{"server_lp", "lp.test/im"}, {"key", "key"}, {"ts", "100"}}}, {"conversations_info", QJsonArray{QJsonObject{{"conversation", QJsonObject{{"peer", QJsonObject{{"id", 77}}}, {"last_conversation_message_id", 1}, {"in_read_cmid", 1}}}, {"message", QJsonArray{message}}}}}}}}).toJson()};
        Messenger messenger(nullptr, &network); messenger.loginWithToken("test-token"); QTRY_COMPARE(messenger.conversations()->rowCount(), 1); messenger.selectConversation("77", "API");
        QQmlApplicationEngine engine; engine.rootContext()->setContextProperty("messenger", &messenger); engine.load(QUrl::fromLocalFile(QStringLiteral(NATIVE_QML_PATH))); QVERIFY(!engine.rootObjects().isEmpty());
        auto window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        QQuickItem *card = nullptr;
        const auto find = [&](auto &&self, QQuickItem *item) -> void { if (item->objectName() == "linkPreview") card = item; for (auto child : item->childItems()) self(self, child); };
        QTRY_VERIFY([&] { find(find, window->contentItem()); return card != nullptr; }());
        QCOMPARE(card->findChild<QQuickItem *>("linkPreviewTitle")->property("text").toString(), QString("Статья из API"));
        QCOMPARE(card->findChild<QQuickItem *>("linkPreviewDescription")->property("text").toString(), QString("Описание из attachments.link"));
        QTRY_COMPARE(card->findChild<QQuickItem *>("linkPreviewImage")->property("status").toInt(), 1);
    }

    void apiLinkPreviewLoadsOnDemandAndRetriesWithoutOpeningPage()
    {
        TokenNetwork network; network.responses["link-thumbnail"] = {"#offline", "invalid-image"};
        QImage image(320, 180, QImage::Format_RGB32); image.fill(Qt::blue); QByteArray png; QBuffer bytes(&png); QVERIFY(bytes.open(QIODevice::WriteOnly)); QVERIFY(image.save(&bytes, "PNG")); network.responses["link-thumbnail"].append(png);
        const QVariantMap link{{"url", "https://example.test/page"}, {"title", "Заголовок <b>без HTML</b>"}, {"description", "Описание страницы из API"}, {"photo", QVariantMap{{"sizes", QVariantList{QVariantMap{{"url", "https://assets.test/link-thumbnail"}, {"width", 320}, {"height", 180}}}}}}};
        Messenger messenger(nullptr, &network); messenger.startDemo();
        QQmlEngine engine; engine.rootContext()->setContextProperty("messenger", &messenger);
        QQmlComponent component(&engine, QUrl::fromLocalFile(QFileInfo(QStringLiteral(NATIVE_QML_PATH)).dir().filePath("AttachmentView.qml")));
        std::unique_ptr<QObject> object(component.createWithInitialProperties({{"attachments", QVariantList{QVariantMap{{"type", "link"}, {"link", link}}}}, {"loadImages", false}}));
        QVERIFY2(object, qPrintable(component.errorString())); auto view = qobject_cast<QQuickItem *>(object.get()); QVERIFY(view);
        QQuickWindow window; window.resize(400, 500); view->setParentItem(window.contentItem()); view->setWidth(360); view->setX(20); window.show();
        const auto named = [view](const QString &name) {
            QList<QQuickItem *> result; const auto visit = [&](auto &&self, QQuickItem *item) -> void { if (item->objectName() == name) result.append(item); for (auto child : item->childItems()) self(self, child); }; visit(visit, view); return result;
        };
        QTRY_COMPARE(named("linkPreview").size(), 1); QVERIFY(network.requests.isEmpty());
        auto card = named("linkPreview").first(); auto thumbnail = named("linkPreviewImage").first(); auto retry = named("retryLinkPreview").first();
        QCOMPARE(named("linkPreviewTitle").first()->property("text").toString(), link.value("title").toString());
        QCOMPARE(named("linkPreviewTitle").first()->property("textFormat").toInt(), 0);
        view->setProperty("loadImages", true); QTRY_VERIFY(retry->isVisible());
        LoginBrowserCapture browser; QDesktopServices::setUrlHandler("https", &browser, "capture");
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, retry->mapToScene(QPointF(retry->width() / 2, retry->height() / 2)).toPoint());
        QTRY_COMPARE(thumbnail->property("status").toInt(), 3); // damaged image
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, retry->mapToScene(QPointF(retry->width() / 2, retry->height() / 2)).toPoint());
        QTRY_COMPARE(thumbnail->property("status").toInt(), 1); QVERIFY(browser.url.isEmpty());
        QVERIFY(card->height() > thumbnail->height() + 40); QCOMPARE(network.replies["link-thumbnail"].size(), 3);
        card->forceActiveFocus(); QTest::keyClick(&window, Qt::Key_Return); QCOMPARE(browser.url, QUrl("https://example.test/page")); QDesktopServices::unsetUrlHandler("https");
        view->setProperty("loadImages", false); QTRY_VERIFY(thumbnail->property("source").toUrl().isEmpty());
        view->setProperty("loadImages", true); QTRY_COMPARE(thumbnail->property("status").toInt(), 1); QCOMPARE(network.replies["link-thumbnail"].size(), 3);
    }

    void publicApiLinkRevealFetchesTheUpdatedCmidOutsideLatestPage()
    {
        TokenNetwork network;
        network.responses["messages.getDiff"] = {R"({"error":{"error_code":3,"error_msg":"Unknown method passed"}})"};
        network.responses["messages.getConversations"] = {R"({"response":{"count":1,"items":[{"conversation":{"peer":{"id":77}},"last_message":{"id":9000,"conversation_message_id":9000,"peer_id":77,"text":"Latest"}}]}})"};
        const QJsonObject old{{"id", 123}, {"conversation_message_id", 358}, {"peer_id", 77}, {"text", "google.com"}};
        const QJsonObject latest{{"id", 9000}, {"conversation_message_id", 9000}, {"peer_id", 77}, {"text", "Latest"}};
        const auto history = [](QJsonArray items) { return QJsonDocument(QJsonObject{{"response", QJsonObject{{"items", items}}}}).toJson(); };
        network.responses["messages.getHistory"] = {history({latest, old}), history({latest})};
        Messenger messenger(nullptr, &network); messenger.loginWithToken("test-token"); QTRY_COMPARE(messenger.conversations()->rowCount(), 1);
        messenger.selectConversation("77", "API"); QTRY_VERIFY(!messenger.loading()); QTRY_VERIFY(messenger.messages()->find("358") >= 0);
        auto updated = old; updated.insert("attachments", QJsonArray{QJsonObject{{"type", "link"}, {"link", QJsonObject{{"url", "http://google.com"}, {"title", "Google"}}}}});
        network.responses["messages.getByConversationMessageId"] = {history({updated})};
        messenger.applyLongPoll({QJsonValue(QJsonArray{10018, 358, 0, 77})});
        QTRY_VERIFY(!messenger.messages()->get(messenger.messages()->find("358")).value("attachments").toList().isEmpty());
        QCOMPARE(network.replies["messages.getByConversationMessageId"].size(), 1);
        const auto request = std::find_if(network.requests.begin(), network.requests.end(), [](const auto &r) { return r.first.path().endsWith("messages.getByConversationMessageId"); }); QVERIFY(request != network.requests.end());
        QCOMPARE(QUrlQuery(QString::fromUtf8(request->second)).queryItemValue("conversation_message_ids"), "358");
        QVERIFY(messenger.messages()->find("9000") >= 0);
    }

    void photoViewerUsesLargestAvailableSize()
    {
        Messenger messenger; messenger.startDemo();
        QSignalSpy opened(messenger.actions(), &ActionController::mediaRequested);
        messenger.actions()->openAttachment({{"type", "photo"}, {"photo", QVariantMap{{"sizes", QVariantList{
            QVariantMap{{"url", "https://assets.test/full"}, {"width", 1600}, {"height", 1200}},
            QVariantMap{{"url", "https://assets.test/thumb"}, {"width", 320}, {"height", 240}}}}}}});
        QCOMPARE(opened.size(), 1);
        QCOMPARE(opened.first()[0].toUrl(), QUrl("https://assets.test/full"));
        const QVariantMap first{{"type", "photo"}, {"photo", QVariantMap{{"sizes", QVariantList{
            QVariantMap{{"url", "https://assets.test/full"}, {"width", 1600}, {"height", 1200}},
            QVariantMap{{"url", "https://assets.test/too-small"}, {"width", 80}, {"height", 60}},
            QVariantMap{{"url", "https://assets.test/medium"}, {"width", 640}, {"height", 480}},
            QVariantMap{{"url", "http://assets.test/insecure"}, {"width", 180}, {"height", 180}},
            QVariantMap{{"url", "https://assets.test/thumb"}, {"width", 320}, {"height", 240}}}}}}};
        const QVariantMap second{{"type", "photo"}, {"photo", QVariantMap{{"sizes", QVariantList{
            QVariantMap{{"url", "https://assets.test/small-fallback"}, {"width", 80}, {"height", 60}}}}}}};
        QSignalSpy gallery(messenger.actions(), &ActionController::photoGalleryRequested);
        messenger.actions()->openAttachment(first, {first, second});
        QCOMPARE(gallery.size(), 1);
        const auto photos = gallery.first()[0].toList();
        QCOMPARE(photos[0].toMap().value("source").toUrl(), QUrl("https://assets.test/full"));
        QCOMPARE(photos[0].toMap().value("thumbnail").toUrl(), QUrl("https://assets.test/thumb"));
        QCOMPARE(photos[1].toMap().value("thumbnail").toUrl(), QUrl("https://assets.test/small-fallback"));
    }

    void avatarsShareDecodedImagesAndRespectFileChanges()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("avatar.png");
        QImage image(256, 256, QImage::Format_RGB32); image.fill(Qt::blue); QVERIFY(image.save(path));
        AvatarImage first; first.setWidth(64); first.setHeight(64);
        first.setSource(QUrl::fromLocalFile(path)); QTRY_COMPARE(first.status(), 1);
        QCoreApplication::processEvents();
        const auto paintedColor = [](AvatarImage &avatar) {
            QImage output(64, 64, QImage::Format_ARGB32_Premultiplied); output.fill(Qt::transparent);
            QPainter painter(&output); avatar.paint(&painter); painter.end();
            return output.pixelColor(32, 32);
        };
        AvatarImage warm; warm.setWidth(64); warm.setHeight(64);
        warm.setSource(QUrl::fromLocalFile(path)); QCOMPARE(warm.status(), 1); QCOMPARE(paintedColor(warm), QColor(Qt::blue));
        image.fill(Qt::green); QVERIFY(image.save(path));
        QFile updated(path); QVERIFY(updated.open(QIODevice::ReadOnly));
        QVERIFY(updated.setFileTime(QDateTime::currentDateTimeUtc().addSecs(2), QFileDevice::FileModificationTime)); updated.close();
        AvatarImage changed; changed.setWidth(64); changed.setHeight(64);
        changed.setSource(QUrl::fromLocalFile(path)); QTRY_COMPARE(changed.status(), 1); QCOMPARE(paintedColor(changed), QColor(Qt::green));
        const auto bluePath = directory.filePath("redirected-blue.png");
        QImage blue(64, 64, QImage::Format_RGB32); blue.fill(Qt::blue); QVERIFY(blue.save(bluePath));
        AvatarImage redirected; redirected.setWidth(64); redirected.setHeight(64);
        connect(&redirected, &AvatarImage::sourceChanged, &redirected, [&] { if (redirected.source() == QUrl::fromLocalFile(bluePath)) redirected.setSource(QUrl::fromLocalFile(path)); });
        redirected.setSource(QUrl::fromLocalFile(bluePath)); QTRY_COMPARE(redirected.status(), 1);
        QCOMPARE(redirected.source(), QUrl::fromLocalFile(path)); QCOMPARE(paintedColor(redirected), QColor(Qt::green));
        first.setSource({}); QCOMPARE(first.status(), 0);
        // Oversubscribe the shared 8 MiB cache with 40 distinct decoded images.
        std::vector<std::unique_ptr<AvatarImage>> avatars;
        for (int i = 0; i < 40; ++i) {
            const auto file = directory.filePath(QString("avatar-%1.png").arg(i)); QVERIFY(image.save(file));
            auto avatar = std::make_unique<AvatarImage>(); avatar->setSource(QUrl::fromLocalFile(file)); avatars.push_back(std::move(avatar));
        }
        QTRY_VERIFY(std::all_of(avatars.begin(), avatars.end(), [](const auto &avatar) { return avatar->status() == 1; }));
        QCoreApplication::processEvents();
        AvatarImage evicted; evicted.setSource(QUrl::fromLocalFile(path)); QCOMPARE(evicted.status(), 2); QTRY_COMPARE(evicted.status(), 1);
        // Deleting the first requester must not interrupt the other requesters.
        const auto sharedPath = directory.filePath("shared.png"); QVERIFY(image.save(sharedPath));
        auto discarded = std::make_unique<AvatarImage>(); discarded->setSource(QUrl::fromLocalFile(sharedPath));
        AvatarImage survivor; survivor.setSource(QUrl::fromLocalFile(sharedPath)); discarded.reset();
        QTRY_COMPARE(survivor.status(), 1);
    }

    void videoCardsAreCreatedOnlyForVideoAttachments()
    {
        TokenNetwork network;
        Messenger messenger(nullptr, &network); messenger.startDemo();
        QQmlEngine engine; engine.rootContext()->setContextProperty("messenger", &messenger);
        QQmlComponent component(&engine, QUrl::fromLocalFile(QFileInfo(QStringLiteral(NATIVE_QML_PATH)).dir().filePath("AttachmentView.qml")));
        const QVariantMap sticker{{"type", "sticker"}, {"sticker", QVariantMap{{"images", QVariantList{QVariantMap{{"url", "https://assets.test/sticker"}}}}}}};
        const QVariantMap document{{"type", "doc"}, {"doc", QVariantMap{{"title", "Документ"}, {"url", "https://assets.test/file"}}}};
        const QVariantMap video{{"type", "video"}, {"video", QVariantMap{{"title", "Видео"}, {"duration", 5}, {"files", QVariantMap{{"mp4_720", "https://assets.test/video.mp4"}}}}}};
        std::unique_ptr<QObject> object(component.createWithInitialProperties({{"attachments", QVariantList{sticker, document, video}}, {"loadImages", false}}));
        QVERIFY2(object, qPrintable(component.errorString()));
        auto view = qobject_cast<QQuickItem *>(object.get()); QVERIFY(view);
        QQuickWindow window; view->setParentItem(window.contentItem()); view->setWidth(400); view->setHeight(700); window.resize(400, 700); window.show();
        const auto cards = [&] {
            QList<QQuickItem *> result;
            const auto collect = [&](auto &&self, QQuickItem *item) -> void { if (item->objectName() == "videoAttachmentOpen") result.append(item); for (auto child : item->childItems()) self(self, child); };
            collect(collect, view); return result;
        };
        QTRY_COMPARE(cards().size(), 1); QVERIFY(cards().first()->isVisible());
        QVERIFY(cards().first()->width() > 300); QVERIFY(cards().first()->height() > 100);
        QSignalSpy opened(messenger.actions(), &ActionController::mediaRequested);
        QVERIFY(QMetaObject::invokeMethod(cards().first(), "clicked"));
        QCOMPARE(opened.count(), 1); QCOMPARE(opened.first()[0].toUrl(), QUrl("https://assets.test/video.mp4"));
        view->setProperty("attachments", QVariantList{sticker, document}); QTRY_COMPARE(cards().size(), 0);
        QVERIFY(network.requests.isEmpty());
    }

    void photoThumbnailRetriesWithoutOpeningViewer()
    {
        TokenNetwork network; network.responses["retry-thumb"] = {"#offline", "invalid-image"};
        Messenger messenger(nullptr, &network); messenger.startDemo();
        const QVariantMap attachment{{"type", "photo"}, {"photo", QVariantMap{{"sizes", QVariantList{QVariantMap{{"url", "https://assets.test/retry-thumb"}, {"width", 400}, {"height", 300}}}}}}};
        auto message = messenger.messages()->get(0); message["attachments"] = QVariantList{attachment}; messenger.messages()->replace({message});
        QQmlApplicationEngine engine; engine.rootContext()->setContextProperty("messenger", &messenger);
        engine.load(QUrl::fromLocalFile(QStringLiteral(NATIVE_QML_PATH))); QVERIFY(!engine.rootObjects().isEmpty());
        auto window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window); window->requestActivate(); QTRY_VERIFY(window->isActive());
        QQuickItem *retry = nullptr, *thumbnail = nullptr;
        const auto collect = [&](auto &&self, QQuickItem *item) -> void {
            if (item->objectName() == "retryPhotoThumbnail") retry = item;
            if (item->objectName() == "photoThumbnail") thumbnail = item;
            for (auto child : item->childItems()) self(self, child);
        };
        QTRY_VERIFY([&] { collect(collect, window->contentItem()); return retry && thumbnail; }());
        QTRY_VERIFY(retry->isVisible());
        window->resize(780, 560); QTest::qWait(100);
        if (qEnvironmentVariableIsSet("VKM_THUMBNAIL_SCREENSHOT")) {
            for (const bool dark : {true, false}) {
                messenger.setDarkTheme(dark); QTest::qWait(100);
                QVERIFY(window->grabWindow().save(qEnvironmentVariable("VKM_THUMBNAIL_SCREENSHOT") + (dark ? "-failed-dark.png" : "-failed-light.png")));
            }
        }
        QSignalSpy opened(messenger.actions(), &ActionController::mediaRequested);
        const auto click = [&] { QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, retry->mapToScene(QPointF(retry->width() / 2, retry->height() / 2)).toPoint()); };
        click(); QTRY_COMPARE(thumbnail->property("status").toInt(), 3); QTRY_VERIFY(retry->isVisible());
        QImage image(400, 300, QImage::Format_RGB32); image.fill(QColor("#5687a4"));
        QByteArray png; QBuffer buffer(&png); QVERIFY(buffer.open(QIODevice::WriteOnly)); QVERIFY(image.save(&buffer, "PNG"));
        network.responses["retry-thumb"] = {png};
        click(); QTRY_COMPARE(thumbnail->property("status").toInt(), 1); QTRY_VERIFY(!retry->isVisible());
        QCOMPARE(opened.count(), 0); QCOMPARE(network.replies["retry-thumb"].size(), 3);
        window->resize(780, 560); QTest::qWait(100);
        if (qEnvironmentVariableIsSet("VKM_THUMBNAIL_SCREENSHOT")) QVERIFY(window->grabWindow().save(qEnvironmentVariable("VKM_THUMBNAIL_SCREENSHOT") + "-ready.png"));
    }

    void photoCollageUsesThumbnailsAndReplyPreview()
    {
        QImage image(320, 240, QImage::Format_ARGB32_Premultiplied); image.fill(QColor("#5687a4"));
        QByteArray png; QBuffer buffer(&png); QVERIFY(buffer.open(QIODevice::WriteOnly)); QVERIFY(image.save(&buffer, "PNG"));
        TokenNetwork network;
        QVariantList attachments;
        for (int i = 0; i < 10; ++i) {
            const auto key = QString("thumb-%1").arg(i);
            network.responses[key] = {png};
            attachments.append(QVariantMap{{"type", "photo"}, {"photo", QVariantMap{{"id", i}, {"sizes", QVariantList{
                QVariantMap{{"url", "https://assets.test/" + key}, {"width", 640}, {"height", 480}},
                QVariantMap{{"url", "https://assets.test/full-" + QString::number(i)}, {"width", 2560}, {"height", 1920}}}}}}});
        }
        Messenger messenger(nullptr, &network); messenger.startDemo();
        auto message = messenger.messages()->get(0);
        message.insert("text", ""); message.insert("attachments", attachments);
        message.insert("replyAuthor", "Тестовый собеседник");
        message.insert("raw", QVariantMap{{"text", ""}, {"attachments", attachments}, {"reply_message", QVariantMap{{"conversation_message_id", 7}, {"text", "Встреча в субботу"}}}});
        messenger.messages()->replace({message});
        QQmlApplicationEngine engine; engine.rootContext()->setContextProperty("messenger", &messenger);
        engine.load(QUrl::fromLocalFile(QStringLiteral(NATIVE_QML_PATH))); QVERIFY(!engine.rootObjects().isEmpty());
        auto window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        const auto named = [window](const QString &name) {
            QList<QQuickItem *> result;
            const auto collect = [&](auto &&self, QQuickItem *item) -> void {
                if (item->objectName() == name) result.append(item);
                for (auto child : item->childItems()) self(self, child);
            };
            collect(collect, window->contentItem()); return result;
        };
        QTRY_COMPARE(named("attachmentOpen").size(), 10);
        const auto geometryReady = [&] {
            const auto tiles = named("attachmentOpen");
            return std::all_of(tiles.begin(), tiles.end(), [](auto tile) { return tile->width() > 30 && tile->height() > 30; });
        };
        QTRY_VERIFY(geometryReady());
        auto quote = named("messageReplyPreview").value(0); QVERIFY(quote);
        QCOMPARE(quote->property("author").toString(), "Тестовый собеседник");
        QCOMPARE(quote->property("summary").toString(), "Встреча в субботу");
        QTRY_COMPARE(network.requests.size(), 10);
        for (const auto &request : network.requests) QVERIFY(!request.first.path().contains("full-"));
        messenger.setReply(message.value("id").toString());
        auto composerQuote = window->findChild<QQuickItem *>("composerReplyPreview"); QVERIFY(composerQuote);
        QTRY_VERIFY(composerQuote->isVisible());
        QVERIFY(QMetaObject::invokeMethod(composerQuote, "dismissed"));
        QVERIFY(messenger.reply().isEmpty());
        if (qEnvironmentVariableIsSet("VKM_COLLAGE_SCREENSHOT")) QVERIFY(window->grabWindow().save(qEnvironmentVariable("VKM_COLLAGE_SCREENSHOT")));
    }

    void recentlyOpenedChatHasHistoryImmediately()
    {
        TokenNetwork network;
        QJsonArray messages;
        const QJsonArray attachments{QJsonObject{{"type", "photo"}, {"photo", QJsonObject{{"sizes", QJsonArray{
            QJsonObject{{"url", "https://assets.test/thumbnail"}, {"width", 640}, {"height", 480}},
            QJsonObject{{"url", "https://assets.test/original"}, {"width", 2560}, {"height", 1920}}}}}}}};
        for (int i = 1; i <= 200; ++i) messages.append(QJsonObject{{"conversation_message_id", i}, {"peer_id", 77}, {"text", i == 1 ? "Первое" : "Сообщение с фотографией"}, {"attachments", attachments}});
        const QJsonArray conversations{
            QJsonObject{{"conversation", QJsonObject{{"peer", QJsonObject{{"id", 77}}}, {"last_conversation_message_id", 200}}}, {"message", messages}},
            QJsonObject{{"conversation", QJsonObject{{"peer", QJsonObject{{"id", 88}}}, {"last_conversation_message_id", 1}}}, {"message", QJsonArray{QJsonObject{{"conversation_message_id", 1}, {"peer_id", 88}, {"text", "Другой чат"}}}}}
        };
        network.responses["messages.getDiff"] = {QJsonDocument(QJsonObject{{"response", QJsonObject{{"server_version", "500"}, {"credentials", QJsonObject{{"server_lp", "lp.test/im"}, {"key", "key"}, {"ts", "100"}}}, {"conversations_info", conversations}}}}).toJson(QJsonDocument::Compact)};
        Messenger messenger(nullptr, &network);
        messenger.loginWithToken("test-token"); QTRY_COMPARE(messenger.conversations()->rowCount(), 2);
        messenger.selectConversation("77", "Первый чат");
        QTRY_COMPARE(messenger.messages()->rowCount(), 200);
        messenger.selectConversation("88", "Второй чат");
        QList<int> resetSizes;
        connect(messenger.messages(), &QAbstractItemModel::modelReset, &messenger, [&] { resetSizes.append(messenger.messages()->rowCount()); });
        QElapsedTimer timer; timer.start();
        messenger.selectConversation("77", "Первый чат");
        // No event processing or disk/network response is needed for this return.
        QCOMPARE(messenger.messages()->rowCount(), 200);
        QCOMPARE(messenger.messages()->get(0).value("text").toString(), "Первое");
        QVERIFY(!resetSizes.contains(0));
        qInfo() << "Warm chat model ready synchronously in" << timer.elapsed() << "ms";
        messenger.logout(); QVERIFY(messenger.messages()->rows.isEmpty());
    }

    void photoGalleryOpensClickedPhotoAndNavigates()
    {
        QImage image(32, 32, QImage::Format_ARGB32_Premultiplied); image.fill(Qt::green);
        QByteArray png; QBuffer buffer(&png); QVERIFY(buffer.open(QIODevice::WriteOnly)); QVERIFY(image.save(&buffer, "PNG"));
        TokenNetwork network;
        network.responses["gallery-first"] = {png}; network.responses["gallery-second"] = {png};
        network.responses["gallery-third"] = {"#offline"};
        Messenger messenger(nullptr, &network); messenger.startDemo();
        QFile fixture(QFINDTESTDATA("fixtures/photo-gallery.json")); QVERIFY(fixture.open(QIODevice::ReadOnly));
        const auto attachments = safeJson(QJsonDocument::fromJson(fixture.readAll()).array()).toList();
        auto message = messenger.messages()->get(0); message.insert("attachments", attachments);
        messenger.messages()->replace({message});
        QQmlApplicationEngine engine; engine.rootContext()->setContextProperty("messenger", &messenger);
        engine.load(QUrl::fromLocalFile(QStringLiteral(NATIVE_QML_PATH))); QVERIFY(!engine.rootObjects().isEmpty());
        auto window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        const auto openButtons = [window]() {
            QList<QQuickItem *> items;
            const auto visit = [&](auto &&self, QQuickItem *item) -> void {
                if (item->objectName() == "attachmentOpen") items.append(item);
                for (auto child : item->childItems()) self(self, child);
            };
            visit(visit, window->contentItem()); return items;
        };
        QTRY_COMPARE(openButtons().size(), 5);
        QSignalSpy galleryRequested(messenger.actions(), &ActionController::photoGalleryRequested);
        QQuickItem *secondPhoto = nullptr;
        for (auto button : openButtons()) if (button->property("attachment").toMap().value("photo").toMap().value("text").toString() == "Второе фото") secondPhoto = button;
        QVERIFY(secondPhoto);
        QVERIFY(QMetaObject::invokeMethod(secondPhoto, "clicked"));
        QCOMPARE(galleryRequested.size(), 1);
        QCOMPARE(galleryRequested.first()[0].toList().size(), 3);
        QCOMPARE(galleryRequested.first()[1].toInt(), 1);
        auto viewer = window->findChild<QObject *>("mediaViewer"); QVERIFY(viewer);
        auto previous = viewer->findChild<QObject *>("viewerPrevious"); QVERIFY(previous);
        auto next = viewer->findChild<QObject *>("viewerNext"); QVERIFY(next);
        auto count = viewer->findChild<QObject *>("viewerPhotoCount"); QVERIFY(count);
        auto photo = viewer->findChild<QObject *>("viewerPhotograph"); QVERIFY(photo);
        auto error = viewer->findChild<QObject *>("viewerError"); QVERIFY(error);
        QTRY_VERIFY(viewer->property("opened").toBool());
        QTRY_COMPARE(photo->property("status").toInt(), 1);
        QCOMPARE(viewer->property("remote").toUrl(), QUrl("https://assets.test/gallery-second"));
        QCOMPARE(count->property("text").toString(), "2 из 3");
        QCOMPARE(viewer->property("title").toString(), "Второе фото");

        viewer->setProperty("zoom", 3);
        QVERIFY(QMetaObject::invokeMethod(previous, "clicked"));
        QCOMPARE(viewer->property("remote").toUrl(), QUrl("https://assets.test/gallery-first"));
        QCOMPARE(count->property("text").toString(), "1 из 3");
        QCOMPARE(viewer->property("zoom").toReal(), 1.0);
        QVERIFY(!previous->property("enabled").toBool());
        QTest::keyClick(window, Qt::Key_Left);
        QCOMPARE(viewer->property("currentIndex").toInt(), 0);
        QTest::keyClick(window, Qt::Key_Right);
        QTRY_COMPARE(viewer->property("currentIndex").toInt(), 1);
        QVERIFY(QMetaObject::invokeMethod(next, "clicked"));
        QCOMPARE(viewer->property("remote").toUrl(), QUrl("https://assets.test/gallery-third"));
        QCOMPARE(count->property("text").toString(), "3 из 3");
        QVERIFY(!next->property("enabled").toBool());
        QTRY_VERIFY(error->property("visible").toBool());
        QTest::keyClick(window, Qt::Key_Right);
        QCOMPARE(viewer->property("currentIndex").toInt(), 2);
        QVERIFY(QMetaObject::invokeMethod(previous, "clicked"));
        QTRY_COMPARE(photo->property("status").toInt(), 1);
        QVERIFY(!error->property("visible").toBool());

        QVERIFY(QMetaObject::invokeMethod(viewer, "close"));
        QTRY_VERIFY(!viewer->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(window, "presentMedia", Q_ARG(QVariant, "https://assets.test/gallery-first"), Q_ARG(QVariant, "photo"), Q_ARG(QVariant, "Одиночное фото")));
        QTRY_COMPARE(photo->property("status").toInt(), 1);
        QVERIFY(!previous->property("visible").toBool()); QVERIFY(!next->property("visible").toBool());
        QCOMPARE(viewer->property("currentIndex").toInt(), 0);
    }

    void photoGalleryThumbnailsStayBoundedAndFollowSelection()
    {
        QImage image(320, 240, QImage::Format_RGB32); image.fill(QColor("#547d83"));
        QByteArray png; QBuffer buffer(&png); QVERIFY(buffer.open(QIODevice::WriteOnly)); QVERIFY(image.save(&buffer, "PNG"));
        TokenNetwork network;
        QVariantList gallery;
        for (int i = 0; i < 200; ++i) {
            const auto source = QString("gallery-full-%1").arg(i);
            const auto thumbnail = QString("gallery-thumb-%1").arg(i);
            network.responses[source] = {png}; network.responses[thumbnail] = {png};
            gallery.append(QVariantMap{{"source", "https://assets.test/" + source}, {"thumbnail", "https://assets.test/" + thumbnail}, {"title", QString("Снимок %1").arg(i + 1)}});
        }
        Messenger messenger(nullptr, &network); messenger.startDemo();
        QQmlApplicationEngine engine; engine.rootContext()->setContextProperty("messenger", &messenger);
        engine.load(QUrl::fromLocalFile(QStringLiteral(NATIVE_QML_PATH))); QVERIFY(!engine.rootObjects().isEmpty());
        auto window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        window->resize(780, 560); window->requestActivate();
        // Instantiate the viewer through the same path as attachment actions.
        QVERIFY(QMetaObject::invokeMethod(window, "presentMedia", Q_ARG(QVariant, "https://assets.test/gallery-full-100"), Q_ARG(QVariant, "photo"), Q_ARG(QVariant, "Снимок 101")));
        auto viewer = window->findChild<QObject *>("mediaViewer"); QVERIFY(viewer);
        QVERIFY(QMetaObject::invokeMethod(viewer, "presentGallery", Q_ARG(QVariant, gallery), Q_ARG(QVariant, 100)));
        QTRY_VERIFY(viewer->property("opened").toBool());
        auto strip = viewer->findChild<QQuickItem *>("viewerThumbnails"); QVERIFY(strip);
        auto photo = viewer->findChild<QObject *>("viewerPhotograph"); QVERIFY(photo);
        QTRY_COMPARE(photo->property("status").toInt(), 1);
        const auto requestCount = [&](const QString &prefix) {
            return std::count_if(network.requests.begin(), network.requests.end(), [&](const auto &request) { return request.first.path().section('/', -1).startsWith(prefix); });
        };
        const auto thumbnails = [&] {
            QList<QQuickItem *> items;
            const auto visit = [&](auto &&self, QQuickItem *item) -> void {
                if (item->objectName() == "viewerThumbnail") items.append(item);
                for (auto child : item->childItems()) self(self, child);
            };
            visit(visit, strip); return items;
        };
        QTRY_VERIFY(requestCount("gallery-thumb-") > 0);
        QTest::qWait(100);
        QVERIFY(requestCount("gallery-thumb-") < 25);
        QCOMPARE(requestCount("gallery-full-"), 1);
        QVERIFY(thumbnails().size() < 25);
        const auto selectedVisible = [&] {
            for (const auto item : thumbnails()) {
                if (!item->property("selected").toBool() || !item->property("inViewport").toBool()) continue;
                const auto x = item->mapToItem(strip, QPointF()).x();
                if (x >= -1 && x + item->width() <= strip->width() + 1) return true;
            }
            return false;
        };
        QTRY_VERIFY(selectedVisible());
        QQuickItem *nextThumbnail = nullptr;
        for (auto item : thumbnails()) if (item->property("inViewport").toBool() && item->property("index").toInt() != 100) { nextThumbnail = item; break; }
        QVERIFY(nextThumbnail);
        const auto clickedIndex = nextThumbnail->property("index").toInt();
        viewer->setProperty("zoom", 3);
        QVERIFY(QMetaObject::invokeMethod(nextThumbnail, "clicked"));
        QTRY_COMPARE(viewer->property("currentIndex").toInt(), clickedIndex);
        QCOMPARE(viewer->property("zoom").toReal(), 1.0);
        QTRY_COMPARE(photo->property("status").toInt(), 1);
        window->requestActivate(); QTRY_VERIFY(window->isActive());
        QTest::keyClick(window, Qt::Key_End);
        QTRY_COMPARE(viewer->property("currentIndex").toInt(), 199);
        QTRY_VERIFY(selectedVisible());
        QTRY_COMPARE(photo->property("status").toInt(), 1);
        QTest::keyClick(window, Qt::Key_Home);
        QTRY_COMPARE(viewer->property("currentIndex").toInt(), 0);
        QTRY_VERIFY(selectedVisible());
        QTRY_COMPARE(photo->property("status").toInt(), 1);
        QCOMPARE(requestCount("gallery-full-"), 4);
        QVERIFY(requestCount("gallery-thumb-") < 50);
        QVERIFY(thumbnails().size() < 25);
        auto stage = viewer->findChild<QQuickItem *>("viewerStage"); QVERIFY(stage); QVERIFY(stage->height() > 250);
        QVERIFY(QMetaObject::invokeMethod(viewer, "close"));
        QTRY_VERIFY(!viewer->property("visible").toBool());
        QTRY_COMPARE(strip->property("count").toInt(), 0);
        QVERIFY(photo->property("source").toUrl().isEmpty());
    }

    void photoViewerZoomFullscreenAndLayout()
    {
        QImage image(1400, 900, QImage::Format_RGB32);
        QPainter painter(&image);
        QLinearGradient sky(0, 0, 0, 900); sky.setColorAt(0, QColor("#91bfd5")); sky.setColorAt(0.65, QColor("#ead5ba")); sky.setColorAt(1, QColor("#567d84"));
        painter.fillRect(image.rect(), sky); painter.setPen(Qt::NoPen);
        painter.setBrush(QColor("#f6e5bf")); painter.drawEllipse(QPointF(1040, 250), 90, 90);
        painter.setBrush(QColor("#547d83")); painter.drawPolygon(QPolygonF{QPointF(0, 590), QPointF(320, 300), QPointF(700, 650), QPointF(1050, 420), QPointF(1400, 620), QPointF(1400, 900), QPointF(0, 900)});
        painter.setBrush(QColor("#284e5b")); painter.drawPolygon(QPolygonF{QPointF(0, 760), QPointF(380, 530), QPointF(750, 780), QPointF(1150, 600), QPointF(1400, 730), QPointF(1400, 900), QPointF(0, 900)}); painter.end();
        QByteArray png; QBuffer buffer(&png); QVERIFY(buffer.open(QIODevice::WriteOnly)); QVERIFY(image.save(&buffer, "PNG"));
        TokenNetwork network; network.responses["landscape"] = {png};
        Messenger messenger(nullptr, &network); messenger.startDemo();
        QQmlApplicationEngine engine; engine.rootContext()->setContextProperty("messenger", &messenger);
        engine.load(QUrl::fromLocalFile(QStringLiteral(NATIVE_QML_PATH))); QVERIFY(!engine.rootObjects().isEmpty());
        auto window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        window->requestActivate(); QTRY_VERIFY(window->isActive());
        QVERIFY(QMetaObject::invokeMethod(window, "presentMedia", Q_ARG(QVariant, "https://assets.test/landscape"), Q_ARG(QVariant, "photo"), Q_ARG(QVariant, "Северное побережье")));
        auto viewer = window->findChild<QObject *>("mediaViewer"); QVERIFY(viewer);
        auto photo = viewer->findChild<QObject *>("viewerPhotograph"); QVERIFY(photo);
        auto flick = viewer->findChild<QQuickItem *>("photoFlick"); QVERIFY(flick);
        QTRY_COMPARE(photo->property("status").toInt(), 1);
        auto zoomIn = viewer->findChild<QObject *>("viewerZoomIn"); QVERIFY(zoomIn);
        QVERIFY(QMetaObject::invokeMethod(zoomIn, "clicked")); QCOMPARE(viewer->property("zoom").toReal(), 1.25);
        const auto point = flick->mapToScene(QPointF(flick->width() / 2, flick->height() / 2)).toPoint();
        QWheelEvent wheel(QPointF(point), QPointF(window->mapToGlobal(point)), QPoint(), QPoint(0, 120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(window, &wheel); QTRY_VERIFY(viewer->property("zoom").toReal() > 1.25);
        auto fit = viewer->findChild<QObject *>("viewerFit"); QVERIFY(fit); QVERIFY(QMetaObject::invokeMethod(fit, "clicked"));
        QCOMPARE(viewer->property("zoom").toReal(), 1.0); QCOMPARE(flick->property("contentX").toReal(), 0.0);
        const QVariantList gallery{QVariantMap{{"source", "https://assets.test/landscape"}, {"title", "Северное побережье"}}, QVariantMap{{"source", "https://assets.test/landscape"}, {"title", "Вторая фотография"}}};
        QVERIFY(QMetaObject::invokeMethod(viewer, "presentGallery", Q_ARG(QVariant, gallery), Q_ARG(QVariant, 0)));
        window->resize(780, 560); QTest::qWait(100);
        auto stage = viewer->findChild<QQuickItem *>("viewerStage"); QVERIFY(stage); QVERIFY(stage->height() > 250); QVERIFY(stage->width() <= 780);
        QVERIFY(QMetaObject::invokeMethod(viewer, "toggleFullscreen")); QTRY_VERIFY(viewer->property("fullscreen").toBool());
        QCOMPARE(window->visibility(), QWindow::FullScreen);
        window->requestActivate(); QTRY_VERIFY(window->isActive()); QTRY_VERIFY(viewer->property("opened").toBool());
        QTest::keyClick(window, Qt::Key_Escape); QTRY_VERIFY(!viewer->property("fullscreen").toBool()); QVERIFY(viewer->property("opened").toBool());
        window->resize(1180, 780); QTest::qWait(100);
        if (qEnvironmentVariableIsSet("VKM_PHOTO_VIEWER_SCREENSHOT")) QVERIFY(window->grabWindow().save(qEnvironmentVariable("VKM_PHOTO_VIEWER_SCREENSHOT")));
        QTest::keyClick(window, Qt::Key_Escape); QTRY_VERIFY(!viewer->property("visible").toBool());
        QVERIFY(photo->property("source").toUrl().isEmpty());
    }

    void videoAttachmentPlaysSeeksAndStopsOnClose()
    {
        QFile fixture(QFINDTESTDATA("fixtures/viewer-video.mp4")); QVERIFY(fixture.open(QIODevice::ReadOnly));
        TokenNetwork network; network.responses["movie.mp4"] = {fixture.readAll()};
        Messenger messenger(nullptr, &network); messenger.startDemo();
        const QUrl remote("https://assets.test/movie.mp4");
        messenger.media()->source(remote); QTRY_VERIFY(!messenger.media()->cached(remote).isEmpty());
        QQmlApplicationEngine engine; engine.rootContext()->setContextProperty("messenger", &messenger);
        engine.load(QUrl::fromLocalFile(QStringLiteral(NATIVE_QML_PATH))); QVERIFY(!engine.rootObjects().isEmpty());
        auto window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window); window->requestActivate(); QTRY_VERIFY(window->isActive());
        const QVariantMap attachment{{"type", "video"}, {"video", QVariantMap{{"title", "Тестовый ролик"}, {"files", QVariantMap{{"mp4_1080", remote.toString()}}}}}};
        messenger.actions()->openAttachment(attachment);
        auto viewer = window->findChild<QObject *>("mediaViewer"); QVERIFY(viewer);
        auto player = viewer->findChild<QMediaPlayer *>("viewerPlayer"); QVERIFY(player);
        auto output = viewer->findChild<QQuickItem *>("viewerVideoOutput"); QVERIFY(output);
        auto sink = output->property("videoSink").value<QVideoSink *>(); QVERIFY(sink);
        QTRY_COMPARE(player->playbackState(), QMediaPlayer::PlayingState);
        QTRY_VERIFY(player->hasVideo() && sink->videoFrame().isValid());
        QTRY_VERIFY(!sink->videoFrame().toImage().isNull());
        QTRY_VERIFY(viewer->property("opened").toBool());
        QTRY_VERIFY(player->duration() >= 4900 && player->duration() <= 5100); QVERIFY(player->isSeekable());
        auto play = viewer->findChild<QObject *>("viewerPlayPause"); QVERIFY(play); QVERIFY(QMetaObject::invokeMethod(play, "clicked"));
        QTRY_COMPARE(player->playbackState(), QMediaPlayer::PausedState);
        QVERIFY(QMetaObject::invokeMethod(viewer, "seekTo", Q_ARG(QVariant, 2000))); QTRY_VERIFY(qAbs(player->position() - 2000) < 200);
        auto speed = viewer->findChild<QObject *>("viewerSpeed"); QVERIFY(speed); speed->setProperty("currentIndex", 3);
        QVERIFY(QMetaObject::invokeMethod(speed, "activated", Q_ARG(int, 3))); QCOMPARE(player->playbackRate(), 1.5);
        auto sound = viewer->findChild<QAudioOutput *>("viewerAudioOutput"); QVERIFY(sound);
        auto mute = viewer->findChild<QObject *>("viewerMute"); QVERIFY(mute); QVERIFY(QMetaObject::invokeMethod(mute, "clicked")); QVERIFY(sound->isMuted());
        QVERIFY(QMetaObject::invokeMethod(mute, "clicked")); QVERIFY(!sound->isMuted());
        QTest::qWait(100);
        const auto stage = viewer->findChild<QQuickItem *>("viewerStage"); QVERIFY(stage);
        const auto sample = stage->mapToScene(QPointF(stage->width() / 2 + 120, stage->height() / 2)).toPoint();
        const auto pixels = window->grabWindow(); QVERIFY(!pixels.isNull());
        const auto color = pixels.pixelColor(sample * window->devicePixelRatio());
        QVERIFY2(color.red() + color.green() + color.blue() > 80, "Decoded video must be painted into the window, not just delivered to the sink");
        if (qEnvironmentVariableIsSet("VKM_VIDEO_PLAYER_SCREENSHOT")) {
            QTest::qWait(150);
            QVERIFY(window->grabWindow().save(qEnvironmentVariable("VKM_VIDEO_PLAYER_SCREENSHOT")));
        }
        QVERIFY(QMetaObject::invokeMethod(play, "clicked")); QTRY_COMPARE(player->playbackState(), QMediaPlayer::PlayingState);
        QVERIFY(QMetaObject::invokeMethod(viewer, "seekTo", Q_ARG(QVariant, 4900))); QTRY_COMPARE(player->mediaStatus(), QMediaPlayer::EndOfMedia);
        QVERIFY(QMetaObject::invokeMethod(play, "clicked")); QTRY_COMPARE(player->playbackState(), QMediaPlayer::PlayingState); QVERIFY(player->position() < 2000);
        QVERIFY(QMetaObject::invokeMethod(window, "presentMedia", Q_ARG(QVariant, ""), Q_ARG(QVariant, "photo"), Q_ARG(QVariant, "Фото")));
        QVERIFY(player->source().isEmpty()); QCOMPARE(player->playbackState(), QMediaPlayer::StoppedState);
        messenger.actions()->openAttachment(attachment); QTRY_COMPARE(player->playbackState(), QMediaPlayer::PlayingState);
        QVERIFY(QMetaObject::invokeMethod(viewer, "close")); QTRY_VERIFY(!viewer->property("visible").toBool());
        QCOMPARE(player->playbackState(), QMediaPlayer::StoppedState); QVERIFY(player->source().isEmpty());
    }

    void videoPlayerRecoversFromDecodeError()
    {
        QTemporaryDir media; const auto path = media.filePath("broken.mp4"); QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("not a video"); file.close();
        Messenger messenger; messenger.startDemo();
        QQmlApplicationEngine engine; engine.rootContext()->setContextProperty("messenger", &messenger);
        engine.load(QUrl::fromLocalFile(QStringLiteral(NATIVE_QML_PATH))); QVERIFY(!engine.rootObjects().isEmpty());
        auto window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        QVERIFY(QMetaObject::invokeMethod(window, "presentMedia", Q_ARG(QVariant, QUrl::fromLocalFile(path)), Q_ARG(QVariant, "video"), Q_ARG(QVariant, "Повреждённое видео")));
        auto viewer = window->findChild<QObject *>("mediaViewer"); QVERIFY(viewer);
        auto player = viewer->findChild<QMediaPlayer *>("viewerPlayer"); QVERIFY(player);
        auto error = viewer->findChild<QQuickItem *>("viewerError"); QVERIFY(error);
        auto retry = viewer->findChild<QObject *>("viewerRetry"); QVERIFY(retry);
        QTRY_VERIFY(player->error() != QMediaPlayer::NoError); QTRY_VERIFY(error->isVisible());
        QFile fixture(QFINDTESTDATA("fixtures/viewer-video.mp4")); QVERIFY(fixture.open(QIODevice::ReadOnly));
        QVERIFY(file.open(QIODevice::WriteOnly)); file.write(fixture.readAll()); file.close();
        QVERIFY(QMetaObject::invokeMethod(retry, "clicked")); QTRY_COMPARE(player->playbackState(), QMediaPlayer::PlayingState);
        QVERIFY(!error->isVisible()); QVERIFY(QMetaObject::invokeMethod(viewer, "close"));
    }

    void transferErrorRemainsReadableInNarrowWindow()
    {
        Messenger messenger;
        QQmlApplicationEngine engine; engine.rootContext()->setContextProperty("messenger", &messenger);
        engine.load(QUrl::fromLocalFile(QStringLiteral(NATIVE_QML_PATH))); QVERIFY(!engine.rootObjects().isEmpty());
        auto window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        messenger.startDemo(); window->resize(780, 560);
        messenger.transfers()->restoreState(QJsonObject{{"jobs", QJsonArray{QJsonObject{{"file", "file:///tmp/mock-upload.png"}, {"peer", "1"}, {"kind", "photo"}, {"name", "Фото.png"}}}}});
        const QString error = "Сервер передачи (pu.vk.ru) вернул HTTP 403. Повторите загрузку. Подготовленный файл сохранён, выбирать его заново не нужно.";
        messenger.transfers()->setProperty("error", error);
        auto status = window->findChild<QQuickItem *>("transferStatus"); QVERIFY(status);
        QTRY_VERIFY(status->isVisible()); QTest::qWait(100);
        QCOMPARE(status->property("text").toString(), error);
        QVERIFY(status->property("lineCount").toInt() > 1); QVERIFY(!status->property("truncated").toBool());
        QVERIFY(status->height() >= status->property("contentHeight").toReal() - 1);
        QVERIFY(status->mapToScene(QPointF(status->width(), 0)).x() < window->width());
        if (qEnvironmentVariableIsSet("VKM_UPLOAD_ERROR_SCREENSHOT")) QVERIFY(window->grabWindow().save(qEnvironmentVariable("VKM_UPLOAD_ERROR_SCREENSHOT")));
    }

    void transferCardsSeparateDirectionsAndShowProgress()
    {
        QTemporaryDir files;
        QFile input(files.filePath("Отчёт.txt")); QVERIFY(input.open(QIODevice::WriteOnly)); input.write("fixture payload"); input.close();
        TokenNetwork network;
        Messenger messenger(nullptr, &network); messenger.startDemo();
        QQmlApplicationEngine engine; engine.rootContext()->setContextProperty("messenger", &messenger);
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        engine.load(QUrl::fromLocalFile(QStringLiteral(NATIVE_QML_PATH))); QVERIFY(!engine.rootObjects().isEmpty());
        auto window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window); window->resize(780, 560);
        auto uploads = window->findChild<QQuickItem *>("uploadTransferCard"); QVERIFY(uploads);
        auto downloads = window->findChild<QQuickItem *>("downloadTransferCard"); QVERIFY(downloads);
        auto uploadBar = uploads->findChild<QQuickItem *>("uploadTransferProgress"); QVERIFY(uploadBar);
        auto downloadBar = downloads->findChild<QQuickItem *>("downloadTransferProgress"); QVERIFY(downloadBar);
        auto uploadCancel = uploads->findChild<QObject *>("uploadTransferCancel"); QVERIFY(uploadCancel);
        auto downloadCancel = downloads->findChild<QObject *>("downloadTransferCancel"); QVERIFY(downloadCancel);
        auto transfers = messenger.transfers();
        transfers->upload(QUrl::fromLocalFile(input.fileName()), "1", "doc");
        transfers->upload(QUrl::fromLocalFile(input.fileName()), "2", "doc");
        QTRY_VERIFY(transfers->busy()); QTRY_VERIFY(uploads->isVisible());
        transfers->setProperty("sentBytes", 1048576); transfers->setProperty("totalBytes", 4194304); transfers->setProperty("progress", 0.25);
        QTRY_COMPARE(uploadBar->property("value").toDouble(), 0.25); QVERIFY(!uploadBar->property("indeterminate").toBool());
        transfers->download(QUrl("https://assets.test/large"), QUrl::fromLocalFile(files.filePath("Скачанный файл.bin")));
        QTRY_VERIFY(downloads->isVisible()); QVERIFY(downloadBar->property("indeterminate").toBool());
        QTRY_COMPARE(network.replies["large"].size(), 1);
        network.replies["large"].first()->downloadProgress(5242880, 10485760);
        QTRY_COMPARE(downloadBar->property("value").toDouble(), 0.5); QVERIFY(!downloadBar->property("indeterminate").toBool());
        transfers->setProperty("progress", 1.0); QTRY_COMPARE(uploadBar->property("value").toDouble(), 1.0);
        QVERIFY(!uploadBar->property("indeterminate").toBool()); // 100% is determinate while VK saves the attachment.
        QVERIFY(QMetaObject::invokeMethod(downloadCancel, "clicked")); QTRY_VERIFY(!downloads->isVisible());
        QVERIFY(transfers->busy()); QCOMPARE(transfers->queuedCount(), 2);
        transfers->download(QUrl("https://assets.test/another"), QUrl::fromLocalFile(files.filePath("Другой файл.bin")));
        QVERIFY(QMetaObject::invokeMethod(uploadCancel, "clicked")); QTRY_VERIFY(!uploads->isVisible()); QVERIFY(transfers->downloading());
        QVERIFY(QMetaObject::invokeMethod(downloadCancel, "clicked"));
        const QJsonObject queued{{"jobs", QJsonArray{QJsonObject{{"file", QUrl::fromLocalFile(input.fileName()).toString()}, {"peer", "1"}, {"kind", "doc"}}}}};
        transfers->restoreState(queued);
        transfers->setProperty("filename", "Документ с очень длинным названием для проверки переноса.txt");
        const QString uploadError = "Сервер передачи (pu.vk.ru) вернул HTTP 403. Повторите загрузку. Подготовленный файл сохранён, выбирать его заново не нужно.";
        transfers->setError(uploadError);
        transfers->download(QUrl("https://assets.test/retry"), QUrl::fromLocalFile(files.filePath("missing/Документ.bin")));
        QTRY_VERIFY(uploads->isVisible()); QTRY_VERIFY(downloads->isVisible());
        QCOMPARE(uploads->property("error").toString(), uploadError);
        QVERIFY(!downloads->property("error").toString().isEmpty());
        auto copy = uploads->findChild<QQuickItem *>("uploadTransferCopyError"); QVERIFY(copy); QVERIFY(copy->isVisible());
        const auto accessible = QAccessible::queryAccessibleInterface(copy); QVERIFY(accessible);
        QCOMPARE(accessible->text(QAccessible::Name), QString("Скопировать ошибку передачи"));
        auto status = uploads->findChild<QQuickItem *>("transferStatus"); QVERIFY(status);
        auto history = window->findChild<QQuickItem *>("historyList"); QVERIFY(history);
        for (bool dark : {false, true}) {
            messenger.setDarkTheme(dark); QTest::qWait(50);
            QVERIFY(status->height() >= status->property("contentHeight").toReal() - 1);
            QVERIFY(history->height() > 100);
            if (qEnvironmentVariableIsSet("VKM_TRANSFER_CARDS_SCREENSHOT"))
                QVERIFY(window->grabWindow().save(qEnvironmentVariable("VKM_TRANSFER_CARDS_SCREENSHOT") + (dark ? "-dark.png" : "-light.png")));
        }
        QVERIFY(QMetaObject::invokeMethod(uploads->findChild<QObject *>("uploadTransferRetry"), "clicked"));
        QTRY_VERIFY(transfers->busy()); QVERIFY(!downloads->property("error").toString().isEmpty());
        QVERIFY(QDir().mkpath(files.filePath("missing")));
        QVERIFY(QMetaObject::invokeMethod(downloads->findChild<QObject *>("downloadTransferRetry"), "clicked"));
        QTRY_VERIFY(transfers->downloading()); QVERIFY(transfers->busy());
        transfers->cancel(); transfers->cancelDownload(); QCOMPARE(warnings.count(), 0);
    }

    void photoViewerRecoversFromNetworkAndDecodeErrors()
    {
        TokenNetwork network;
        Messenger messenger(nullptr, &network); messenger.startDemo();
        QQmlApplicationEngine engine; engine.rootContext()->setContextProperty("messenger", &messenger);
        engine.load(QUrl::fromLocalFile(QStringLiteral(NATIVE_QML_PATH))); QVERIFY(!engine.rootObjects().isEmpty());
        auto root = engine.rootObjects().first();
        const auto present = [&](const QString &url) {
            return QMetaObject::invokeMethod(root, "presentMedia", Q_ARG(QVariant, url), Q_ARG(QVariant, "photo"), Q_ARG(QVariant, "Фотография"));
        };
        QVERIFY(present("https://assets.test/viewer-photo"));
        auto viewer = root->findChild<QObject *>("mediaViewer"); QVERIFY(viewer);
        auto loading = viewer->findChild<QObject *>("viewerLoading"); QVERIFY(loading);
        auto error = viewer->findChild<QObject *>("viewerError"); QVERIFY(error);
        auto retry = viewer->findChild<QObject *>("viewerRetry"); QVERIFY(retry);
        auto photo = viewer->findChild<QObject *>("viewerPhotograph"); QVERIFY(photo);
        auto flick = viewer->findChild<QObject *>("photoFlick"); QVERIFY(flick);
        QTRY_COMPARE(network.replies["viewer-photo"].size(), 1);
        QVERIFY(loading->property("running").toBool());
        network.replies["viewer-photo"].first()->deliver("#offline");
        QTRY_VERIFY(error->property("visible").toBool());
        QVERIFY(!loading->property("running").toBool());
        QVERIFY(retry->property("visible").toBool());

        network.responses["viewer-photo"] = {"invalid-image"};
        QVERIFY(QMetaObject::invokeMethod(retry, "clicked"));
        QTRY_COMPARE(photo->property("status").toInt(), 3); // Image.Error
        QVERIFY(messenger.media()->error(QUrl("https://assets.test/viewer-photo")).isEmpty());
        QVERIFY(error->property("visible").toBool());
        QVERIFY(!loading->property("running").toBool());
        QVERIFY(retry->property("visible").toBool());

        QImage image(32, 32, QImage::Format_ARGB32_Premultiplied); image.fill(Qt::blue);
        QByteArray png; QBuffer buffer(&png); QVERIFY(buffer.open(QIODevice::WriteOnly)); QVERIFY(image.save(&buffer, "PNG"));
        network.responses["viewer-photo"] = {png};
        QVERIFY(QMetaObject::invokeMethod(retry, "clicked"));
        QTRY_COMPARE(photo->property("status").toInt(), 1); // Image.Ready, same cache path
        QCOMPARE(network.replies["viewer-photo"].size(), 3);
        QVERIFY(!error->property("visible").toBool());
        QVERIFY(!retry->property("visible").toBool());
        QVERIFY(!loading->property("running").toBool());

        viewer->setProperty("zoom", 3);
        QTRY_VERIFY(flick->property("contentWidth").toReal() > flick->property("width").toReal());
        flick->setProperty("contentX", 100); flick->setProperty("contentY", 100);
        QCOMPARE(flick->property("contentX").toReal(), 100.0);
        QVERIFY(present("https://assets.test/next-photo"));
        QCOMPARE(viewer->property("zoom").toReal(), 1.0);
        QCOMPARE(flick->property("contentX").toReal(), 0.0);
        QCOMPARE(flick->property("contentY").toReal(), 0.0);
        QTRY_VERIFY(loading->property("running").toBool());
        QVERIFY(QMetaObject::invokeMethod(viewer, "close"));
        QTRY_VERIFY(!viewer->property("visible").toBool());
        QVERIFY(!loading->property("running").toBool());

        QVERIFY(present("https://assets.test/viewer-photo"));
        QTRY_COMPARE(photo->property("status").toInt(), 1);
        QCOMPARE(network.replies["viewer-photo"].size(), 3); // reopen from disk without a new download
        QVERIFY(present(""));
        QTRY_VERIFY(error->property("visible").toBool());
        QVERIFY(!loading->property("running").toBool());
        QVERIFY(!retry->property("visible").toBool());
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
        const QByteArray catalog = "{\"response\":{\"assets\":[{\"reaction_id\":1,\"links\":{\"static\":\"https://assets.test/base.png\"}},{\"reaction_id\":2,\"links\":{\"static\":\"https://assets.test/second.webp\"}},{\"reaction_id\":3,\"links\":{\"static\":\"https://assets.test/third.svg\"}},{\"reaction_id\":4,\"links\":{\"static\":\"https://assets.test/hidden.png\"}}],\"override_assets\":[{\"reaction_id\":1,\"links\":{\"static\":\"https://assets.test/override.png\"}}],\"reaction_ids\":[1,2,3]}}";
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

    void folderChoicesKeepSavedPeersAndTrackLiveSource()
    {
        Messenger messenger;
        auto source = messenger.conversations(); auto choices = messenger.folderChoices();
        QAbstractItemModelTester tester(choices, QAbstractItemModelTester::FailureReportingMode::QtTest);
        QVariantList rows{QVariantMap{{"id", "1"}, {"peer", "1"}, {"title", "Анна"}}, QVariantMap{{"id", "2"}, {"peer", "2"}, {"title", "Архив"}, {"archived", true}}};
        source->replace(rows);
        choices->setSavedPeers({"2", "999", "999", "-5", "0", "invalid"});
        QCOMPARE(choices->columnCount(), 1); QCOMPARE(choices->rowCount(), 4);
        QCOMPARE(choices->get(2).value("peer").toString(), "999"); QVERIFY(choices->get(2).value("missing").toBool());
        choices->setQuery("  АННА  "); QCOMPARE(choices->rowCount(), 1);
        auto anna = rows[0].toMap(); anna["title"] = "Другое имя"; rows[0] = anna; source->reconcile(rows);
        QCOMPARE(choices->rowCount(), 0);
        choices->setQuery("сохранённая"); QCOMPARE(choices->rowCount(), 2);
        rows.append(QVariantMap{{"id", "999"}, {"peer", "999"}, {"title", "Новая переписка"}}); source->reconcile(rows);
        QTRY_COMPARE(choices->rowCount(), 1);
        choices->setQuery(""); QCOMPARE(choices->rowCount(), 4);
        int matches = 0; for (int i = 0; i < choices->rowCount(); ++i) if (choices->get(i).value("peer").toString() == "999") { ++matches; QCOMPARE(choices->get(i).value("title").toString(), "Новая переписка"); }
        QCOMPARE(matches, 1);
        rows.removeAt(1); source->reconcile(rows); QTRY_COMPARE(choices->rowCount(), 4);
        choices->setQuery("сохранённая"); QTRY_COMPARE(choices->rowCount(), 2); // removed archive remains selected and available
        choices->setSavedPeers({}); QTRY_COMPARE(choices->rowCount(), 0);
        choices->setQuery(""); QCOMPARE(choices->rowCount(), 2);
        source->replace({}); QCOMPARE(choices->rowCount(), 0); QVERIFY(choices->get(-1).isEmpty()); QVERIFY(choices->get(0).isEmpty());
    }

    void folderEditorFiltersLargeListsAndPreservesSelection()
    {
        Messenger messenger; messenger.startDemo();
        QQmlApplicationEngine engine; engine.rootContext()->setContextProperty("messenger", &messenger);
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        engine.load(QUrl::fromLocalFile(QStringLiteral(NATIVE_QML_PATH))); QVERIFY(!engine.rootObjects().isEmpty());
        auto window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window); window->resize(780, 560); window->requestActivate(); QTRY_VERIFY(window->isActive());
        QVariantList rows;
        for (int i = 1; i <= 10000; ++i) rows.append(QVariantMap{{"id", QString::number(i)}, {"peer", QString::number(i)}, {"title", QString("Переписка %1").arg(i)}, {"preview", "Последнее сообщение"}, {"time", ""}});
        messenger.conversations()->replace(rows); messenger.filteredConversations()->setQuery("Не совпадает");
        const QJsonObject catalog{{"items", QJsonArray{QJsonObject{{"id", 8}, {"name", "Работа"}, {"flags", 0}, {"included_peer_ids", QJsonArray{1, 99999}}}}}};
        messenger.folders()->restore(catalog);
        auto sheet = window->findChild<QObject *>("folderSheet"); QVERIFY(sheet); sheet->setProperty("reducedMotion", true);
        QVERIFY(QMetaObject::invokeMethod(sheet, "show"));
        QVERIFY(QMetaObject::invokeMethod(sheet, "edit", Q_ARG(QVariant, messenger.folders()->get("8"))));
        auto results = sheet->findChild<QQuickItem *>("folderResults"); QVERIFY(results);
        auto query = sheet->findChild<QQuickItem *>("folderSearch"); QVERIFY(query);
        auto count = sheet->findChild<QQuickItem *>("folderSelectionCount"); QVERIFY(count);
        auto empty = sheet->findChild<QQuickItem *>("folderEmptyState"); QVERIFY(empty);
        QTRY_COMPARE(results->property("count").toInt(), 10001);
        QCOMPARE(qvariant_cast<QObject *>(results->property("model")), messenger.folderChoices());
        const auto content = results->property("contentItem").value<QQuickItem *>(); QVERIFY(content);
        QVERIFY(content->childItems().size() < 50); QCOMPARE(count->property("text").toString(), "Выбрано переписок: 2");
        QSignalSpy filtered(messenger.folderChoices(), &FolderPeerModel::queryChanged);
        query->forceActiveFocus();
        for (const auto &text : {"П", "Пе", "Пер", "  ПЕРЕПИСКА 9999  "}) query->setProperty("text", text);
        QTRY_COMPARE(results->property("count").toInt(), 1); QCOMPARE(filtered.count(), 1);
        QTRY_VERIFY(results->property("contentHeight").toReal() <= 62);
        QTest::keyClick(window, Qt::Key_Down); QTRY_VERIFY(results->hasActiveFocus());
        QTest::keyClick(window, Qt::Key_Space); QTRY_COMPARE(count->property("text").toString(), "Выбрано переписок: 3");
        QTest::keyClick(window, Qt::Key_Escape); QTRY_VERIFY(query->hasActiveFocus());
        QTest::keyClick(window, Qt::Key_Escape); QTRY_COMPARE(results->property("count").toInt(), 10001);
        QInputMethodEvent composing(QStringLiteral("Пер"), {}); QCoreApplication::sendEvent(query, &composing);
        QVERIFY(query->property("inputMethodComposing").toBool()); QTest::qWait(170); QCOMPARE(messenger.folderChoices()->query(), QString());
        QInputMethodEvent committed; committed.setCommitString(QStringLiteral("Переписка 5000")); QCoreApplication::sendEvent(query, &committed);
        QTRY_COMPARE(results->property("count").toInt(), 1);
        QVERIFY(QMetaObject::invokeMethod(sheet->findChild<QObject *>("clearFolderSearch"), "clicked"));
        QTRY_COMPARE(results->property("count").toInt(), 10001);
        results->forceActiveFocus(); QTest::keyClick(window, Qt::Key_End); QTest::keyClick(window, Qt::Key_Space);
        QTRY_COMPARE(count->property("text").toString(), "Выбрано переписок: 2");
        QCOMPARE(results->property("count").toInt(), 10001); // deselected missing peer can be selected again
        QTest::keyClick(window, Qt::Key_Return); QTRY_COMPARE(count->property("text").toString(), "Выбрано переписок: 3");
        rows.append(QVariantMap{{"id", "99999"}, {"peer", "99999"}, {"title", "Новая переписка"}, {"time", ""}}); messenger.conversations()->reconcile(rows);
        QTRY_COMPARE(messenger.folderChoices()->rowCount(), 10001);
        query->setProperty("text", "Новая"); QTRY_COMPARE(results->property("count").toInt(), 1);
        QCOMPARE(messenger.folderChoices()->get(0).value("peer").toString(), "99999");
        query->setProperty("text", "Несуществующее имя"); QTRY_COMPARE(results->property("count").toInt(), 0); QVERIFY(empty->isVisible());
        QVERIFY(QMetaObject::invokeMethod(sheet->findChild<QObject *>("clearFolderSearch"), "clicked")); QTRY_COMPARE(results->property("count").toInt(), 10001);
        QTest::keyClick(window, Qt::Key_Down);
        for (bool dark : {false, true}) {
            messenger.setDarkTheme(dark); QTest::qWait(30);
            QVERIFY(results->height() > 100);
            if (qEnvironmentVariableIsSet("VKM_FOLDER_EDITOR_SCREENSHOT")) QVERIFY(window->grabWindow().save(qEnvironmentVariable("VKM_FOLDER_EDITOR_SCREENSHOT") + (dark ? "-dark.png" : "-light.png")));
        }
        QVERIFY(QMetaObject::invokeMethod(sheet->findChild<QObject *>("clearFolderSelection"), "clicked"));
        QTRY_COMPARE(count->property("text").toString(), "Выбрано переписок: 0");
        QCOMPARE(messenger.folders()->get("8").value("peers").toStringList(), (QStringList{"1", "99999"}));
        QVERIFY(QMetaObject::invokeMethod(sheet, "close")); QTRY_COMPARE(results->property("count").toInt(), 0);
        QVERIFY(messenger.folderChoices()->savedPeers().isEmpty());
        QVERIFY(QMetaObject::invokeMethod(sheet, "show"));
        QVERIFY(QMetaObject::invokeMethod(sheet, "edit", Q_ARG(QVariant, messenger.folders()->get("8"))));
        messenger.logout(); QTRY_VERIFY(!sheet->property("visible").toBool());
        QVERIFY(messenger.folderChoices()->savedPeers().isEmpty()); QCOMPARE(warnings.count(), 0);
    }

    void folderEditorSavesLoadedAndMissingPeers()
    {
        TokenNetwork network;
        network.responses["messages.getDiff"] = {R"({"response":{"server_version":"500","credentials":{"server_lp":"lp.test/im","key":"key","ts":"100"},"conversations_info":[{"conversation":{"peer":{"id":77}},"message":[]},{"conversation":{"peer":{"id":88}},"message":[]}]}})"};
        const QByteArray catalog = R"({"response":{"items":[{"id":8,"name":"Работа","flags":0,"included_peer_ids":[77,999]}]}})";
        network.responses["messages.getFolders"] = {catalog, catalog}; network.responses["messages.updateFolder"] = {R"({"response":1})"};
        Messenger messenger(nullptr, &network); messenger.loginWithToken("test-token"); QTRY_COMPARE(messenger.folders()->items().size(), 1);
        QQmlApplicationEngine engine; engine.rootContext()->setContextProperty("messenger", &messenger);
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        engine.load(QUrl::fromLocalFile(QStringLiteral(NATIVE_QML_PATH))); QVERIFY(!engine.rootObjects().isEmpty());
        auto sheet = engine.rootObjects().first()->findChild<QObject *>("folderSheet"); QVERIFY(sheet); sheet->setProperty("reducedMotion", true);
        QVERIFY(QMetaObject::invokeMethod(sheet, "show")); QTRY_VERIFY(!messenger.folders()->busy());
        QVERIFY(QMetaObject::invokeMethod(sheet, "edit", Q_ARG(QVariant, messenger.folders()->get("8"))));
        QVERIFY(QMetaObject::invokeMethod(sheet, "toggle", Q_ARG(QVariant, "77")));
        QVERIFY(QMetaObject::invokeMethod(sheet, "toggle", Q_ARG(QVariant, "88")));
        sheet->findChild<QObject *>("folderName")->setProperty("text", "Команда");
        auto save = sheet->findChild<QObject *>("saveFolder"); QVERIFY(save); QTRY_VERIFY(save->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(save, "clicked")); QTRY_VERIFY(!sheet->property("editing").toBool());
        QCOMPARE(messenger.folders()->get("8").value("peers").toStringList(), (QStringList{"999", "88"}));
        bool sent = false;
        for (const auto &request : network.requests) if (request.first.path().endsWith("messages.updateFolder")) {
            const QUrlQuery body(QString::fromUtf8(request.second));
            QCOMPARE(body.queryItemValue("add_included_peer_ids"), "88"); QCOMPARE(body.queryItemValue("remove_included_peer_ids"), "77"); sent = true;
        }
        QVERIFY(sent); QCOMPARE(warnings.count(), 0);
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
            folders->setSelected("1");
            QCOMPARE(messenger.filteredConversations()->rowCount(), 1);
            QCOMPARE(messenger.filteredConversations()->get(0).value("peer").toString(), "77");
            folders->setSelected("2");
            QCOMPARE(messenger.filteredConversations()->rowCount(), 1);
            QCOMPARE(messenger.filteredConversations()->get(0).value("peer").toString(), "88");
            folders->setSelected("");
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
        auto model = qobject_cast<ItemModel *>(actions->messageModel()); QVERIFY(model);
        QAbstractItemModelTester modelTester(model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        actions->search("Needle", "");
        QTRY_COMPARE(network.replies["messages.search"].size(), 1);
        QTRY_VERIFY(actions->cachedResults()); QCOMPARE(actions->results().size(), 50);
        QJsonArray firstPage;
        for (int i = 201; i <= 250; ++i) firstPage.append(QJsonObject{{"peer_id", 77}, {"conversation_message_id", i}, {"text", "Needle server"}});
        network.replies["messages.search"][0]->deliver(QJsonDocument(QJsonObject{{"response", QJsonObject{{"items", firstPage}}}}).toJson());
        QVERIFY(!actions->cachedResults()); QVERIFY(actions->hasMore());
        QCOMPARE(actions->results().first().toMap().value("cmid").toString(), "201");
        QCOMPARE(model->rowCount(), 50);
        QPersistentModelIndex retained(model->index(49));
        QSignalSpy resets(model, &QAbstractItemModel::modelReset);
        QSignalSpy inserts(model, &QAbstractItemModel::rowsInserted);
        QSignalSpy changes(model, &QAbstractItemModel::dataChanged);
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
        QCOMPARE(model->rowCount(), 52); QCOMPARE(resets.size(), 0); QCOMPARE(inserts.size(), 1); QCOMPARE(changes.size(), 1);
        QVERIFY(retained.isValid()); QCOMPARE(retained.row(), 49);
        QCOMPARE(retained.data(Qt::UserRole).toMap().value("text").toString(), "Updated");
        QCOMPARE(model->get(50).value("identity").toString(), "88:250");
        QStringList offsets;
        for (const auto &request : network.requests) if (request.first.path().endsWith("messages.search"))
            offsets.append(QUrlQuery(QString::fromUtf8(request.second)).queryItemValue("offset"));
        QCOMPARE(offsets, (QStringList{"0", "50"}));
    }

    void searchResultsKeepCardsScrollAndFocusWhenAppending()
    {
        TokenNetwork network;
        Messenger messenger(nullptr, &network); messenger.loginWithToken("test-token"); QTRY_VERIFY(messenger.connected());
        QQmlApplicationEngine engine; engine.rootContext()->setContextProperty("messenger", &messenger);
        QSignalSpy qmlWarnings(&engine, &QQmlEngine::warnings);
        engine.load(QUrl::fromLocalFile(QStringLiteral(NATIVE_QML_PATH))); QVERIFY(!engine.rootObjects().isEmpty());
        auto window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        window->resize(780, 560); window->requestActivate(); QTRY_VERIFY(window->isActive());
        auto sheet = window->findChild<QObject *>("actionSheet"); QVERIFY(sheet); sheet->setProperty("reducedMotion", true);
        auto results = sheet->findChild<QQuickItem *>("actionResults"); QVERIFY(results);
        auto query = sheet->findChild<QQuickItem *>("actionSearch"); QVERIFY(query);
        auto empty = sheet->findChild<QQuickItem *>("actionEmptyState"); QVERIFY(empty);
        auto model = qobject_cast<ItemModel *>(messenger.actions()->messageModel()); QVERIFY(model);
        const auto response = [](int from, int to) {
            QJsonArray items;
            for (int i = from; i <= to; ++i) items.append(QJsonObject{{"peer_id", 77}, {"conversation_message_id", i}, {"from_id", 42}, {"date", 1794323860 + i * 60}, {"text", QString("Обсуждение проекта: сообщение %1").arg(i)}});
            const QJsonArray profiles{QJsonObject{{"id", 42}, {"first_name", "Анна"}, {"last_name", "Смирнова"}}};
            return QJsonDocument(QJsonObject{{"response", QJsonObject{{"items", items}, {"profiles", profiles}}}}).toJson();
        };
        QVERIFY(QMetaObject::invokeMethod(sheet, "show", Q_ARG(QVariant, "search"), Q_ARG(QVariant, "77"), Q_ARG(QVariant, QStringList()), Q_ARG(QVariant, "")));
        QTRY_VERIFY(empty->isVisible());
        query->setProperty("text", "проект"); QVERIFY(QMetaObject::invokeMethod(query, "accepted"));
        QTRY_COMPARE(network.replies["messages.search"].size(), 1);
        network.replies["messages.search"][0]->deliver(response(1, 50));
        QTRY_COMPARE(results->property("count").toInt(), 50);
        QCOMPARE(qvariant_cast<QObject *>(results->property("model")), model);
        QVERIFY(!empty->isVisible());
        results->setProperty("currentIndex", 20);
        QVERIFY(QMetaObject::invokeMethod(results, "positionViewAtIndex", Q_ARG(int, 20), Q_ARG(int, 0)));
        results->forceActiveFocus();
        QTRY_VERIFY(results->hasActiveFocus()); QTest::qWait(50);
        auto current = results->property("currentItem").value<QQuickItem *>(); QVERIFY(current);
        QPointer<QQuickItem> retained(current);
        const auto row = current->property("row");
        QCOMPARE((row.metaType() == QMetaType::fromType<QJSValue>() ? row.value<QJSValue>().toVariant() : row).toMap().value("cmid").toString(), "21");
        const auto contentY = results->property("contentY").toReal();
        QSignalSpy resets(model, &QAbstractItemModel::modelReset);
        QVERIFY(QMetaObject::invokeMethod(sheet->findChild<QObject *>("actionSearchMore"), "clicked"));
        QTRY_COMPARE(network.replies["messages.search"].size(), 2);
        QVERIFY(retained); QCOMPARE(results->property("currentItem").value<QQuickItem *>(), retained.data());
        network.replies["messages.search"][1]->deliver(response(51, 100));
        QTRY_COMPARE(results->property("count").toInt(), 100);
        QTest::qWait(50);
        QCOMPARE(resets.size(), 0);
        QVERIFY(retained); QCOMPARE(results->property("currentItem").value<QQuickItem *>(), retained.data());
        QCOMPARE(results->property("currentIndex").toInt(), 20);
        QVERIFY(qAbs(results->property("contentY").toReal() - contentY) <= 1);
        QVERIFY(results->hasActiveFocus());
        auto content = results->property("contentItem").value<QQuickItem *>(); QVERIFY(content); QVERIFY(content->childItems().size() < 35);
        auto date = current->findChild<QQuickItem *>("actionResultDate"); QVERIFY(date); QVERIFY(date->isVisible());
        QVERIFY(date->property("text").toString().contains("2026"));
        QCOMPARE(sheet->findChild<QObject *>("messageResultCount")->property("text").toString(), "Загружено: 100");
        if (qEnvironmentVariableIsSet("VKM_SEARCH_RESULTS_SCREENSHOT")) {
            for (const bool dark : {false, true}) {
                messenger.setDarkTheme(dark); QTest::qWait(50);
                QVERIFY(window->grabWindow().save(qEnvironmentVariable("VKM_SEARCH_RESULTS_SCREENSHOT") + (dark ? "-dark.png" : "-light.png")));
            }
        }
        query->forceActiveFocus(); query->setProperty("text", "пусто"); QVERIFY(QMetaObject::invokeMethod(query, "accepted"));
        QTRY_COMPARE(network.replies["messages.search"].size(), 3);
        network.replies["messages.search"][2]->deliver(R"({"response":{"items":[]}})");
        QTRY_COMPARE(model->rowCount(), 0); QTRY_VERIFY(empty->isVisible());
        QVERIFY(QMetaObject::invokeMethod(sheet, "close")); QTRY_VERIFY(!sheet->property("visible").toBool());
        QCOMPARE(model->rowCount(), 0);
        // Important messages share the model and release it on close as well.
        QVERIFY(QMetaObject::invokeMethod(sheet, "show", Q_ARG(QVariant, "important"), Q_ARG(QVariant, "77"), Q_ARG(QVariant, QStringList()), Q_ARG(QVariant, "")));
        QTRY_COMPARE(network.replies["messages.getImportantMessages"].size(), 1);
        network.replies["messages.getImportantMessages"][0]->deliver(response(1, 2));
        QTRY_COMPARE(results->property("count").toInt(), 2);
        results->setProperty("currentIndex", 1); results->forceActiveFocus();
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_VERIFY(!sheet->property("visible").toBool());
        QCOMPARE(messenger.selectedPeer(), "77");
        QCOMPARE(model->rowCount(), 0);
        QCOMPARE(qmlWarnings.size(), 0);
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

void MessengerTest::senderProfileOpensWithoutLeavingGroupAndWritesToPerson()
    {
        QImage image(100, 100, QImage::Format_ARGB32_Premultiplied); image.fill(QColor("#c95f47"));
        QByteArray png; QBuffer buffer(&png); QVERIFY(buffer.open(QIODevice::WriteOnly)); QVERIFY(image.save(&buffer, "PNG"));
        TokenNetwork network;
        network.responses["portrait-first"] = {png}; network.responses["portrait-second"] = {png};
        network.responses["messages.getDiff"] = {R"({"response":{"server_version":"500","credentials":{"server_lp":"lp.test/im","key":"key","ts":"100"},"conversations_info":[{"conversation":{"peer":{"id":2000000077},"last_conversation_message_id":2,"chat_settings":{"title":"Тестовая беседа"}},"message":[{"conversation_message_id":1,"peer_id":2000000077,"from_id":77,"date":1700000000,"text":"Первый участник"},{"conversation_message_id":2,"peer_id":2000000077,"from_id":88,"date":1700000060,"text":"Второй участник"}]}],"profiles":[{"id":77,"first_name":"Анна","last_name":"Тест","photo_200":"https://assets.test/portrait-first","domain":"anna_test"},{"id":88,"first_name":"Павел","last_name":"Тест","photo_100":"https://assets.test/portrait-second"}]}})"};
        network.responses["messages.getHistoryAttachments"] = {R"({"response":{"items":[]}})", R"({"response":{"items":[]}})"};
        Messenger messenger(nullptr, &network);
        QQmlApplicationEngine engine; engine.rootContext()->setContextProperty("messenger", &messenger);
        engine.load(QUrl::fromLocalFile(QStringLiteral(NATIVE_QML_PATH))); QVERIFY(!engine.rootObjects().isEmpty());
        auto window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        messenger.loginWithToken("test-token"); QTRY_COMPARE(messenger.conversations()->rowCount(), 1);
        messenger.selectConversation("2000000077", "Тестовая беседа"); QTRY_COMPARE(messenger.messages()->rowCount(), 2);
        QCOMPARE(messenger.messages()->get(0).value("senderPeer").toString(), "77");
        QCOMPARE(messenger.messages()->get(0).value("senderAvatar").toString(), "https://assets.test/portrait-first");
        auto composer = window->findChild<QObject *>("composer"); QVERIFY(composer);
        composer->setProperty("text", "Групповой черновик"); QTRY_COMPARE(messenger.draftText(), "Групповой черновик");
        const auto named = [window](const QString &name) {
            QList<QQuickItem *> result;
            const auto visit = [&](auto &&self, QQuickItem *item) -> void { if (item->objectName() == name) result.append(item); for (auto child : item->childItems()) self(self, child); };
            visit(visit, window->contentItem()); return result;
        };
        QTRY_COMPARE(named("messageSenderAvatar").size(), 2);
        QQuickItem *avatar = nullptr;
        for (auto item : named("messageSenderAvatar")) if (item->property("peer").toString() == "77") avatar = item;
        QVERIFY(avatar);
        auto picture = avatar->findChild<QObject *>("avatarImage"); QVERIFY(picture);
        QTRY_COMPARE(picture->property("status").toInt(), 1);
        window->setProperty("flatMessages", false);
        QTRY_VERIFY(avatar->isVisible());
        window->setProperty("flatMessages", true);
        QTest::qWait(100);
        network.responses["users.get"] = {R"({"response":[{"id":77,"first_name":"Анна","last_name":"Тест","photo_200":"https://assets.test/portrait-first","domain":"anna_test","online":1}]})"};
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, avatar->mapToScene(QPointF(avatar->width()/2, avatar->height()/2)).toPoint());
        auto panel = window->findChild<QObject *>("conversationProfile"); QVERIFY(panel);
        QTRY_VERIFY(panel->property("opened").toBool());
        QVERIFY(panel->property("person").toBool()); QVERIFY(!panel->property("chat").toBool());
        QCOMPARE(panel->property("peer").toString(), "77");
        QCOMPARE(messenger.selectedPeer(), "2000000077"); QCOMPARE(messenger.draftText(), "Групповой черновик");
        QTRY_COMPARE(messenger.peerProfile("77").value("online").toInt(), 1);
        if (qEnvironmentVariableIsSet("VKM_PERSON_CARD_SCREENSHOT")) QVERIFY(window->grabWindow().save(qEnvironmentVariable("VKM_PERSON_CARD_SCREENSHOT")));
        QVERIFY(QMetaObject::invokeMethod(panel, "close")); QTRY_VERIFY(!panel->property("visible").toBool());
        QQuickItem *name = nullptr;
        for (auto item : named("messageSenderName")) if (item->property("peer").toString() == "77") name = item;
        QVERIFY(name);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, name->mapToScene(QPointF(8, name->height()/2)).toPoint());
        QTRY_VERIFY(panel->property("opened").toBool());
        auto write = window->findChild<QObject *>("personWriteButton"); QVERIFY(write);
        QVERIFY(QMetaObject::invokeMethod(write, "clicked"));
        QCOMPARE(messenger.selectedPeer(), "77"); QCOMPARE(messenger.title(), "Анна Тест");
        QTRY_VERIFY(!panel->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(window, "selectChat", Q_ARG(QVariant, "2000000077"), Q_ARG(QVariant, "Тестовая беседа")));
        QCOMPARE(messenger.draftText(), "Групповой черновик");
    }


void MessengerTest::missingGroupAvatarsLoadInOneRequest()
{
    TokenNetwork network;
    network.responses["users.get"] = {R"({"response":[{"id":42}]})", R"({"response":[{"id":77,"first_name":"Анна","photo_200":"https://assets.test/first"},{"id":88,"first_name":"Павел","photo_100":"https://assets.test/second"}]})"};
    network.responses["messages.getDiff"] = {R"({"response":{"server_version":"500","credentials":{"server_lp":"lp.test/im","key":"key","ts":"100"},"conversations_info":[{"conversation":{"peer":{"id":2000000077},"last_conversation_message_id":2},"message":[{"conversation_message_id":1,"peer_id":2000000077,"from_id":77,"text":"Первое"},{"conversation_message_id":2,"peer_id":2000000077,"from_id":88,"text":"Второе"}]}],"profiles":[{"id":77,"first_name":"Анна"},{"id":88,"first_name":"Павел"}]}})"};
    Messenger messenger(nullptr, &network);
    messenger.loginWithToken("test-token"); QTRY_COMPARE(messenger.conversations()->rowCount(), 1);
    messenger.selectConversation("2000000077", "Беседа"); QTRY_COMPARE(messenger.messages()->rowCount(), 2);
    QTRY_COMPARE(messenger.messages()->get(0).value("senderAvatar").toString(), "https://assets.test/first");
    QCOMPARE(messenger.messages()->get(1).value("senderAvatar").toString(), "https://assets.test/second");
    QCOMPARE(network.replies["users.get"].size(), 2);
    QList<QUrlQuery> queries;
    for (const auto &request : network.requests) if (request.first.path().endsWith("users.get")) queries.append(QUrlQuery(QString::fromUtf8(request.second)));
    const auto ids = queries.last().queryItemValue("user_ids", QUrl::FullyDecoded).split(',');
    QCOMPARE(QSet<QString>(ids.begin(), ids.end()), QSet<QString>({"77", "88"}));
    messenger.loadPeerProfile("77"); QTest::qWait(50);
    QCOMPARE(network.replies["users.get"].size(), 2);
}

void MessengerTest::scrollingShowsDateThenFadesWithoutChangingLayout()
{
    Messenger messenger; messenger.startDemo();
    QQmlApplicationEngine engine; engine.rootContext()->setContextProperty("messenger", &messenger);
    engine.load(QUrl::fromLocalFile(QStringLiteral(NATIVE_QML_PATH))); QVERIFY(!engine.rootObjects().isEmpty());
    auto window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
    window->requestActivate(); QTRY_VERIFY(window->isActive());
    QVariantList rows;
    const auto day = QDateTime(QDate(QDate::currentDate().year(), 8, 7), QTime(12, 0)).toSecsSinceEpoch();
    for (int i = 0; i < 90; ++i) rows.append(QVariantMap{{"id", QString::number(i + 1)}, {"cmid", QString::number(i + 1)}, {"sender", "Тестовый собеседник"}, {"text", QString("Сообщение %1\nВторая строка сообщения").arg(i + 1)}, {"sentAt", day + (i >= 45 ? 86400 : 0) + i * 60}, {"time", "12:00"}});
    messenger.messages()->replace(TimelinePresentation::decorate(rows));
    auto history = window->findChild<QQuickItem *>("historyList"); QVERIFY(history);
    auto badge = window->findChild<QQuickItem *>("scrollDateBadge"); QVERIFY(badge);
    QTest::qWait(150); QCOMPARE(badge->opacity(), 0.0);
    const auto paneHeight = history->height();
    const auto position = [&](int index) {
        QVERIFY(QMetaObject::invokeMethod(history, "positionViewAtIndex", Q_ARG(int, index), Q_ARG(int, 0)));
        QTest::qWait(60);
    };
    window->setProperty("wasAtEnd", false);
    position(5); QCOMPARE(badge->opacity(), 0.0);
    const auto scroll = [&] {
        const auto start = history->mapToScene(QPointF(history->width() - 26, history->height() * 0.6)).toPoint();
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, start);
        QTest::mouseMove(window, start - QPoint(0, 25), 30);
        QTest::mouseMove(window, start - QPoint(0, 90), 30);
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, start - QPoint(0, 90));
    };
    scroll(); QTRY_VERIFY(badge->opacity() > 0.9);
    QCOMPARE(badge->property("dateText").toString(), "7 августа");
    QTRY_VERIFY(!history->property("moving").toBool());
    QCOMPARE(history->height(), paneHeight);
    position(55); scroll(); QTRY_COMPARE(badge->property("dateText").toString(), QString("8 августа"));
    QTRY_VERIFY(!history->property("moving").toBool());
    QTest::qWait(1200); QVERIFY(badge->opacity() > 0.9);
    if (qEnvironmentVariableIsSet("VKM_SCROLL_DATE_SCREENSHOT")) QVERIFY(window->grabWindow().save(qEnvironmentVariable("VKM_SCROLL_DATE_SCREENSHOT")));
    QTRY_VERIFY(badge->opacity() == 0.0);
    const auto wheelPoint = history->mapToScene(QPointF(history->width() - 26, history->height() / 2)).toPoint();
    QTest::mouseMove(window, wheelPoint);
    const auto beforeWheel = history->property("contentY").toReal();
    QWheelEvent wheel(QPointF(wheelPoint), QPointF(window->mapToGlobal(wheelPoint)), QPoint(), QPoint(0, 120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(window, &wheel);
    // A passive observer must not consume an impulse that native scrolling ignores.
    QVERIFY(!wheel.isAccepted() || history->property("contentY").toReal() != beforeWheel);
    QTRY_VERIFY(badge->opacity() > 0.9);
    messenger.selectConversation("2", "Анна Смирнова");
    QTRY_VERIFY(!badge->isVisible());
}

void MessengerTest::searchAndComposerControlsWorkInBothThemes()
{
    Messenger messenger; messenger.startDemo();
    QQmlApplicationEngine engine; engine.rootContext()->setContextProperty("messenger", &messenger);
    engine.load(QUrl::fromLocalFile(QStringLiteral(NATIVE_QML_PATH))); QVERIFY(!engine.rootObjects().isEmpty());
    auto window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
    window->requestActivate(); QTRY_VERIFY(window->isActive());
    auto search = window->findChild<QQuickItem *>("conversationSearch"); QVERIFY(search);
    auto clear = window->findChild<QQuickItem *>("clearConversationSearch"); QVERIFY(clear);
    auto composer = window->findChild<QQuickItem *>("composer"); QVERIFY(composer);
    auto send = window->findChild<QQuickItem *>("sendMessageButton"); QVERIFY(send);
    auto chats = window->findChild<QQuickItem *>("conversationList"); QVERIFY(chats);
    int count = messenger.messages()->rowCount();
    for (bool dark : {true, false}) {
        messenger.setDarkTheme(dark);
        search->setProperty("text", "Анна"); QTRY_COMPARE(chats->property("count").toInt(), 1);
        QVERIFY(clear->isVisible());
        QVERIFY(QMetaObject::invokeMethod(clear, "clicked"));
        QTRY_COMPARE(chats->property("count").toInt(), 4); QVERIFY(search->hasActiveFocus());
        search->setProperty("text", "Команда"); QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(search->property("text").toString().isEmpty());
        composer->forceActiveFocus(); composer->setProperty("text", "Проверка обновлённого интерфейса");
        QTRY_VERIFY(send->isEnabled());
        QTest::qWait(200);
        if (qEnvironmentVariableIsSet("VKM_POLISH_SCREENSHOT")) QVERIFY(window->grabWindow().save(qEnvironmentVariable("VKM_POLISH_SCREENSHOT") + (dark ? "-dark.png" : "-light.png")));
        QVERIFY(QMetaObject::invokeMethod(send, "clicked"));
        ++count;
        QTRY_COMPARE(messenger.messages()->rowCount(), count);
        QTRY_VERIFY(composer->property("text").toString().isEmpty());
        QVERIFY(!send->isEnabled());
    }
    window->resize(780, 560); QTest::qWait(100);
    QVERIFY(composer->width() >= 200);
}

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    app.setOrganizationName("VKTests");
    app.setApplicationName("VKTests");
    QStandardPaths::setTestModeEnabled(true);
    QQuickStyle::setStyle("Basic");
    qmlRegisterType<AvatarImage>("VK", 1, 0, "AvatarImage");
    qmlRegisterType<VideoFrameOutput>("VK", 1, 0, "VideoFrameOutput");
    qmlRegisterType<LottieSticker>("VK", 1, 0, "LottieSticker");
    MessengerTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "MessengerTest.moc"
