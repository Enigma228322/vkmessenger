#include "MediaCache.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QNetworkReply>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <memory>

MediaCache::MediaCache(const QString &directory, QObject *parent, QNetworkAccessManager *network)
    : QObject(parent), m_root(directory.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/media" : directory), m_network(network ? network : &m_defaultNetwork)
{
    m_limit = qBound(qint64(64 * 1048576), m_settings.value("cache/mediaLimit", m_limit).toLongLong(), qint64(10) * 1073741824);
    scan();
}
MediaCache::~MediaCache() { stop(); }
void MediaCache::stop()
{
    ++m_generation; m_queue.clear(); m_waiting.clear();
    const auto active = m_active; m_active.clear();
    for (const auto &reply : active) if (reply) { reply->disconnect(this); reply->abort(); reply->deleteLater(); }
}
void MediaCache::setAccount(const QString &account)
{
    const auto directory = account.isEmpty() ? QString() : m_root + '/' + QCryptographicHash::hash(account.toUtf8(), QCryptographicHash::Sha256).toHex();
    if (directory == m_accountDirectory) return;
    stop(); m_accountDirectory = directory; m_errors.clear(); m_accessed.clear();
    ++m_revision; emit changed();
}
QString MediaCache::path(const QUrl &remote) const
{
    return m_accountDirectory + '/' + QCryptographicHash::hash(remote.toEncoded(), QCryptographicHash::Sha256).toHex();
}
QUrl MediaCache::cached(const QUrl &remote) const
{
    if (m_accountDirectory.isEmpty() || remote.scheme() != "https") return {};
    const auto file = path(remote);
    return QFileInfo::exists(file) ? QUrl::fromLocalFile(file) : QUrl();
}
QUrl MediaCache::source(const QUrl &remote)
{
    if (m_accountDirectory.isEmpty() || remote.scheme() != "https") return {};
    const auto local = cached(remote);
    if (!local.isEmpty()) {
        const auto file = local.toLocalFile();
        if (!m_accessed.contains(file)) {
            m_accessed.insert(file);
            const auto now = QDateTime::currentMSecsSinceEpoch();
            if (m_entries.contains(file)) m_entries[file].accessed = now;
            QFile f(file); if (f.open(QIODevice::ReadOnly)) f.setFileTime(QDateTime::currentDateTimeUtc(), QFileDevice::FileModificationTime);
        }
        return local;
    }
    if (!m_waiting.contains(remote) && !m_errors.contains(remote)) {
        m_waiting.insert(remote); m_queue.append(remote);
        QTimer::singleShot(0, this, &MediaCache::pump);
    }
    return {};
}
void MediaCache::pump()
{
    while (m_active.size() < 4 && !m_queue.isEmpty()) {
        const auto remote = m_queue.dequeue();
        const auto generation = m_generation;
        const auto filePath = path(remote);
        if (!QDir().mkpath(m_accountDirectory)) { m_waiting.remove(remote); m_errors[remote] = "Не удалось создать медиакэш."; ++m_revision; emit changed(); continue; }
        QFile::setPermissions(m_accountDirectory, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        auto file = std::make_shared<QSaveFile>(filePath);
        if (!file->open(QIODevice::WriteOnly)) { m_waiting.remove(remote); m_errors[remote] = "Не удалось сохранить вложение."; ++m_revision; emit changed(); continue; }
        QNetworkRequest request(remote); request.setTransferTimeout(30000);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        auto reply = m_network->get(request); m_active[remote] = reply;
        auto failed = std::make_shared<bool>(false);
        const auto maximum = qMin(m_limit, qint64(128 * 1048576));
        connect(reply, &QNetworkReply::readyRead, this, [this, generation, file, reply, failed, maximum] {
            if (generation != m_generation) return;
            const auto data = reply->readAll();
            if (file->size() + data.size() > maximum || file->write(data) != data.size()) { *failed = true; reply->abort(); }
        });
        connect(reply, &QNetworkReply::finished, this, [this, generation, remote, filePath, file, reply, failed, maximum] {
            reply->deleteLater();
            if (generation != m_generation) return;
            m_active.remove(remote); m_waiting.remove(remote);
            const auto remaining = reply->readAll();
            const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const bool success = !*failed && reply->error() == QNetworkReply::NoError && status >= 200 && status < 300
                && file->size() + remaining.size() <= maximum && file->write(remaining) == remaining.size() && file->commit();
            if (success) {
                QFile::setPermissions(filePath, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
                const auto size = QFileInfo(filePath).size();
                m_bytes -= m_entries.value(filePath).size;
                m_entries[filePath] = {filePath, size, QDateTime::currentMSecsSinceEpoch()}; m_bytes += size;
                trim();
            } else { file->cancelWriting(); m_errors[remote] = "Не удалось загрузить вложение. Нажмите, чтобы повторить."; }
            ++m_revision; emit changed(); pump();
        });
    }
    emit changed();
}
void MediaCache::retry(const QUrl &remote)
{
    const auto file = cached(remote).toLocalFile();
    if (!file.isEmpty() && QFile::remove(file)) {
        m_bytes -= m_entries.take(file).size; m_accessed.remove(file);
    }
    m_errors.remove(remote); ++m_revision; emit changed(); source(remote);
}
void MediaCache::setLimit(qint64 bytes)
{
    m_limit = qBound(qint64(64 * 1048576), bytes, qint64(10) * 1073741824);
    m_settings.setValue("cache/mediaLimit", m_limit); trim(); ++m_revision; emit changed();
}
void MediaCache::trim()
{
    if (m_bytes <= m_limit) return;
    auto entries = m_entries.values();
    std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) { return a.accessed < b.accessed; });
    for (const auto &entry : entries) {
        if (m_bytes <= m_limit) break;
        if (QFile::remove(entry.path) || !QFileInfo::exists(entry.path)) { m_bytes -= entry.size; m_entries.remove(entry.path); }
    }
}
void MediaCache::scan()
{
    auto watcher = new QFutureWatcher<QList<Entry>>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher] {
        const auto entries = watcher->result(); watcher->deleteLater();
        for (const auto &entry : entries) if (!m_entries.contains(entry.path) && QFileInfo::exists(entry.path)) { m_entries.insert(entry.path, entry); m_bytes += entry.size; }
        trim(); ++m_revision; emit changed();
    });
    watcher->setFuture(QtConcurrent::run([root = m_root] {
        QList<Entry> entries;
        QDirIterator it(root, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) { it.next(); const auto info = it.fileInfo(); if (info.fileName().size() == 64) entries.append({info.absoluteFilePath(), info.size(), info.lastModified().toMSecsSinceEpoch()}); }
        return entries;
    }));
}
void MediaCache::clear()
{
    stop();
    if (!m_accountDirectory.isEmpty()) {
        QDir(m_accountDirectory).removeRecursively();
        for (auto it = m_entries.begin(); it != m_entries.end();) {
            if (it.key().startsWith(m_accountDirectory + '/')) { m_bytes -= it.value().size; it = m_entries.erase(it); }
            else ++it;
        }
    }
    m_errors.clear(); ++m_revision; emit changed();
}
