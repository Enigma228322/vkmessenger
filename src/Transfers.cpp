#include "Transfers.h"
#include <QFile>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QJsonDocument>
#include <QJsonArray>
#include <QNetworkReply>
#include <QTimer>
#include <QDir>
#include <QUuid>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>
#include <QMimeDatabase>

namespace {
bool transientUploadFailure(QNetworkReply *reply)
{
    const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status >= 500 && status <= 504) return true;
    switch (reply->error()) {
    case QNetworkReply::HostNotFoundError:
    case QNetworkReply::RemoteHostClosedError:
    case QNetworkReply::TimeoutError:
    case QNetworkReply::TemporaryNetworkFailureError:
    case QNetworkReply::NetworkSessionFailedError:
    case QNetworkReply::ProtocolFailure: return true;
    default: return false;
    }
}
QString transferError(QNetworkReply *reply)
{
    // Upload URLs contain signatures. Show the host and numeric status only.
    const auto host = reply->url().host();
    const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const auto location = host.isEmpty() ? QString() : " (" + host + ")";
    if (reply->error() == QNetworkReply::HostNotFoundError) return "Сервер загрузки" + location + " не найден. Проверьте соединение или DNS и повторите.";
    if (reply->error() == QNetworkReply::SslHandshakeFailedError) return "Не удалось установить защищённое соединение с сервером" + location + ". Повторите позже.";
    if (status >= 400) return "Сервер передачи" + location + " вернул HTTP " + QString::number(status) + ". Повторите загрузку.";
    return "Соединение с сервером" + location + " прервано (код " + QString::number(reply->error()) + "). Файл можно загрузить повторно.";
}
}

Transfers::Transfers(Api api, QObject *parent, QNetworkAccessManager *network)
    : QObject(parent), m_api(std::move(api)), m_network(network ? network : &m_defaultNetwork) {}
Transfers::~Transfers() { cancel(); cancelDownload(); }

void Transfers::upload(const QUrl &file, const QString &peer, const QString &kind)
{
    if (!file.isLocalFile() || peer.isEmpty() || !QFileInfo(file.toLocalFile()).isFile()) {
        m_error = "Выберите файл и диалог для загрузки."; emit changed(); return;
    }
    if (!QStringList{"photo", "doc", "video", "audio_message", "chat_photo"}.contains(kind)) return;
    if (QFileInfo(file.toLocalFile()).size() == 0) { m_error = "Выбранный файл пуст. Выберите другой файл."; emit changed(); return; }
    if (kind == "chat_photo" && peer.toLongLong() <= 2000000000) {
        m_error = "Фотографию можно установить только для группового чата."; emit changed(); return;
    }
    m_queue.enqueue({file, peer, kind, QFileInfo(file.toLocalFile()).fileName()});
    emit changed();
    if (!m_busy) next();
}

