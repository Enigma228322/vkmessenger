#pragma once
#include <QObject>
#include <QUrl>
#include <QAudioSource>
#include <QThread>
#include <QTimer>
#include <memory>
#include "OggOpusWriter.h"

class VoiceRecorder : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool recording READ recording NOTIFY changed)
    Q_PROPERTY(bool ready READ ready NOTIFY changed)
    Q_PROPERTY(bool requestingPermission READ requestingPermission NOTIFY changed)
    Q_PROPERTY(bool finalizing READ finalizing NOTIFY changed)
    Q_PROPERTY(qint64 duration READ duration NOTIFY changed)
    Q_PROPERTY(QUrl file READ file NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
public:
    explicit VoiceRecorder(QObject *parent = nullptr);
    ~VoiceRecorder() override;
    bool recording() const { return m_recording; }
    bool ready() const { return m_ready; }
    bool requestingPermission() const { return m_requesting; }
    bool finalizing() const { return m_finalizing; }
    qint64 duration() const { return m_duration; }
    QUrl file() const { return m_file; }
    QString error() const { return m_error; }
    Q_INVOKABLE void start();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void discard();
    Q_INVOKABLE QUrl release();
signals:
    void changed();
private:
    void begin();
    void consume();
    QThread m_thread;
    QObject *m_worker;
    std::unique_ptr<OggOpusWriter> m_writer; // accessed on m_thread only
    std::unique_ptr<QAudioSource> m_source;
    QIODevice *m_input = nullptr;
    QTimer m_tick;
    QUrl m_file;
    QString m_error;
    qint64 m_duration = 0, m_pcmBytes = 0;
    bool m_recording = false, m_ready = false, m_requesting = false, m_finalizing = false;
    quint64 m_generation = 0;
};
