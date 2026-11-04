#include "OAuth.h"
#include "Form.h"
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDateTime>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QRandomGenerator>
#include <QTcpSocket>
#include <algorithm>
#include <limits>

OAuth::OAuth(QObject *parent, QNetworkAccessManager *network) : QObject(parent), m_network(network ? network : &m_defaultNetwork)
{
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, &OAuth::expire);
    connect(&m_server, &QTcpServer::newConnection, this, [this] {
        while (m_server.hasPendingConnections()) {
            auto socket = m_server.nextPendingConnection();
            auto buffer = std::make_shared<QByteArray>();
            auto handled = std::make_shared<bool>(false);
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            QTimer::singleShot(10000, socket, [socket] { socket->disconnectFromHost(); });
            connect(socket, &QTcpSocket::readyRead, this, [this, socket, buffer, handled] {
                if (*handled) return;
                buffer->append(socket->readAll());
                if (buffer->size() > 16384) { socket->disconnectFromHost(); return; }
                if (!buffer->contains("\r\n\r\n")) return;
                *handled = true;
                const auto parts = buffer->split('\n').first().trimmed().split(' ');
                const auto expected = QUrl(m_redirect);
                const auto target = parts.size() == 3 ? QUrl::fromEncoded(parts.at(1)) : QUrl();
                const bool valid = parts.value(0) == "GET" && target.isRelative()
                    && target.path() == expected.path()
                    && QUrlQuery(target).queryItemValue("state") == m_state && !m_state.isEmpty();
                const QByteArray body = valid ? "Login received. You can return to VK."
                                              : "Invalid callback.";
                socket->write(QByteArray(valid ? "HTTP/1.1 200 OK\r\n" : "HTTP/1.1 400 Bad Request\r\n")
                    + "Content-Type: text/plain; charset=utf-8\r\nConnection: close\r\nContent-Length: "
                    + QByteArray::number(body.size()) + "\r\n\r\n" + body);
                socket->disconnectFromHost();
                if (valid) {
                    auto callback = expected;
                    callback.setQuery(target.query());
                    complete(callback.toString());
                }
            });
        }
    });
}

