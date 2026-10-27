#include "OAuth.h"
#include "Form.h"
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QRandomGenerator>
#include <QTcpSocket>

OAuth::OAuth(QObject *parent) : QObject(parent)
{
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        cancel();
        emit failed(QStringLiteral("Время входа истекло. Попробуйте ещё раз."));
    });
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

void OAuth::cancel()
{
    ++m_generation;
    m_state.clear();
    m_verifier.clear();
    m_timeout.stop();
    m_server.close();
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
    if (!QDesktopServices::openUrl(url)) {
        cancel();
        emit failed(QStringLiteral("Не удалось открыть браузер."));
    }
}

void OAuth::complete(const QString &callback)
{
    const QUrl url(callback.trimmed());
    const QUrlQuery query(url);
    if (m_state.isEmpty() || !sameRedirect(url, QUrl(m_redirect))
        || query.queryItemValue("state") != m_state) {
        emit failed(QStringLiteral("Ссылка не соответствует текущему входу (redirect URI или state)."));
        return;
    }
    if (query.hasQueryItem("error")) {
        cancel();
        emit failed(QStringLiteral("Вход отменён или отклонён VK ID."));
        return;
    }
    const auto code = query.queryItemValue("code", QUrl::FullyDecoded);
    const auto device = query.queryItemValue("device_id", QUrl::FullyDecoded);
    if (code.isEmpty() || device.isEmpty()) {
        emit failed(QStringLiteral("В callback отсутствует code или device_id."));
        return;
    }
    Form parameters{{"grant_type", "authorization_code"}, {"code", code},
        {"code_verifier", m_verifier}, {"device_id", device}};
    const auto state = m_state;
    m_state.clear();
    m_verifier.clear();
    m_timeout.stop();
    m_server.close();
    parameters.append({"state", state});
    exchange(parameters, state);
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
    parameters.append({"client_id", m_clientId});
    parameters.append({"redirect_uri", m_redirect});
    QNetworkRequest request(QUrl("https://id.vk.ru/oauth2/auth"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    request.setTransferTimeout(30000);
    auto reply = m_network.post(request, formBody(parameters));
    const auto generation = m_generation;
    QString device;
    for (const auto &[key, value] : parameters)
        if (key == "device_id") device = value;
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation, device, expectedState] {
        reply->deleteLater();
        if (generation != m_generation) return;
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
