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

Transfers::Transfers(Api api, QObject *parent, QNetworkAccessManager *network)
    : QObject(parent), m_api(std::move(api)), m_network(network ? network : &m_defaultNetwork) {}
Transfers::~Transfers() { cancel(); cancelDownload(); }

void Transfers::upload(const QUrl &file, const QString &peer, const QString &kind)
{
    if (!file.isLocalFile() || peer.isEmpty() || !QFileInfo(file.toLocalFile()).isFile()) {
        m_error = "Выберите файл и диалог для загрузки."; emit changed(); return;
    }
    if (!QStringList{"photo", "doc", "video", "audio_message", "chat_photo"}.contains(kind)) return;
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
    m_job = m_queue.dequeue(); m_busy = true; m_error.clear(); m_progress = 0;
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
    part.setHeader(QNetworkRequest::ContentDispositionHeader, QString("form-data; name=\"%1\"; filename=\"%2\"").arg(field, name));
    part.setBodyDevice(file);
    multipart->append(part);
    QNetworkRequest request(url);
    request.setTransferTimeout(120000);
    auto reply = m_network->post(request, multipart);
    multipart->setParent(reply);
    m_reply = reply;
    connect(reply, &QNetworkReply::uploadProgress, this, [this, generation](qint64 sent, qint64 total) {
        if (generation != m_generation) return;
        m_progress = total > 0 ? double(sent) / total : 0; emit changed();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation] {
        reply->deleteLater();
        if (generation != m_generation) return;
        m_reply.clear();
        if (reply->error() != QNetworkReply::NoError) { fail("Не удалось загрузить файл: " + reply->errorString()); return; }
        QJsonParseError parse;
        const auto object = QJsonDocument::fromJson(reply->readAll(), &parse).object();
        if (parse.error != QJsonParseError::NoError || object.contains("error")) { fail("Сервер отклонил загруженный файл."); return; }
        save(object, generation);
    });
}

void Transfers::save(const QJsonObject &object, quint64 generation)
{
    if (m_job.kind == "video") {
        if (m_server.value("video_id").toInteger() <= 0 || object.contains("error_code")) { fail("VK не подтвердил загрузку видео."); return; }
        QString attachment = "video" + QString::number(m_server.value("owner_id").toInteger()) + "_" + QString::number(m_server.value("video_id").toInteger());
        if (!m_server.value("access_key").toString().isEmpty()) attachment += "_" + m_server.value("access_key").toString();
        finish(attachment); return;
    }
    QString method;
    Form params;
    if (m_job.kind == "photo") {
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
    if (!attachment.isEmpty()) m_pending.append(QVariantMap{{"peer", m_job.peer}, {"attachment", attachment}, {"name", m_filename}, {"file", m_job.managed ? m_job.file.toString() : QString()}});
    m_busy = false; m_progress = 1; emit changed();
    QTimer::singleShot(0, this, [this] { if (!m_busy) next(); });
}
void Transfers::fail(const QString &error) {
    m_error = error; m_busy = false; m_paused = true;
    m_queue.prepend(m_job);
    emit changed();
}
void Transfers::cancel() {
    ++m_generation; m_queue.clear(); m_busy = false; m_paused = false;
    if (m_stageCancelled) m_stageCancelled->store(true);
    if (m_reply) { m_reply->disconnect(this); m_reply->abort(); m_reply->deleteLater(); } m_reply.clear(); emit changed();
}
void Transfers::reset() { cancel(); cancelDownload(); m_pending.clear(); m_error.clear(); emit changed(); }
void Transfers::remove(int index) {
    if (index < 0 || index >= m_pending.size()) return;
    const auto file = QUrl(m_pending[index].toMap().value("file").toString()).toLocalFile();
    if (!m_workspace.isEmpty() && file.startsWith(m_workspace + '/')) QFile::remove(file);
    m_pending.removeAt(index); emit changed();
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
    m_downloadFile = std::make_unique<QSaveFile>(destination.toLocalFile());
    if (!m_downloadFile->open(QIODevice::WriteOnly)) { m_downloadFile.reset(); m_error = "Не удалось создать файл."; emit changed(); return; }
    QNetworkRequest request(remote); request.setTransferTimeout(120000);
    auto reply = m_network->get(request); m_downloadReply = reply;
    const auto generation = ++m_downloadGeneration;
    m_error.clear(); emit changed();
    connect(reply, &QNetworkReply::readyRead, this, [this, reply, generation] {
        if (generation != m_downloadGeneration || !m_downloadFile) return;
        const auto data = reply->readAll();
        if (m_downloadFile->write(data) != data.size()) {
            m_error = "Ошибка записи файла."; cancelDownload(); emit changed();
        }
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation, destination] {
        reply->deleteLater();
        if (generation != m_downloadGeneration) return;
        m_downloadReply.clear();
        const auto remaining = reply->readAll();
        const bool ok = reply->error() == QNetworkReply::NoError
            && m_downloadFile && m_downloadFile->write(remaining) == remaining.size() && m_downloadFile->commit();
        if (!ok) m_error = "Не удалось скачать файл.";
        m_downloadFile.reset(); emit changed();
        if (ok) emit downloadFinished(destination);
    });
}
void Transfers::cancelDownload() {
    ++m_downloadGeneration;
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
    for (const auto &item : state.value("jobs").toArray()) {
        const auto job = item.toObject();
        const QUrl file(job.value("file").toString());
        if (file.isLocalFile()) m_queue.enqueue({file, job.value("peer").toString(), job.value("kind").toString(), job.value("name").toString(), job.value("managed").toBool()});
    }
    emit changed();
}