QString OAuth::randomString()
{
    QByteArray bytes(32, Qt::Uninitialized);
    for (auto &byte : bytes)
        byte = static_cast<char>(QRandomGenerator::system()->generate() & 0xff);
    return QString::fromLatin1(bytes.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

QString OAuth::challenge(const QString &verifier)
{
    return QString::fromLatin1(QCryptographicHash::hash(verifier.toUtf8(), QCryptographicHash::Sha256)
        .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

bool OAuth::sameRedirect(const QUrl &actual, const QUrl &expected)
{
    return actual.isValid() && actual.scheme() == expected.scheme()
        && actual.host() == expected.host() && actual.port() == expected.port()
        && actual.path() == expected.path() && actual.userInfo().isEmpty() && !actual.hasFragment();
}

bool OAuth::isAppCallback(const QUrl &url)
{
    return url.isValid() && url.scheme() == "vkm" && url.authority() == "auth"
        && url.path() == "/callback" && !url.hasFragment();
}

void OAuth::expire()
{
    if (!awaitingCallback()) return;
    cancel();
    emit failed(QStringLiteral("Время входа истекло. Начните вход заново."));
}

void OAuth::cancel()
{
    ++m_generation;
    m_state.clear();
    m_verifier.clear();
    m_consumedState.clear();
    m_exchanging = false;
    m_apiLogin = false;
    m_timeout.stop();
    m_server.close();
    if (m_reply) {
        auto reply = m_reply.data(); m_reply.clear();
        reply->disconnect(this); reply->abort(); reply->deleteLater();
    }
}

void OAuth::start(const QString &clientId, const QString &redirect, const QString &scope)
{
    cancel();
    const QUrl uri(redirect);
    const bool loopback = uri.scheme() == "http" && uri.host() == "127.0.0.1" && uri.port() > 0;
    if (clientId.isEmpty() || !uri.isValid() || uri.host().isEmpty() || uri.hasQuery()
        || uri.hasFragment() || !uri.userInfo().isEmpty() || (!loopback && uri.scheme() != "https")) {
        emit failed(QStringLiteral("Укажите ID приложения и зарегистрированный HTTPS redirect URI (либо http://127.0.0.1:порт/callback)."));
        return;
    }
    m_clientId = clientId;
    m_redirect = redirect;
    if (loopback && !m_server.listen(QHostAddress::LocalHost, uri.port())) {
        emit failed(QStringLiteral("Не удалось открыть локальный порт для входа."));
        return;
    }
    m_state = randomString();
    m_verifier = randomString();
    QUrl url("https://id.vk.ru/authorize");
    QUrlQuery query;
    query.addQueryItem("response_type", "code");
    query.addQueryItem("client_id", clientId);
    // VK ID's authorize page also consumes app_id and frontend compatibility
    // metadata; mirror VKCOM/vkid-web-sdk 2.6.1 utils/url::getVKIDUrl.
    // This is a native implementation, not an embedded JavaScript SDK.
    query.addQueryItem("app_id", clientId);
    query.addQueryItem("sdk_type", "vkid");
    query.addQueryItem("v", "2.6.1");
    query.addQueryItem("redirect_uri", redirect);
    query.addQueryItem("state", m_state);
    query.addQueryItem("code_challenge", challenge(m_verifier));
    query.addQueryItem("code_challenge_method", "s256");
    if (!scope.isEmpty()) query.addQueryItem("scope", scope);
    url.setQuery(query);
    m_timeout.start(300000);
    m_deadline.setRemainingTime(300000);
    if (!QDesktopServices::openUrl(url)) {
        cancel();
        emit failed(QStringLiteral("Не удалось открыть браузер."));
    }
}

void OAuth::startApi(const QString &clientId)
{
    cancel();
    bool valid = false;
    if (clientId.toLongLong(&valid) <= 0 || !valid) {
        emit failed(QStringLiteral("Укажите ID зарегистрированного приложения VK API."));
        return;
    }
    m_apiLogin = true;
    m_clientId = clientId;
    m_redirect = "https://oauth.vk.com/blank.html";
    m_state = randomString();
    QUrl url("https://oauth.vk.com/authorize");
    QUrlQuery query;
    query.addQueryItem("client_id", clientId);
    query.addQueryItem("display", "page");
    query.addQueryItem("redirect_uri", m_redirect);
    query.addQueryItem("scope", "messages");
    query.addQueryItem("response_type", "token");
    query.addQueryItem("state", m_state);
    query.addQueryItem("v", "5.199");
    url.setQuery(query);
    m_timeout.start(300000);
    m_deadline.setRemainingTime(300000);
    if (!QDesktopServices::openUrl(url)) {
        cancel();
        emit failed(QStringLiteral("Не удалось открыть браузер."));
    }
}

bool OAuth::completeApi(const QUrl &url)
{
    // Implicit OAuth returns a token in the fragment of VK's own blank page.
    // Keep this separate from the strict PKCE callback and never relay it via
    // our HTTPS server or custom scheme.
    QUrl address(url); address.setFragment(QString());
    const bool correctAddress = sameRedirect(address, QUrl(m_redirect)) && !url.hasQuery() && url.hasFragment();
    const QUrlQuery fragment(url.fragment(QUrl::FullyEncoded));
    const auto state = fragment.queryItemValue("state", QUrl::FullyDecoded);
    if (correctAddress && !m_consumedState.isEmpty() && state == m_consumedState) return false;
    if (m_state.isEmpty() || !correctAddress || state != m_state) {
        emit callbackRejected(QStringLiteral("Ссылка не соответствует текущему входу VK API. Нужен полный адрес blank.html с текущим state."));
        return false;
    }
    const auto items = fragment.queryItems();
    for (const auto &name : {"state", "access_token", "user_id", "expires_in", "error"}) {
        if (std::count_if(items.begin(), items.end(), [name](const auto &item) { return item.first == name; }) > 1) {
            emit callbackRejected(QStringLiteral("В ссылке повторяются параметры входа."));
            return false;
        }
    }
    if (fragment.hasQueryItem("error")) {
        if (fragment.hasQueryItem("access_token")) {
            emit callbackRejected(QStringLiteral("В ссылке несовместимые параметры входа."));
            return false;
        }
        cancel(); emit failed(QStringLiteral("VK отклонил вход через OAuth VK API."));
        return true;
    }
    const auto token = fragment.queryItemValue("access_token", QUrl::FullyDecoded);
    bool validUser = false, validExpiry = false;
    const auto user = fragment.queryItemValue("user_id").toLongLong(&validUser);
    const auto lifetime = fragment.queryItemValue("expires_in").toLongLong(&validExpiry);
    const auto now = QDateTime::currentSecsSinceEpoch();
    if (token.isEmpty() || std::any_of(token.begin(), token.end(), [](QChar c) { return c.isSpace() || c.category() == QChar::Other_Control; })
        || !validUser || user <= 0 || !validExpiry || lifetime < 0 || lifetime > std::numeric_limits<qint64>::max() - now) {
        emit callbackRejected(QStringLiteral("В ссылке отсутствуют или некорректны access_token, user_id или expires_in."));
        return false;
    }
    m_consumedState = m_state; m_state.clear(); m_timeout.stop();
    QJsonObject session{{"access_token", token}, {"user_id", user}, {"client_id", m_clientId},
        {"auth_type", "oauth_api"}, {"redirect_uri", m_redirect},
        {"expires_at", lifetime == 0 ? qint64(0) : now + lifetime}};
    emit authenticated(session);
    return true;
}

bool OAuth::completeAppCallback(const QUrl &callback)
{
    if (m_apiLogin) {
        emit callbackRejected(QStringLiteral("Для входа VK API вставьте полный адрес страницы blank.html в приложении."));
        return false;
    }
    if (!isAppCallback(callback)) {
        emit callbackRejected(QStringLiteral("Некорректный адрес возврата в приложение."));
        return false;
    }
    if (!awaitingCallback() && !m_exchanging && m_consumedState.isEmpty()) {
        emit callbackRejected(QStringLiteral("Нет ожидающего входа. Начните вход заново в этом приложении."));
        return false;
    }
    QUrl original(m_redirect);
    if (original.scheme() != "https") {
        emit callbackRejected(QStringLiteral("Этот способ возврата предназначен для HTTPS-входа."));
        return false;
    }
    // Preserve the encoded query. The registered HTTPS redirect, not the
    // transport scheme, must still be sent to VK during the token exchange.
    original.setQuery(callback.query(QUrl::FullyEncoded), QUrl::StrictMode);
    return complete(original.toString(QUrl::FullyEncoded));
}

bool OAuth::complete(const QString &callback)
{
    const QUrl url(callback.trimmed(), QUrl::StrictMode);
    const QUrlQuery query(url);
    if (awaitingCallback() && m_deadline.hasExpired()) { expire(); return false; }
    if (m_apiLogin) return completeApi(url);
    // A browser may deliver the same URL more than once. Never turn a duplicate
    // into a terminal login failure while the token request is in flight.
    if (sameRedirect(url, QUrl(m_redirect)) && (m_exchanging || (!m_consumedState.isEmpty()
        && query.queryItemValue("state") == m_consumedState))) return false;
    if (m_state.isEmpty() || !sameRedirect(url, QUrl(m_redirect))
        || query.queryItemValue("state") != m_state) {
        emit callbackRejected(m_state.isEmpty()
            ? QStringLiteral("Нет ожидающего входа. Начните вход заново в этом приложении.")
            : QStringLiteral("Ссылка не соответствует текущему входу (redirect URI или state)."));
        return false;
    }
    const auto items = query.queryItems();
    for (const auto &name : {"state", "code", "device_id", "error"}) {
        if (std::count_if(items.begin(), items.end(), [name](const auto &item) { return item.first == name; }) > 1) {
            emit callbackRejected(QStringLiteral("В ссылке повторяются параметры входа."));
            return false;
        }
    }
    if (query.hasQueryItem("error")) {
        if (query.hasQueryItem("code") || query.hasQueryItem("device_id")) {
            emit callbackRejected(QStringLiteral("В ссылке несовместимые параметры входа."));
            return false;
        }
        cancel();
        emit failed(QStringLiteral("Вход отменён или отклонён VK ID."));
        return true;
    }
    const auto code = query.queryItemValue("code", QUrl::FullyDecoded);
    const auto device = query.queryItemValue("device_id", QUrl::FullyDecoded);
    if (code.isEmpty() || device.isEmpty()) {
        emit callbackRejected(QStringLiteral("В ссылке отсутствует code или device_id."));
        return false;
    }
    Form parameters{{"grant_type", "authorization_code"}, {"code", code},
        {"code_verifier", m_verifier}, {"device_id", device}};
    const auto state = m_state;
    m_consumedState = state;
    m_state.clear();
    m_verifier.clear();
    m_timeout.stop();
    m_server.close();
    parameters.append({"state", state});
    exchange(parameters, state);
    return true;
}

void OAuth::refresh(const QJsonObject &session)
{
    cancel();
    m_clientId = session.value("client_id").toString();
    m_redirect = session.value("redirect_uri").toString();
    Form parameters{{"grant_type", "refresh_token"},
        {"refresh_token", session.value("refresh_token").toString()},
        {"device_id", session.value("device_id").toString()}};
    const auto state = randomString();
    parameters.append({"state", state});
    exchange(parameters, state);
}

void OAuth::exchange(Form parameters, const QString &expectedState)
{
    m_exchanging = true;
    parameters.append({"client_id", m_clientId});
    parameters.append({"redirect_uri", m_redirect});
    QNetworkRequest request(QUrl("https://id.vk.ru/oauth2/auth"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    request.setTransferTimeout(30000);
    auto reply = m_network->post(request, formBody(parameters));
    m_reply = reply;
    const auto generation = m_generation;
    QString device;
    for (const auto &[key, value] : parameters)
        if (key == "device_id") device = value;
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation, device, expectedState] {
        reply->deleteLater();
        if (generation != m_generation) return;
        m_reply.clear(); m_exchanging = false;
        auto result = QJsonDocument::fromJson(reply->readAll()).object();
        if (reply->error() != QNetworkReply::NoError || result.contains("error")
            || result.value("access_token").toString().isEmpty()) {
            emit failed(QStringLiteral("VK ID: не удалось получить токен. Проверьте параметры приложения и повторите вход."));
            return;
        }
        if (result.value("state").toString() != expectedState) {
            emit failed(QStringLiteral("VK ID: неверный state в ответе обмена токена."));
            return;
        }
        result.insert("device_id", device);
        result.insert("client_id", m_clientId);
        result.insert("redirect_uri", m_redirect);
        emit authenticated(result);
    });
}