bool Transfers::adoptRecording(const QUrl &file, const QString &peer)
{
    if (!file.isLocalFile() || peer.isEmpty() || m_workspace.isEmpty()) return false;
    if (!QDir().mkpath(m_workspace)) { m_error = "Не удалось сохранить голосовое сообщение."; emit changed(); return false; }
    QFile::setPermissions(m_workspace, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    const auto output = m_workspace + '/' + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".ogg";
    // Recorder and uploads share AppLocalDataLocation: rename transfers ownership
    // without a second untracked recording or a GUI-thread copy of its contents.
    if (!QFile::rename(file.toLocalFile(), output)) { m_error = "Не удалось перенести запись в черновик. Попробуйте ещё раз."; emit changed(); return false; }
    m_queue.enqueue({QUrl::fromLocalFile(output), peer, "audio_message", "Голосовое сообщение.ogg", true});
    emit changed(); if (!m_busy) next(); return true;
}

void Transfers::next()
{
    if (m_paused || m_queue.isEmpty()) { m_busy = false; emit changed(); return; }
    m_job = m_queue.dequeue(); m_busy = true; m_error.clear(); m_progress = 0; m_postRetries = 0;
    m_status = "Подготовка файла…";
    m_filename = m_job.name.isEmpty() ? QFileInfo(m_job.file.toLocalFile()).fileName() : m_job.name;
    emit changed();
    if (!m_workspace.isEmpty() && !m_job.managed) { stage(); return; }
    requestServer();
}

void Transfers::stage()
{
    const auto generation = ++m_generation;
    const auto input = m_job.file.toLocalFile();
    const auto output = m_workspace + '/' + QUuid::createUuid().toString(QUuid::WithoutBraces) + '.' + QFileInfo(input).suffix();
    const auto cancelled = m_stageCancelled = std::make_shared<std::atomic_bool>(false);
    auto watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, generation, output] {
        const auto error = watcher->result(); watcher->deleteLater();
        if (generation != m_generation) { QFile::remove(output); return; }
        if (!error.isEmpty()) { fail(error); return; }
        m_job.file = QUrl::fromLocalFile(output); m_job.managed = true;
        emit changed();
        requestServer();
    });
    watcher->setFuture(QtConcurrent::run([input, output, cancelled] {
        if (!QDir().mkpath(QFileInfo(output).absolutePath())) return QString("Не удалось подготовить файл.");
        QFile::setPermissions(QFileInfo(output).absolutePath(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        QFile source(input); QSaveFile destination(output);
        if (!source.open(QIODevice::ReadOnly) || !destination.open(QIODevice::WriteOnly)) return QString("Не удалось сохранить копию вложения.");
        while (!source.atEnd()) {
            if (cancelled->load()) return QString("Подготовка файла отменена.");
            const auto block = source.read(1048576);
            if (block.isEmpty() && source.error() != QFileDevice::NoError) return QString("Не удалось прочитать вложение.");
            if (destination.write(block) != block.size()) return QString("Недостаточно места для вложения.");
        }
        if (cancelled->load() || !destination.commit()) return QString("Не удалось сохранить вложение.");
        QFile::setPermissions(output, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        return QString();
    }));
}

void Transfers::requestServer()
{
    m_status = "Получаем адрес загрузки…"; emit changed();
    const auto generation = ++m_generation;
    QString method = "docs.getMessagesUploadServer";
    Form params{{"peer_id", m_job.peer}, {"type", m_job.kind == "audio_message" ? "audio_message" : "doc"}};
    if (m_job.kind == "photo") { method = "photos.getMessagesUploadServer"; params = {{"peer_id", m_job.peer}}; }
    if (m_job.kind == "chat_photo") {
        method = "photos.getChatUploadServer";
        params = {{"chat_id", QString::number(m_job.peer.toLongLong() - 2000000000)}};
    }
    if (m_job.kind == "video") { method = "video.save"; params = {{"name", m_filename}, {"is_private", "1"}}; }
    m_api(method, params, [this, guard = QPointer<Transfers>(this), generation](const QJsonValue &value, const QString &error) {
        if (!guard || generation != m_generation) return;
        if (!error.isEmpty()) { fail(error); return; }
        post(value.toObject(), generation);
    });
}

void Transfers::post(const QJsonObject &server, quint64 generation)
{
    const QUrl url(server.value("upload_url").toString());
    if (url.scheme() != "https" || url.host().isEmpty()) { fail("VK не вернул HTTPS адрес загрузки."); return; }
    m_server = server;
    auto multipart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    auto file = new QFile(m_job.file.toLocalFile(), multipart);
    if (!file->open(QIODevice::ReadOnly)) { delete multipart; fail("Не удалось открыть файл."); return; }
    QHttpPart part;
    QString field = m_job.kind == "photo" || m_job.kind == "chat_photo" ? "photo" : m_job.kind == "video" ? "video_file" : "file";
    auto name = m_filename; name.remove('"'); name.remove('\r'); name.remove('\n');
    name.replace('\\', '_');
    part.setRawHeader("Content-Disposition", "form-data; name=\"" + field.toLatin1() + "\"; filename=\"" + name.toUtf8() + "\"");
    part.setHeader(QNetworkRequest::ContentTypeHeader, QMimeDatabase().mimeTypeForFile(m_filename, QMimeDatabase::MatchExtension).name());
    part.setBodyDevice(file);
    multipart->append(part);
    QNetworkRequest request(url);
    request.setTransferTimeout(120000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    // A single compatibility retry also avoids problematic HTTP/2 upload paths.
    if (m_postRetries > 0) request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    m_status = "Загружаем файл…";
    auto reply = m_network->post(request, multipart);
    multipart->setParent(reply);
    m_reply = reply;
    emit changed();
    connect(reply, &QNetworkReply::uploadProgress, this, [this, generation](qint64 sent, qint64 total) {
        if (generation != m_generation) return;
        m_progress = total > 0 ? double(sent) / total : 0; emit changed();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation] {
        reply->deleteLater();
        if (generation != m_generation) return;
        m_reply.clear();
        const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError || status >= 400) {
            // No message has been sent yet. Refresh an expiring upload URL and
            // replay the staged file once; video.save is not repeated implicitly.
            if (m_job.kind != "video" && m_postRetries++ == 0 && transientUploadFailure(reply)) {
                m_status = "Повторяем соединение…"; m_progress = 0; emit changed();
                QTimer::singleShot(350, this, [this, generation] { if (generation == m_generation && m_busy) requestServer(); });
            } else fail(transferError(reply));
            return;
        }
        QJsonParseError parse;
        const auto document = QJsonDocument::fromJson(reply->readAll(), &parse);
        const auto object = document.object();
        if (parse.error != QJsonParseError::NoError || !document.isObject() || object.contains("error") || object.value("error_code").toInt() != 0) { fail("Сервер отклонил загруженный файл. Повторите загрузку; фотографию можно отправить как документ."); return; }
        save(object, generation);
    });
}

void Transfers::save(const QJsonObject &object, quint64 generation)
{
    if (m_job.kind == "video") {
        if (m_server.value("video_id").toInteger() <= 0 || m_server.value("owner_id").toInteger() == 0 || object.value("error_code").toInt() != 0) { fail("VK не подтвердил загрузку видео."); return; }
        QString attachment = "video" + QString::number(m_server.value("owner_id").toInteger()) + "_" + QString::number(m_server.value("video_id").toInteger());
        if (!m_server.value("access_key").toString().isEmpty()) attachment += "_" + m_server.value("access_key").toString();
        finish(attachment); return;
    }
    QString method;
    Form params;
    if (m_job.kind == "photo") {
        if (object.value("server").toInteger() <= 0 || object.value("photo").toString().isEmpty() || object.value("photo").toString() == "[]" || object.value("hash").toString().isEmpty()) {
            fail("VK не принял фотографию. Используйте JPEG или PNG либо отправьте её как документ."); return;
        }
        method = "photos.saveMessagesPhoto";
        params = {{"server", QString::number(object.value("server").toInteger())},
            {"photo", object.value("photo").toString()}, {"hash", object.value("hash").toString()}};
    } else if (m_job.kind == "chat_photo") {
        method = "messages.setChatPhoto";
        const auto file = object.value("response").toString(object.value("file").toString());
        if (file.isEmpty()) { fail("Сервер не вернул данные фотографии чата."); return; }
        params = {{"file", file}};
    } else {
        method = "docs.save";
        if (object.value("file").toString().isEmpty()) { fail("Сервер не вернул загруженный документ."); return; }
        params = {{"file", object.value("file").toString()}, {"title", m_filename}};
    }
    m_status = "Сохраняем вложение…"; emit changed();
    m_api(method, params, [this, guard = QPointer<Transfers>(this), generation](const QJsonValue &value, const QString &error) {
        if (!guard || generation != m_generation) return;
        if (!error.isEmpty()) { fail(error); return; }
        if (m_job.kind == "chat_photo") { emit chatPhotoChanged(); finish({}); return; }
        QString type = m_job.kind == "photo" ? "photo" : value.toObject().value("type").toString("doc");
        const auto photos = value.toArray();
        const auto object = m_job.kind == "photo" ? (photos.isEmpty() ? QJsonObject() : photos.first().toObject()) : value.toObject().value(type).toObject();
        if (object.value("id").toInteger() <= 0 || object.value("owner_id").toInteger() == 0) { fail("VK не вернул ID сохранённого вложения."); return; }
        QString attachment = (m_job.kind == "audio_message" ? QString("doc") : type)
            + QString::number(object.value("owner_id").toInteger()) + "_" + QString::number(object.value("id").toInteger());
        if (!object.value("access_key").toString().isEmpty()) attachment += "_" + object.value("access_key").toString();
        finish(attachment);
    });
}

void Transfers::finish(const QString &attachment)
{
    if (!attachment.isEmpty()) {
        m_pending.append(QVariantMap{{"peer", m_job.peer}, {"attachment", attachment}, {"name", m_filename}, {"file", m_job.managed ? m_job.file.toString() : QString()}});
        emit pendingChanged();
    }
    m_busy = false; m_progress = 1; m_status.clear(); emit changed();
    QTimer::singleShot(0, this, [this] { if (!m_busy) next(); });
}
void Transfers::fail(const QString &error) {
    m_error = error; m_status.clear(); m_busy = false; m_paused = true;
    m_queue.prepend(m_job);
    emit changed();
}
void Transfers::cancel() {
    ++m_generation; m_queue.clear(); m_busy = false; m_paused = false; m_error.clear(); m_status.clear();
    if (m_stageCancelled) m_stageCancelled->store(true);
    if (m_reply) { m_reply->disconnect(this); m_reply->abort(); m_reply->deleteLater(); } m_reply.clear(); emit changed();
}
void Transfers::reset() {
    cancel(); cancelDownload();
    if (!m_pending.isEmpty()) { m_pending.clear(); emit pendingChanged(); }
    m_error.clear(); emit changed();
}
void Transfers::remove(int index) {
    if (index < 0 || index >= m_pending.size()) return;
    const auto file = QUrl(m_pending[index].toMap().value("file").toString()).toLocalFile();
    if (!m_workspace.isEmpty() && file.startsWith(m_workspace + '/')) QFile::remove(file);
    m_pending.removeAt(index); emit pendingChanged(); emit changed();
}
QStringList Transfers::attachments(const QString &peer) const {
    QStringList result;
    for (const auto &value : m_pending) if (value.toMap().value("peer").toString() == peer) result << value.toMap().value("attachment").toString();
    return result;
}
void Transfers::acknowledge(const QString &peer, const QStringList &attachments) {
    for (int i = m_pending.size() - 1; i >= 0; --i) {
        auto value = m_pending[i].toMap();
        if (value.value("peer").toString() == peer && attachments.contains(value.value("attachment").toString())) remove(i);
    }
    emit changed();
}

void Transfers::download(const QUrl &remote, const QUrl &destination)
{
    cancelDownload();
    if (remote.scheme() != "https" || !destination.isLocalFile()) { m_error = "Некорректный адрес скачивания."; emit changed(); return; }
    m_downloadRemote = remote; m_downloadDestination = destination;
    m_downloadFile = std::make_unique<QSaveFile>(destination.toLocalFile());
    if (!m_downloadFile->open(QIODevice::WriteOnly)) { m_downloadFile.reset(); m_downloadFailed = true; m_error = "Не удалось создать файл."; emit changed(); return; }
    QNetworkRequest request(remote); request.setTransferTimeout(120000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    auto reply = m_network->get(request); m_downloadReply = reply;
    const auto generation = ++m_downloadGeneration;
    m_error.clear(); emit changed();
    connect(reply, &QNetworkReply::readyRead, this, [this, reply, generation] {
        if (generation != m_downloadGeneration || !m_downloadFile) return;
        const auto data = reply->readAll();
        if (m_downloadFile->write(data) != data.size()) {
            m_error = "Ошибка записи файла."; cancelDownload(); m_downloadFailed = true; emit changed();
        }
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation, destination] {
        reply->deleteLater();
        if (generation != m_downloadGeneration) return;
        m_downloadReply.clear();
        const auto remaining = reply->readAll();
        const bool ok = reply->error() == QNetworkReply::NoError
            && m_downloadFile && m_downloadFile->write(remaining) == remaining.size() && m_downloadFile->commit();
        m_downloadFailed = !ok;
        if (!ok) m_error = reply->error() == QNetworkReply::NoError ? "Не удалось сохранить файл. Проверьте свободное место." : transferError(reply);
        m_downloadFile.reset(); emit changed();
        if (ok) emit downloadFinished(destination);
    });
}
void Transfers::cancelDownload() {
    ++m_downloadGeneration;
    m_downloadFailed = false;
    if (m_downloadReply) { m_downloadReply->disconnect(this); m_downloadReply->abort(); m_downloadReply->deleteLater(); }
    m_downloadReply.clear();
    if (m_downloadFile) m_downloadFile->cancelWriting();
    m_downloadFile.reset(); emit changed();
}


void Transfers::pause()
{
    const auto active = m_busy;
    if (active) m_queue.prepend(m_job);
    ++m_generation; m_busy = false; m_paused = true;
    if (m_stageCancelled) m_stageCancelled->store(true);
    if (m_reply) { m_reply->disconnect(this); m_reply->abort(); m_reply->deleteLater(); } m_reply.clear(); emit changed();
}
void Transfers::resume() { m_paused = false; m_error.clear(); if (!m_busy) next(); }
void Transfers::retry() {
    if (m_downloadFailed) download(m_downloadRemote, m_downloadDestination);
    else resume();
}
QJsonObject Transfers::savedState() const
{
    QJsonArray jobs;
    const auto encode = [](const Job &job) { return QJsonObject{{"file", job.file.toString()}, {"peer", job.peer}, {"kind", job.kind}, {"name", job.name}, {"managed", job.managed}}; };
    if (m_busy) jobs.append(encode(m_job));
    for (const auto &job : m_queue) jobs.append(encode(job));
    return {{"pending", QJsonArray::fromVariantList(m_pending)}, {"jobs", jobs}};
}
void Transfers::restoreState(const QJsonObject &state)
{
    reset(); m_paused = true;
    m_pending = state.value("pending").toArray().toVariantList();
    if (!m_pending.isEmpty()) emit pendingChanged();
    for (const auto &item : state.value("jobs").toArray()) {
        const auto job = item.toObject();
        const QUrl file(job.value("file").toString());
        if (file.isLocalFile()) m_queue.enqueue({file, job.value("peer").toString(), job.value("kind").toString(), job.value("name").toString(), job.value("managed").toBool()});
    }
    emit changed();
}
