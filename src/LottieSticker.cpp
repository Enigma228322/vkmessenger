#include "LottieSticker.h"
#include <rlottie.h>
#include <QtConcurrent/QtConcurrentRun>
#include <QFutureWatcher>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkDiskCache>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QPainter>
#include <QQuickWindow>
#include <QFile>
#include <zlib.h>
#include <cmath>

namespace {
constexpr qsizetype MaxBytes = 16 * 1024 * 1024;
QNetworkAccessManager *network()
{
    static QPointer<QNetworkAccessManager> manager;
    if (!manager) {
        manager = new QNetworkAccessManager(QCoreApplication::instance());
        auto cache = new QNetworkDiskCache(manager);
        cache->setCacheDirectory(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/stickers");
        cache->setMaximumCacheSize(64 * 1024 * 1024);
        manager->setCache(cache);
    }
    return manager;
}

QByteArray unpack(QByteArray bytes)
{
    if (bytes.size() > MaxBytes) return {};
    if (bytes.size() < 2 || uchar(bytes[0]) != 0x1f || uchar(bytes[1]) != 0x8b) return bytes;
    z_stream stream{};
    stream.next_in = reinterpret_cast<Bytef *>(bytes.data());
    stream.avail_in = uInt(bytes.size());
    if (inflateInit2(&stream, 16 + MAX_WBITS) != Z_OK) return {};
    QByteArray result;
    int status = Z_OK;
    do {
        char block[65536];
        stream.next_out = reinterpret_cast<Bytef *>(block); stream.avail_out = sizeof(block);
        status = inflate(&stream, Z_NO_FLUSH);
        result.append(block, qsizetype(sizeof(block) - stream.avail_out));
        if (result.size() > MaxBytes) { status = Z_DATA_ERROR; break; }
    } while (status == Z_OK);
    inflateEnd(&stream);
    return status == Z_STREAM_END ? result : QByteArray();
}
}

struct LottieSticker::State {
    std::unique_ptr<rlottie::Animation> animation;
    int frames = 0;
    double fps = 0;
};

LottieSticker::LottieSticker(QQuickItem *parent) : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
    setOpaquePainting(false);
    setImplicitWidth(208); setImplicitHeight(208);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, [this] { renderFrame(); });
    connect(this, &QQuickItem::visibleChanged, this, &LottieSticker::updatePlayback);
    connect(this, &QQuickItem::windowChanged, this, [this] { renderFrame(true); });
}

LottieSticker::~LottieSticker()
{
    ++m_generation;
    if (m_reply) m_reply->abort();
    // Background jobs capture shared state and pixel buffers, never this item.
}

int LottieSticker::totalFrames() const { return m_state ? m_state->frames : 0; }

void LottieSticker::setSource(const QUrl &source)
{
    if (m_source == source) return;
    const auto generation = ++m_generation;
    if (m_reply) m_reply->abort(); m_reply.clear();
    m_timer.stop(); m_state.reset(); m_image = {}; m_ready = false;
    m_frame = -1; m_resumeFrame = 0; m_error.clear(); m_source = source;
    update(); emit sourceChanged(); emit stateChanged();
    if (source.isEmpty()) return;
    if (source.isLocalFile() || source.scheme() == "qrc") {
        QFile file(source.isLocalFile() ? source.toLocalFile() : ":" + source.path());
        if (!file.open(QIODevice::ReadOnly)) { fail("Не удалось открыть анимацию."); return; }
        parse(file.read(MaxBytes + 1), generation);
        return;
    }
    if (source.scheme() != "https") { fail("Некорректный адрес анимации."); return; }
    QNetworkRequest request(source);
    request.setTransferTimeout(20000);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::PreferCache);
    auto reply = network()->get(request);
    m_reply = reply;
    connect(reply, &QNetworkReply::readyRead, this, [reply] { if (reply->bytesAvailable() > MaxBytes) reply->abort(); });
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation] {
        reply->deleteLater();
        if (generation != m_generation) return;
        m_reply.clear();
        if (reply->error() != QNetworkReply::NoError) { fail("Не удалось загрузить анимацию."); return; }
        parse(reply->readAll(), generation);
    });
}

