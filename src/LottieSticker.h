#pragma once
#include <QQuickPaintedItem>
#include <QtQml/qqmlregistration.h>
#include <QImage>
#include <QTimer>
#include <QElapsedTimer>
#include <QPointer>
#include <memory>

class QNetworkReply;

class LottieSticker : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QUrl source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(bool playing READ playing WRITE setPlaying NOTIFY playingChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
    Q_PROPERTY(int totalFrames READ totalFrames NOTIFY stateChanged)
    Q_PROPERTY(int currentFrame READ currentFrame NOTIFY frameChanged)
public:
    explicit LottieSticker(QQuickItem *parent = nullptr);
    ~LottieSticker() override;
    QUrl source() const { return m_source; }
    void setSource(const QUrl &source);
    bool playing() const { return m_playing; }
    void setPlaying(bool playing);
    bool ready() const { return m_ready; }
    QString error() const { return m_error; }
    int totalFrames() const;
    int currentFrame() const { return m_frame; }
    QImage snapshot() const { return m_image; }
    void paint(QPainter *painter) override;
signals:
    void sourceChanged();
    void playingChanged();
    void stateChanged();
    void frameChanged();
protected:
    void geometryChange(const QRectF &now, const QRectF &before) override;
private:
    struct State;
    void parse(QByteArray bytes, quint64 generation);
    void fail(const QString &error);
    void updatePlayback();
    void renderFrame(bool force = false);
    QUrl m_source;
    std::shared_ptr<State> m_state;
    QPointer<QNetworkReply> m_reply;
    QTimer m_timer;
    QElapsedTimer m_clock;
    QImage m_image;
    QSize m_renderedSize;
    quint64 m_generation = 0;
    int m_frame = -1, m_resumeFrame = 0;
    bool m_playing = true, m_ready = false, m_rendering = false, m_renderAgain = false;
    QString m_error;
};
