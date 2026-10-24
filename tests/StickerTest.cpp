#include "LottieSticker.h"
#include <QSignalSpy>
#include <QTemporaryFile>
#include <QtTest>
#include <zlib.h>

class StickerTest : public QObject {
    Q_OBJECT
private slots:
    void rendersDifferentFramesAndPauses()
    {
        LottieSticker sticker;
        sticker.setWidth(64); sticker.setHeight(64);
        sticker.setPlaying(false);
        sticker.setSource(QUrl::fromLocalFile(QStringLiteral(STICKER_FIXTURE_PATH)));
        QTRY_VERIFY(sticker.ready());
        QCOMPARE(sticker.totalFrames(), 30);
        QCOMPARE(sticker.currentFrame(), 0);
        const auto first = sticker.snapshot();
        QVERIFY(!first.isNull());
        bool colored = false;
        for (int y = 0; y < first.height(); ++y)
            for (int x = 0; x < first.width(); ++x) colored |= qAlpha(first.pixel(x, y)) > 0;
        QVERIFY(colored);
        sticker.setPlaying(true);
        QTRY_VERIFY(sticker.currentFrame() >= 5);
        QVERIFY(sticker.snapshot() != first);
        sticker.setPlaying(false);
        QTest::qWait(80); // allow the last already-running render to complete
        const auto paused = sticker.currentFrame();
        QTest::qWait(150);
        QCOMPARE(sticker.currentFrame(), paused);
    }
    void acceptsGzipAndRejectsInvalidJson()
    {
        QFile source(QStringLiteral(STICKER_FIXTURE_PATH)); QVERIFY(source.open(QIODevice::ReadOnly));
        auto bytes = source.readAll();
        QByteArray compressed(8192, Qt::Uninitialized);
        z_stream stream{};
        QCOMPARE(deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 16 + MAX_WBITS, 8, Z_DEFAULT_STRATEGY), Z_OK);
        stream.next_in = reinterpret_cast<Bytef *>(bytes.data()); stream.avail_in = bytes.size();
        stream.next_out = reinterpret_cast<Bytef *>(compressed.data()); stream.avail_out = compressed.size();
        QCOMPARE(deflate(&stream, Z_FINISH), Z_STREAM_END);
        compressed.resize(stream.total_out); deflateEnd(&stream);
        QTemporaryFile gzip;
        QVERIFY(gzip.open()); gzip.write(compressed); gzip.flush();
        LottieSticker sticker;
        sticker.setPlaying(false);
        sticker.setSource(QUrl::fromLocalFile(gzip.fileName()));
        QTRY_VERIFY(sticker.ready());
        QTemporaryFile invalid;
        QVERIFY(invalid.open()); invalid.write("not a lottie animation"); invalid.flush();
        sticker.setSource(QUrl::fromLocalFile(invalid.fileName()));
        QTRY_VERIFY(!sticker.error().isEmpty());
        QVERIFY(!sticker.ready());
    }
    void changingSourceDiscardsOldBackgroundWork()
    {
        LottieSticker sticker;
        sticker.setSource(QUrl::fromLocalFile(QStringLiteral(STICKER_FIXTURE_PATH)));
        sticker.setSource(QUrl());
        QTest::qWait(150);
        QVERIFY(!sticker.ready());
        QCOMPARE(sticker.totalFrames(), 0);
        QVERIFY(sticker.snapshot().isNull());
    }
};
QTEST_MAIN(StickerTest)
#include "StickerTest.moc"
