#pragma once
#include <QByteArray>
#include <QSaveFile>
#include <QString>
#include "OpusApi.h"

// Streaming 48 kHz mono signed-16 PCM to RFC 7845 Ogg Opus.
// Owned exclusively by the recorder's encoding thread.
class OggOpusWriter {
public:
    OggOpusWriter() = default;
    ~OggOpusWriter();
    bool open(const QString &path);
    bool append(const QByteArray &pcm);
    bool finish();
    QString error() const { return m_error; }
    static bool selfCheck();
private:
    bool encode(const QByteArray &frame);
    bool page(const QByteArray &packet, quint64 granule, quint8 flags);
    bool fail(const QString &error);
    QSaveFile m_file;
    OpusEncoder *m_encoder = nullptr;
    QByteArray m_pcm, m_pending;
    QString m_error;
    quint32 m_serial = 0, m_sequence = 0;
    quint64 m_samples = 0, m_encoded = 0, m_pendingGranule = 0;
    int m_preSkip = 0;
};
