#include "AvatarImage.h"
#include <QFutureWatcher>
#include <QImageReader>
#include <QPainter>
#include <QPainterPath>
#include <QtConcurrent/QtConcurrentRun>
#include <QCache>
#include <QCoreApplication>
#include <QFileInfo>
#include <QDateTime>

namespace {
struct DecodeJob { quint64 id; QFuture<QImage> future; };
struct AvatarCache {
    QCache<QString, QImage> images{8 * 1024}; // KiB of decoded pixels, shared implicitly by items.
    QHash<QString, DecodeJob> pending;
    quint64 nextId = 0;
};
// All cache access and future completion handlers run on the GUI thread.
AvatarCache &avatarCache() { static AvatarCache cache; return cache; }
QString imageKey(const QUrl &source)
{
    if (!source.isLocalFile()) return {};
    const QFileInfo info(source.toLocalFile());
    if (!info.isFile()) return {};
    return info.absoluteFilePath() + '\n' + QString::number(info.size()) + '\n' + QString::number(info.lastModified().toMSecsSinceEpoch());
}
QFuture<QImage> decodedImage(const QUrl &source, const QString &key)
{
    auto &cache = avatarCache();
    if (!key.isEmpty() && cache.pending.contains(key)) return cache.pending.value(key).future;
    auto future = QtConcurrent::run([source] {
        if (!source.isLocalFile()) return QImage();
        QImageReader reader(source.toLocalFile()); reader.setAutoTransform(true);
        const auto size = reader.size();
        if (size.width() > 256 || size.height() > 256) reader.setScaledSize(size.scaled(256, 256, Qt::KeepAspectRatio));
        auto image = reader.read();
        // Some image plugins do not implement scaled decoding.
        if (image.width() > 256 || image.height() > 256) image = image.scaled(256, 256, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        return image;
    });
    if (key.isEmpty()) return future;
    const auto id = ++cache.nextId;
    cache.pending.insert(key, {id, future});
    // A disappearing delegate must not cancel work shared by other avatars.
    auto watcher = new QFutureWatcher<QImage>(QCoreApplication::instance());
    QObject::connect(watcher, &QFutureWatcherBase::finished, watcher, [watcher, key, id] {
        const auto image = watcher->result(); watcher->deleteLater();
        auto &cache = avatarCache();
        const auto current = cache.pending.constFind(key);
        if (current == cache.pending.cend() || current->id != id) return;
        cache.pending.remove(key);
        if (!image.isNull()) cache.images.insert(key, new QImage(image), int((image.sizeInBytes() + 1023) / 1024));
    });
    watcher->setFuture(future);
    return future;
}
}

AvatarImage::AvatarImage(QQuickItem *parent) : QQuickPaintedItem(parent) { setAntialiasing(true); }
void AvatarImage::setSource(const QUrl &source)
{
    if (source == m_source) return;
    if (source.isEmpty() && !m_cacheKey.isEmpty()) {
        avatarCache().images.remove(m_cacheKey);
        avatarCache().pending.remove(m_cacheKey);
    }
    m_source = source; m_image = {};
    const auto generation = ++m_generation;
    m_cacheKey = imageKey(source);
    m_status = source.isEmpty() ? 0 : 2;
    if (!m_cacheKey.isEmpty()) if (const auto cached = avatarCache().images.object(m_cacheKey)) { m_image = *cached; m_status = 1; }
    emit sourceChanged(); emit statusChanged(); update();
    if (generation != m_generation || source.isEmpty() || m_status == 1) return;
    auto watcher = new QFutureWatcher<QImage>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, generation] {
        const auto image = watcher->result(); watcher->deleteLater();
        if (generation != m_generation) return;
        m_image = image; m_status = image.isNull() ? 3 : 1;
        emit statusChanged(); update();
    });
    watcher->setFuture(decodedImage(source, m_cacheKey));
}
void AvatarImage::paint(QPainter *painter)
{
    if (m_image.isNull() || width() <= 0 || height() <= 0) return;
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setRenderHint(QPainter::SmoothPixmapTransform);
    QPainterPath mask; mask.addEllipse(boundingRect()); painter->setClipPath(mask);
    const qreal side = qMin(m_image.width(), m_image.height());
    const QRectF crop((m_image.width() - side) / 2, (m_image.height() - side) / 2, side, side);
    painter->drawImage(boundingRect(), m_image, crop);
}