void LottieSticker::parse(QByteArray bytes, quint64 generation)
{
    auto watcher = new QFutureWatcher<std::shared_ptr<State>>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, generation] {
        watcher->deleteLater();
        if (generation != m_generation) return;
        m_state = watcher->result();
        if (!m_state) { fail("Формат анимации не поддерживается."); return; }
        emit stateChanged(); renderFrame(true); updatePlayback();
    });
    watcher->setFuture(QtConcurrent::run([bytes = std::move(bytes)]() mutable -> std::shared_ptr<State> {
        bytes = unpack(std::move(bytes));
        if (bytes.isEmpty() || !QJsonDocument::fromJson(bytes).isObject()) return {};
        auto state = std::make_shared<State>();
        const auto key = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex().toStdString();
        state->animation = rlottie::Animation::loadFromData(bytes.toStdString(), key, {}, false);
        if (!state->animation) return {};
        const auto frames = state->animation->totalFrame();
        state->fps = state->animation->frameRate();
        if (frames == 0 || frames > 1000000 || !std::isfinite(state->fps) || state->fps <= 0) return {};
        state->frames = int(frames);
        return state;
    }));
}

void LottieSticker::fail(const QString &error)
{
    m_timer.stop(); m_error = error; m_ready = false; emit stateChanged();
}

void LottieSticker::setPlaying(bool playing)
{
    if (m_playing == playing) return;
    m_playing = playing; updatePlayback(); emit playingChanged();
}

void LottieSticker::updatePlayback()
{
    const bool active = m_state && m_state->frames > 1 && m_playing && isVisible();
    if (active && !m_timer.isActive()) {
        m_resumeFrame = qMax(0, m_frame); m_clock.restart();
        m_timer.start(qBound(16, int(1000 / m_state->fps), 1000));
    } else if (!active) m_timer.stop();
}

void LottieSticker::renderFrame(bool force)
{
    if (!m_state || width() <= 0 || height() <= 0) return;
    if (m_rendering) { m_renderAgain |= force; return; }
    const auto dpr = window() ? window()->effectiveDevicePixelRatio() : 1;
    QSize pixels(qBound(1, qRound(width() * dpr), 768), qBound(1, qRound(height() * dpr), 768));
    const auto frame = m_timer.isActive() ? (m_resumeFrame + int(m_clock.elapsed() * m_state->fps / 1000)) % m_state->frames : qMax(0, m_frame);
    if (!force && frame == m_frame && pixels == m_renderedSize) return;
    m_rendering = true;
    const auto state = m_state;
    const auto generation = m_generation;
    auto watcher = new QFutureWatcher<QImage>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, generation, frame, pixels] {
        watcher->deleteLater(); m_rendering = false;
        if (generation == m_generation) {
            m_image = watcher->result(); m_renderedSize = pixels; m_frame = frame;
            const bool wasReady = m_ready; m_ready = !m_image.isNull();
            update(); emit frameChanged(); if (wasReady != m_ready) emit stateChanged();
        }
        if (m_renderAgain || generation != m_generation) { m_renderAgain = false; renderFrame(true); }
    });
    watcher->setFuture(QtConcurrent::run([state, pixels, frame] {
        QImage image(pixels, QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent);
        rlottie::Surface surface(reinterpret_cast<uint32_t *>(image.bits()), size_t(image.width()), size_t(image.height()), size_t(image.bytesPerLine()));
        state->animation->renderSync(size_t(frame), surface, true);
        return image;
    }));
}

void LottieSticker::geometryChange(const QRectF &now, const QRectF &before)
{
    QQuickPaintedItem::geometryChange(now, before);
    if (now.size() != before.size()) renderFrame(true);
}

void LottieSticker::paint(QPainter *painter)
{
    if (!m_image.isNull()) { painter->setRenderHint(QPainter::SmoothPixmapTransform); painter->drawImage(boundingRect(), m_image); }
}
