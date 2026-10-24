#include "Messenger.h"
#include "Keychain.h"
#include "LottieSticker.h"
#include <QGuiApplication>
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

    void manualTokenLoginAndPersistence()
    {
        TokenNetwork network;
        Messenger messenger(nullptr, &network);
        messenger.loginWithToken("  manual+token/%2B== \n");
        QVERIFY(messenger.authorizing());
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
        QVERIFY(QMetaObject::invokeMethod(history, "positionViewAtBeginning"));
        QTRY_VERIFY(button->property("visible").toBool());
        history->setProperty("contentY", 600);
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
        QVERIFY(QMetaObject::invokeMethod(button, "clicked"));
        QTRY_VERIFY(!button->property("visible").toBool());
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
        for (const QSize size : {QSize(1180, 780), QSize(780, 560)}) {
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
        if (qEnvironmentVariableIsSet("VKM_SCREENSHOT")) {
            QVERIFY(window->grabWindow().save(qEnvironmentVariable("VKM_SCREENSHOT")));
        }
    }

    void reactionCatalogAndImagesRenderInMessagesAndPicker()
    {
        const auto webp = QByteArray::fromBase64("UklGRiIAAABXRUJQVlA4IBYAAAAwAQCdASoBAAEADsD+JaQAA3AAAAAA");
#ifdef Q_OS_MACOS
        QVERIFY(QImageReader::supportedImageFormats().contains("webp"));
        const auto decoded = QImage::fromData(webp, "webp"); QVERIFY(!decoded.isNull()); QCOMPARE(decoded.size(), QSize(1, 1));
#endif
        QImage icon(48, 48, QImage::Format_ARGB32_Premultiplied); icon.fill(Qt::transparent);
        QPainter painter(&icon); painter.setPen(Qt::NoPen); painter.setBrush(QColor("#ed657b")); painter.drawEllipse(4, 4, 40, 40); painter.end();
        QByteArray png; QBuffer buffer(&png); QVERIFY(buffer.open(QIODevice::WriteOnly)); QVERIFY(icon.save(&buffer, "PNG"));
        TokenNetwork network;
        const QByteArray catalog = R"({"response":{"assets":[{"reaction_id":1,"links":{"static":"https://assets.test/base.png"}},{"reaction_id":2,"links":{"static":"https://assets.test/second.webp"}},{"reaction_id":3,"links":{"static":"https://assets.test/hidden.png"}}],"override_assets":[{"reaction_id":1,"links":{"static":"https://assets.test/override.png"}}],"reaction_ids":[1,2]}})";
        network.responses["messages.getReactionsAssets"] = {catalog, catalog, catalog};
        network.responses["override.png"] = {png}; network.responses["second.webp"] = {webp};
        network.responses["messages.getFolders"] = {R"({"response":{"items":[]}})"};
        network.responses["messages.getDiff"] = {R"({"response":{"server_version":"500","credentials":{"server_lp":"lp.test/im","key":"key","ts":"100"},"conversations_info":[{"conversation":{"peer":{"id":77},"last_conversation_message_id":1,"out_read_cmid":1},"message":[{"id":900,"conversation_message_id":1,"peer_id":77,"from_id":42,"date":1792830000,"out":1,"text":"adsda","reactions":[{"reaction_id":1,"count":1,"user_ids":[42]},{"reaction_id":2,"count":12,"user_ids":[42]}]}]}]}})"};
        network.responses["messages.getDiffContent"] = {R"({"response":{"items":[{"peer_id":77,"messages":[{"id":900,"conversation_message_id":1,"peer_id":77,"from_id":42,"date":1792830000,"out":1,"text":"adsda","reactions":[{"reaction_id":1,"count":1,"user_ids":[42]},{"reaction_id":2,"count":12,"user_ids":[42]}]}]}]}})"};
        auto diff = QJsonDocument::fromJson(network.responses["messages.getDiff"].first()).object().value("response").toObject();
        auto info = diff.value("conversations_info").toArray().first().toObject();
        auto conversation = info.value("conversation").toObject(); conversation.insert("last_conversation_message_id", 3); conversation.insert("out_read_cmid", 3); info.insert("conversation", conversation);
        auto messages = info.value("message").toArray();
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
        QTRY_COMPARE(messenger.actions()->reactionAssets().size(), 3);
        QCOMPARE(messenger.actions()->reactionImage(1), "https://assets.test/override.png");
        QCOMPARE(messenger.actions()->reactionImage(2), "https://assets.test/second.webp");
        QTRY_COMPARE(visualItems("reactionChip").size(), 2);
        QTRY_VERIFY([&] { const auto images = visualItems("reactionImage"); return images.size() == 2 && std::all_of(images.begin(), images.end(), [](auto image) { return image->property("status").toInt() == 1; }); }());
        QTRY_VERIFY(!messenger.loading());
        QTRY_VERIFY([&] { const auto images = visualItems("attachmentImage"); return images.size() == 1 && images.first()->property("status").toInt() == 1 && images.first()->height() > 100; }());
        auto chips = visualItems("reactionChip");
        QVERIFY(chips[0]->width() < 80); QCOMPARE(chips[0]->height(), 30);
        QVERIFY(chips[1]->x() > chips[0]->x()); QCOMPARE(chips[0]->y(), chips[1]->y());
        if (qEnvironmentVariableIsSet("VKM_REACTION_SCREENSHOT")) QVERIFY(window->grabWindow().save(qEnvironmentVariable("VKM_REACTION_SCREENSHOT")));
        auto picker = window->findChild<QObject *>("reactionPicker"); QVERIFY(picker); QVERIFY(QMetaObject::invokeMethod(picker, "open"));
        QTRY_VERIFY(!messenger.actions()->busy()); QTRY_COMPARE(messenger.actions()->results().size(), 2);
        QTRY_VERIFY([&] { const auto images = visualItems("reactionImage"); return images.size() >= 4 && std::all_of(images.begin(), images.end(), [](auto image) { return image->property("status").toInt() == 1; }); }());
        if (qEnvironmentVariableIsSet("VKM_PICKER_SCREENSHOT")) QVERIFY(window->grabWindow().save(qEnvironmentVariable("VKM_PICKER_SCREENSHOT")));
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
