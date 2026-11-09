#pragma once
#include <QQuickPaintedItem>
#include <QVideoFrame>
#include <QPointer>
#include <QVideoSink>
#include <QtQml/qqmlregistration.h>

// Qt's native VideoOutput requires RHI. Supply a CPU surface only when the
// window uses the software scene graph; hardware rendering keeps its native path.
class VideoFrameOutput : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVideoSink *videoSink READ videoSink WRITE setVideoSink NOTIFY videoSinkChanged)
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
public:
    explicit VideoFrameOutput(QQuickItem *parent = nullptr);
    QVideoSink *videoSink() const { return m_sink; }
    void setVideoSink(QVideoSink *sink);
    bool active() const { return m_active; }
    void paint(QPainter *painter) override;
signals:
    void videoSinkChanged();
    void activeChanged();
private:
    void refreshBackend();
    void refreshFrame();
    QPointer<QVideoSink> m_sink;
    QMetaObject::Connection m_frameConnection, m_backendConnection;
    QVideoFrame m_frame;
    bool m_active = false;
};
