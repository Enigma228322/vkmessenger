#include "VideoFrameOutput.h"
#include <QPainter>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QVideoFrame>

VideoFrameOutput::VideoFrameOutput(QQuickItem *parent) : QQuickPaintedItem(parent)
{
    setOpaquePainting(true);
    connect(this, &QQuickItem::windowChanged, this, [this](QQuickWindow *window) {
        disconnect(m_backendConnection);
        if (window) m_backendConnection = connect(window, &QQuickWindow::sceneGraphInitialized, this, &VideoFrameOutput::refreshBackend, Qt::QueuedConnection);
        refreshBackend();
    });
    connect(this, &QQuickItem::visibleChanged, this, &VideoFrameOutput::refreshFrame);
}

void VideoFrameOutput::setVideoSink(QVideoSink *sink)
{
    if (m_sink == sink) return;
    disconnect(m_frameConnection); m_sink = sink;
    if (sink) m_frameConnection = connect(sink, &QVideoSink::videoFrameChanged, this, &VideoFrameOutput::refreshFrame);
    emit videoSinkChanged(); refreshFrame();
}

void VideoFrameOutput::refreshBackend()
{
    const bool active = window() && window()->rendererInterface()->graphicsApi() == QSGRendererInterface::Software;
    if (active == m_active) return;
    m_active = active;
    if (!active) m_frame = {};
    emit activeChanged(); refreshFrame(); update();
}

void VideoFrameOutput::refreshFrame()
{
    if (!m_active) return;
    if (!m_sink || !isVisible()) { m_frame = {}; update(); return; }
    m_frame = m_sink->videoFrame();
    update();
}

void VideoFrameOutput::paint(QPainter *painter)
{
    painter->fillRect(boundingRect(), QColor("#0a0e14"));
    if (!m_frame.isValid()) return;
    painter->setRenderHint(QPainter::SmoothPixmapTransform);
    QVideoFrame::PaintOptions options;
    options.backgroundColor = QColor("#0a0e14");
    options.aspectRatioMode = Qt::KeepAspectRatio;
    m_frame.paint(painter, boundingRect(), options);
}
