#pragma once
#include <QQuickPaintedItem>
#include <QImage>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

class AvatarImage : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QUrl source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(int status READ status NOTIFY statusChanged)
public:
    explicit AvatarImage(QQuickItem *parent = nullptr);
    QUrl source() const { return m_source; }
    int status() const { return m_status; }
    void setSource(const QUrl &source);
    void paint(QPainter *painter) override;
signals:
    void sourceChanged();
    void statusChanged();
private:
    QUrl m_source;
    QImage m_image;
    QString m_cacheKey;
    int m_status = 0;
    quint64 m_generation = 0;
};
