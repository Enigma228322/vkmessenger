#include "LongPoll.h"
#include "Form.h"
#include <QJsonDocument>
#include <QNetworkReply>
#include <QRandomGenerator>
#include <QUrlQuery>

LongPoll::LongPoll(QObject *parent, QNetworkAccessManager *network)
    : QObject(parent), m_network(network ? network : &m_defaultNetwork)
{
    m_retry.setSingleShot(true);
}

QString LongPoll::cursor(const QJsonValue &value)
{
    if (value.isString()) return value.toString();
    return value.isDouble() ? QString::number(value.toInteger()) : QString();
}

void LongPoll::stop()
{
    ++m_generation;
    m_running = false;
    m_retry.stop();
    m_retry.disconnect(this);
    if (m_reply) m_reply->abort();
    m_reply.clear();
    m_key.clear(); m_ts.clear(); m_server = QUrl();
}

void LongPoll::start(const QJsonObject &credentials, int version)
{
    stop();
    m_version = version;
    auto address = credentials.value(version == 3 ? "server" : "server_lp").toString();
    if (!address.contains("://")) address.prepend("https://");
    m_server = QUrl(address);
    m_key = credentials.value("key").toString();
    m_ts = cursor(credentials.value("ts"));
    if (m_server.scheme() != "https" || m_server.host().isEmpty() || m_server.hasFragment()
        || !m_server.userInfo().isEmpty() || m_key.isEmpty() || m_ts.isEmpty()) {
        emit fatalError("VK вернул некорректные параметры Long Poll.");
        return;
    }
    m_running = true;
    emit statusChanged(QString("Long Poll v%1 · подключено").arg(m_version));
    poll();
}

void LongPoll::recover(const QString &reason)
{
    m_running = false;
    const auto generation = m_generation;
    const auto delay = qMin(30000, 500 * (1 << qMin(m_failures++, 6)))
        + QRandomGenerator::global()->bounded(200);
    emit statusChanged(reason + " · восстановление соединения…");
    m_retry.disconnect(this);
    connect(&m_retry, &QTimer::timeout, this, [this, generation] {
        if (generation == m_generation) emit resyncRequired();
    });
    m_retry.start(delay);
}

void LongPoll::poll()
{
    if (!m_running || m_reply) return;
    auto url = m_server;
    QUrlQuery query(url);
    query.removeAllQueryItems("version"); query.removeAllQueryItems("mode");
    query.addQueryItem("version", QString::number(m_version)); query.addQueryItem("mode", m_version == 3 ? "170" : "682");
    if (m_version == 3) {
        query.addQueryItem("act", "a_check"); query.addQueryItem("key", m_key);
        query.addQueryItem("ts", m_ts); query.addQueryItem("wait", "25");
    }
    url.setQuery(query);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    request.setTransferTimeout(35000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    auto reply = m_version == 3 ? m_network->get(request)
        : m_network->post(request, formBody({{"act", "a_check"}, {"key", m_key}, {"ts", m_ts}, {"wait", "25"}}));
    m_reply = reply;
    const auto generation = m_generation;
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation] {
        reply->deleteLater();
        if (!m_running || generation != m_generation) return;
        m_reply.clear();
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(reply->readAll(), &error);
        if (reply->error() != QNetworkReply::NoError || error.error != QJsonParseError::NoError
            || reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200 || !document.isObject()) {
            recover("Long Poll: соединение прервано");
            return;
        }
        const auto object = document.object();
        if (object.contains("failed")) {
            if (object.value("failed").toInt() == 4) {
                stop();
                emit fatalError(QString("Сервер не поддерживает Long Poll v%1.").arg(m_version));
            } else recover("Long Poll: сессия требует синхронизации");
            return;
        }
        if (cursor(object.value("ts")).isEmpty() || !object.value("updates").isArray()) {
            recover("Long Poll: некорректный ответ");
            return;
        }
        m_ts = cursor(object.value("ts"));
        m_failures = 0;
        emit batch(object);
        if (!m_running || generation != m_generation) return;
        emit statusChanged(QString("Long Poll v%1 · подключено").arg(m_version));
        QTimer::singleShot(0, this, [this, generation] { if (generation == m_generation) poll(); });
    });
}
