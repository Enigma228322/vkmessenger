#include "media/OggOpusWriter.h"
#include <QFile>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>
#include <cmath>

class VoiceCodecTest : public QObject {
    Q_OBJECT
private slots:
    void pcmRoundTrip_data() {
        QTest::addColumn<int>("samples");
        QTest::newRow("short") << 480;
        QTest::newRow("exact-frame") << 960;
        QTest::newRow("partial-frame") << 48233;
    }
    void pcmRoundTrip() {
        QFETCH(int, samples);
        QTemporaryDir directory;
        const auto path = directory.filePath("voice.ogg");
        OggOpusWriter writer; QVERIFY2(writer.open(path), qPrintable(writer.error()));
        QByteArray pcm(samples * 2, '\0');
        for (int i = 0; i < samples; ++i) qToLittleEndian<qint16>(12000 * std::sin(i * 2 * 3.141592653589793 * 440 / 48000), pcm.data() + i * 2);
        // Deliberately split sample boundaries as a QIODevice may do.
        for (int offset = 0; offset < pcm.size(); offset += 137) QVERIFY(writer.append(pcm.mid(offset, 137)));
        QVERIFY2(writer.finish(), qPrintable(writer.error()));
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); const auto bytes = file.readAll();
        int status; auto decoder = opus_decoder_create(48000, 1, &status); QCOMPARE(status, 0); QVERIFY(decoder);
        const auto cleanup = qScopeGuard([decoder] { opus_decoder_destroy(decoder); });
        int offset = 0, pageNumber = 0, preSkip = 0, decoded = 0;
        quint32 serial = 0; quint64 lastGranule = 0; double energy = 0; bool eos = false;
        while (offset < bytes.size()) {
            QVERIFY(bytes.size() - offset >= 27); QCOMPARE(bytes.mid(offset, 4), "OggS");
            const int segments = quint8(bytes[offset + 26]); int size = 0;
            QVERIFY(offset + 27 + segments <= bytes.size());
            for (int i = 0; i < segments; ++i) size += quint8(bytes[offset + 27 + i]);
            const int length = 27 + segments + size; QVERIFY(offset + length <= bytes.size());
            auto page = bytes.mid(offset, length);
            const auto checksum = qFromLittleEndian<quint32>(page.constData() + 22); page.replace(22, 4, QByteArray(4, '\0'));
            quint32 crc = 0;
            for (auto byte : page) { crc ^= quint32(quint8(byte)) << 24; for (int bit = 0; bit < 8; ++bit) crc = (crc << 1) ^ ((crc & 0x80000000u) ? 0x04c11db7u : 0); }
            QCOMPARE(crc, checksum); QCOMPARE(qFromLittleEndian<quint32>(page.constData() + 18), quint32(pageNumber));
            if (pageNumber == 0) serial = qFromLittleEndian<quint32>(page.constData() + 14);
            QCOMPARE(qFromLittleEndian<quint32>(page.constData() + 14), serial);
            const auto packet = page.mid(27 + segments);
            const auto granule = qFromLittleEndian<quint64>(page.constData() + 6); QVERIFY(granule >= lastGranule); lastGranule = granule;
            if (pageNumber == 0) { QVERIFY(packet.startsWith("OpusHead")); QCOMPARE(quint8(page[5]), 2); preSkip = qFromLittleEndian<quint16>(packet.constData() + 10); QVERIFY(preSkip > 0); }
            else if (pageNumber == 1) QVERIFY(packet.startsWith("OpusTags"));
            else {
                opus_int16 output[960]; const auto count = opus_decode(decoder, reinterpret_cast<const unsigned char *>(packet.constData()), packet.size(), output, 960, 0);
                QCOMPARE(count, 960); decoded += count;
                for (const auto sample : output) energy += double(sample) * sample;
                eos = quint8(page[5]) & 4;
                if (eos) QCOMPARE(offset + length, bytes.size());
            }
            offset += length; ++pageNumber;
        }
        QVERIFY(eos); QVERIFY(pageNumber >= 3); QCOMPARE(lastGranule - preSkip, quint64(samples));
        QVERIFY(decoded >= samples + preSkip); QVERIFY(energy > 10000 * samples);
    }
    void abortAndInvalidInputLeaveNoFile() {
        QTemporaryDir directory; const auto path = directory.filePath("cancelled.ogg");
        { OggOpusWriter writer; QVERIFY(writer.open(path)); QVERIFY(writer.append(QByteArray(1920, '\0'))); }
        QVERIFY(!QFile::exists(path));
        { OggOpusWriter writer; QVERIFY(writer.open(path)); QVERIFY(!writer.finish()); QVERIFY(!writer.error().isEmpty()); }
        QVERIFY(!QFile::exists(path));
        { OggOpusWriter writer; QVERIFY(writer.open(path)); QVERIFY(writer.append(QByteArray(1, '\0'))); QVERIFY(!writer.finish()); }
        QVERIFY(!QFile::exists(path));
        QVERIFY(OggOpusWriter::selfCheck());
    }
};
QTEST_GUILESS_MAIN(VoiceCodecTest)
#include "VoiceCodecTest.moc"
