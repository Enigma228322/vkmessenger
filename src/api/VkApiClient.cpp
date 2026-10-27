#include "VkApiClient.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <algorithm>

VkApiClient::VkApiClient(QNetworkAccessManager *network, QObject *parent)
    : QObject(parent), m_network(network ? network : &m_defaultNetwork)
{
    m_pace.setSingleShot(true);
    m_pace.setInterval(350);
    connect(&m_pace, &QTimer::timeout, this, &VkApiClient::pump);
}

void VkApiClient::cancel()
{
    ++m_generation; m_pace.stop(); m_queue.clear(); m_userBurst = 0;
    const auto active = m_active; m_active.clear();
    for (const auto &reply : active) if (reply) { reply->disconnect(this); reply->abort(); reply->deleteLater(); }
}

void VkApiClient::request(const QString &method, Form params, Callback callback)
{
    const bool background = method.startsWith("messages.get") || method.startsWith("messages.search") || method == "messages.setActivity";
    m_queue.append({method, std::move(params), std::move(callback), 0, background ? 1 : 0});
    if (!m_pace.isActive()) pump();
}

void VkApiClient::pump()
{
    if (m_queue.isEmpty()) return;
    // Bound a burst of user actions so background synchronization still runs.
    int index = 0;
    if (m_userBurst < 4) {
        for (int i = 0; i < m_queue.size(); ++i) if (m_queue[i].priority < m_queue[index].priority) index = i;
    } else {
        for (int i = 0; i < m_queue.size(); ++i) if (m_queue[i].priority > m_queue[index].priority) index = i;
    }
    auto request = m_queue.takeAt(index);
    if (request.priority == 0) ++m_userBurst; else m_userBurst = 0;
    m_pace.start();
    perform(std::move(request));
}

void VkApiClient::perform(Request pending)
{
    auto params = pending.params;
    params.append({"access_token", m_token}); params.append(qMakePair(QString("v"), QString("5.199")));
    QNetworkRequest request(QUrl("https://api.vk.ru/method/" + pending.method));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(20000);
    auto reply = m_network->post(request, formBody(params));
    m_active.append(reply);
    const auto generation = m_generation;
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation, pending]() mutable {
        reply->deleteLater(); m_active.removeAll(reply);
        if (generation != m_generation) return;
        ApiResult result;
        result.method = pending.method;
        QJsonParseError parse;
        const auto document = QJsonDocument::fromJson(reply->readAll(), &parse);
        const auto object = document.object();
        const auto error = object.value("error").toObject();
        result.code = error.value("error_code").toInt();
        // VK has returned this error outside the public schema's access codes.
        // Classify its specific message without guessing an undocumented code.
        const auto serverMessage = error.value("error_msg").toString();
        result.profileTypeUnavailable = serverMessage.contains("method is not available for this profile type", Qt::CaseInsensitive)
            || serverMessage.contains("method is unavailable with current profile type", Qt::CaseInsensitive);
        if (result.code == 6 && pending.retries < 2 && (pending.method.startsWith("messages.get") || pending.method.startsWith("messages.search"))) {
            ++pending.retries;
            QTimer::singleShot(1000 * pending.retries, this, [this, pending, generation] {
                if (generation != m_generation) return;
                m_queue.append(pending); if (!m_pace.isActive()) pump();
            });
            return;
        }
        if (!error.isEmpty()) {
            if (result.profileTypeUnavailable) result.error = "VK не разрешает это действие для текущего типа профиля.";
            else if (result.code == 5) result.error = "Сессия истекла. Войдите в аккаунт снова.";
            else if (result.code == 7 || result.code == 15 || result.code == 27) result.error = "У аккаунта нет доступа к этому действию.";
            else if (result.code == 6 || result.code == 9) result.error = "Слишком много действий. Подождите немного и повторите.";
            else if (result.code == 14) result.error = "VK требует дополнительного подтверждения. Завершите проверку в официальном клиенте и повторите действие.";
            else if (result.code == 3) result.error = "Этот метод VK API недоступен.";
            // Arbitrary server text may echo request parameters. Keep the
            // diagnostic limited to the method and numeric error code.
            else result.error = "VK отклонил действие.";
            result.error += " (" + result.diagnostic() + ")";
        } else if (reply->error() != QNetworkReply::NoError) {
            result.error = "Не удалось связаться с VK. Сообщения останутся в очереди.";
            result.networkFailure = true;
        } else if (parse.error != QJsonParseError::NoError || !object.contains("response")) {
            result.error = "Не удалось прочитать ответ VK. Повторите действие.";
        } else result.value = object.value("response");
        pending.callback(result);
    });
}
