#include "OggOpusWriter.h"
#include <QFile>
#include <QRandomGenerator>
#include <QTemporaryDir>
#include <QtEndian>
#include <cstring>

namespace {
template<typename T> void little(QByteArray &bytes, T value) { const auto offset = bytes.size(); bytes.resize(offset + sizeof(T)); qToLittleEndian<T>(value, bytes.data() + offset); }
}
OggOpusWriter::~OggOpusWriter() { if (m_encoder) opus_encoder_destroy(m_encoder); }
bool OggOpusWriter::fail(const QString &error) { m_error = error; m_file.cancelWriting(); return false; }
bool OggOpusWriter::open(const QString &path)
{
    if (m_encoder) return fail("Запись уже открыта.");
    int status;
    m_encoder = opus_encoder_create(48000, 1, OPUS_APPLICATION_VOIP, &status);
    if (!m_encoder || status != OPUS_OK) return fail("Не удалось запустить кодек Opus.");
    if (opus_encoder_ctl(m_encoder, OPUS_SET_BITRATE_REQUEST, 32000) != OPUS_OK ||
        opus_encoder_ctl(m_encoder, OPUS_SET_COMPLEXITY_REQUEST, 5) != OPUS_OK ||
        opus_encoder_ctl(m_encoder, OPUS_GET_LOOKAHEAD_REQUEST, &m_preSkip) != OPUS_OK) return fail("Не удалось настроить кодек Opus.");
    m_file.setFileName(path);
    if (!m_file.open(QIODevice::WriteOnly)) return fail(m_file.errorString());
    m_file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    m_serial = QRandomGenerator::global()->generate();
    QByteArray head("OpusHead", 8); head.append(char(1)); head.append(char(1));
    little<quint16>(head, m_preSkip); little<quint32>(head, 48000); little<quint16>(head, 0); head.append(char(0));
    QByteArray tags("OpusTags", 8); const QByteArray vendor("VK libopus"); little<quint32>(tags, vendor.size()); tags += vendor; little<quint32>(tags, 0);
    return page(head, 0, 2) && page(tags, 0, 0);
}
bool OggOpusWriter::page(const QByteArray &packet, quint64 granule, quint8 flags)
{
    // Each Opus packet fits in one page; a zero lace terminates exact multiples of 255.
    const int segments = packet.size() / 255 + 1;
    if (segments > 255) return fail("Слишком большой пакет записи.");
    QByteArray bytes("OggS", 4); bytes.append(char(0)); bytes.append(char(flags));
    little<quint64>(bytes, granule); little<quint32>(bytes, m_serial); little<quint32>(bytes, m_sequence++); little<quint32>(bytes, 0);
    bytes.append(char(segments));
    for (int i = 0; i < segments - 1; ++i) bytes.append(char(255));
    bytes.append(char(packet.size() % 255)); bytes += packet;
    quint32 crc = 0;
    for (const auto byte : bytes) { crc ^= quint32(quint8(byte)) << 24; for (int bit = 0; bit < 8; ++bit) crc = (crc << 1) ^ ((crc & 0x80000000u) ? 0x04c11db7u : 0); }
    qToLittleEndian<quint32>(crc, bytes.data() + 22);
    if (m_file.write(bytes) != bytes.size()) return fail(m_file.errorString());
    return true;
}
bool OggOpusWriter::encode(const QByteArray &frame)
{
    opus_int16 samples[960]; std::memcpy(samples, frame.constData(), sizeof(samples));
    unsigned char packet[1275]; const auto size = opus_encode(m_encoder, samples, 960, packet, sizeof(packet));
    if (size < 0) return fail(QString::fromUtf8(opus_strerror(size)));
    if (!m_pending.isEmpty() && !page(m_pending, m_pendingGranule, 0)) return false;
    m_encoded += 960; m_pendingGranule = m_encoded;
    m_pending = QByteArray(reinterpret_cast<const char *>(packet), size); return true;
}
bool OggOpusWriter::append(const QByteArray &pcm)
{
    if (!m_encoder || !m_error.isEmpty() || !m_file.isOpen()) return false;
    m_samples += pcm.size(); // bytes until finalization, including fragments across input callbacks
    m_pcm += pcm;
    while (m_pcm.size() >= 1920) { const auto frame = m_pcm.left(1920); m_pcm.remove(0, 1920); if (!encode(frame)) return false; }
    return true;
}
bool OggOpusWriter::finish()
{
    if (!m_error.isEmpty() || !m_encoder || !m_file.isOpen()) return false;
    if (m_samples == 0 || m_samples % 2) return fail("Запись не содержит звука или повреждена.");
    const auto finalGranule = m_samples / 2 + m_preSkip;
    while (m_encoded < finalGranule) { auto frame = m_pcm.left(1920); m_pcm.remove(0, frame.size()); frame.append(QByteArray(1920 - frame.size(), '\0')); if (!encode(frame)) return false; }
    if (!page(m_pending, finalGranule, 4)) return false;
    if (!m_file.commit()) return fail(m_file.errorString());
    return true;
}
bool OggOpusWriter::selfCheck()
{
    QTemporaryDir directory; OggOpusWriter writer;
    if (!directory.isValid() || !writer.open(directory.filePath("test.ogg")) || !writer.append(QByteArray(9600, '\0')) || !writer.finish()) return false;
    QFile file(directory.filePath("test.ogg"));
    return file.open(QIODevice::ReadOnly) && file.readAll().contains("OpusHead");
}
