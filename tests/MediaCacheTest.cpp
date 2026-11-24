#include "media/MediaCache.h"
#include <QFile>
#include <QNetworkReply>
#include <QTemporaryDir>
#include <QCryptographicHash>
#include <QTimer>
#include <QtTest>
#include <cstring>

class AssetReply : public QNetworkReply {
public:
    AssetReply(const QNetworkRequest &request, QByteArray content, QObject *parent, bool hold = false) : QNetworkReply(parent), m_content(std::move(content)) {
        setRequest(request); setUrl(request.url()); open(QIODevice::ReadOnly | QIODevice::Unbuffered);
        if (!hold) QTimer::singleShot(0, this, [this] { finish(); });
    }
    void finish(bool signalReadyRead = true) {
        if (isFinished()) return;
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, 200); setFinished(true);
        if (signalReadyRead) emit readyRead();
        emit finished();
    }
    void abort() override { if (isFinished()) return; setError(OperationCanceledError, "cancelled"); setFinished(true); emit finished(); }
    qint64 bytesAvailable() const override { return m_content.size() - m_offset + QIODevice::bytesAvailable(); }
protected:
    qint64 readData(char *data, qint64 max) override { auto count = qMin(max, qint64(m_content.size() - m_offset)); if (count <= 0) return -1; std::memcpy(data, m_content.constData() + m_offset, size_t(count)); m_offset += count; return count; }
private:
    QByteArray m_content;
    qsizetype m_offset = 0;
};
class AssetNetwork : public QNetworkAccessManager {
public:
    int requests = 0;
    QByteArray content = "fixture-content";
    bool hold = false;
    QList<QPointer<AssetReply>> replies;
protected:
    QNetworkReply *createRequest(Operation, const QNetworkRequest &request, QIODevice *) override {
        ++requests; auto reply = new AssetReply(request, content, this, hold); replies.append(reply); return reply;
    }
};

// Generate a large reply on demand, without allocating a payload-sized fixture.
// This isolates the consumer's temporary buffers from the test's own memory use.
class GeneratedAssetReply : public QNetworkReply {
public:
    static constexpr qint64 PayloadSize = 96 * 1048576;
    qint64 largestRead = 0, consumed = 0;
    explicit GeneratedAssetReply(const QNetworkRequest &request, QObject *parent) : QNetworkReply(parent) {
        setRequest(request); setUrl(request.url()); open(QIODevice::ReadOnly | QIODevice::Unbuffered);
    }
    void finish(bool readyReadFirst) {
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, 200); setFinished(true);
        if (readyReadFirst) emit readyRead();
        emit finished();
    }
    void abort() override { setError(OperationCanceledError, "cancelled"); setFinished(true); emit finished(); }
    qint64 bytesAvailable() const override { return PayloadSize - consumed + QIODevice::bytesAvailable(); }
protected:
    qint64 readData(char *data, qint64 maximum) override {
        largestRead = qMax(largestRead, maximum);
        const auto count = qMin(maximum, PayloadSize - consumed);
        if (count <= 0) return -1;
        std::memset(data, 'm', size_t(count)); consumed += count; return count;
    }
};
class GeneratedAssetNetwork : public QNetworkAccessManager {
public:
    QPointer<GeneratedAssetReply> reply;
protected:
    QNetworkReply *createRequest(Operation, const QNetworkRequest &request, QIODevice *) override {
        reply = new GeneratedAssetReply(request, this); return reply;
    }
};

class MediaCacheTest : public QObject {
    Q_OBJECT
    QTemporaryDir m_settings;
private slots:
    void initTestCase() { QSettings::setDefaultFormat(QSettings::IniFormat); QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settings.path()); }
    void persistedFilesArePartitionedAndRequestsAreCoalesced()
    {
        QTemporaryDir directory;
        AssetNetwork network;
        const QUrl url("https://assets.test/photo");
        QUrl first;
        {
            MediaCache cache(directory.path(), nullptr, &network);
            cache.setAccount("user:1");
            QVERIFY(cache.source(url).isEmpty()); QVERIFY(cache.source(url).isEmpty());
            QTRY_VERIFY(!cache.cached(url).isEmpty());
            first = cache.cached(url);
            QCOMPARE(network.requests, 1);
            QFile file(first.toLocalFile()); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), network.content);
            cache.setAccount("user:2"); QVERIFY(cache.cached(url).isEmpty());
            cache.source(url); QTRY_VERIFY(!cache.cached(url).isEmpty());
            QVERIFY(cache.cached(url) != first);
            cache.clear(); QVERIFY(cache.cached(url).isEmpty()); QVERIFY(QFile::exists(first.toLocalFile()));
            QTRY_VERIFY(!cache.clearing()); QVERIFY(cache.clearError().isEmpty());
        }
        MediaCache cache(directory.path(), nullptr, &network);
        cache.setAccount("user:1"); QCOMPARE(cache.source(url), first); QCOMPARE(network.requests, 2);
        QVERIFY(cache.source(QUrl("file:///etc/passwd")).isEmpty());
    }
    void largeReplyUsesBoundedBuffers_data()
    {
        QTest::addColumn<bool>("readyReadFirst");
        QTest::newRow("ready-read") << true;
        QTest::newRow("finish") << false;
    }
    void largeReplyUsesBoundedBuffers()
    {
        QFETCH(bool, readyReadFirst);
        QTemporaryDir directory; GeneratedAssetNetwork network;
        MediaCache cache(directory.path(), nullptr, &network);
        cache.setLimit(128 * 1048576); cache.setAccount("user:bounded-memory");
        const QUrl url("https://assets.test/generated-large-file");
        cache.source(url); QTRY_VERIFY(network.reply);
        network.reply->finish(readyReadFirst);
        QVERIFY(!cache.cached(url).isEmpty()); QCOMPARE(cache.pending(), 0);
        QCOMPARE(cache.bytes(), GeneratedAssetReply::PayloadSize);
        QFile file(cache.cached(url).toLocalFile()); QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.size(), GeneratedAssetReply::PayloadSize);
        const QByteArray block(64 * 1024, 'm');
        while (!file.atEnd()) QCOMPARE(file.read(block.size()), block);
        qInfo() << "largest device read:" << network.reply->largestRead << "bytes";
        QVERIFY2(network.reply->largestRead <= 64 * 1024, "A large reply must not allocate a payload-sized read buffer");
        QVERIFY(network.reply->readBufferSize() > 0 && network.reply->readBufferSize() <= 256 * 1024);
    }
    void assetNotificationsAreScopedAndResetOnAccountChanges()
    {
        QTemporaryDir directory; AssetNetwork network;
        MediaCache cache(directory.path(), nullptr, &network);
        QSignalSpy updates(&cache, &MediaCache::sourceUpdated), resets(&cache, &MediaCache::sourcesReset);
        cache.setAccount("user:1"); QCOMPARE(resets.count(), 1);
        const QUrl first("https://assets.test/first"), second("https://assets.test/second");
        cache.source(first); cache.source(second);
        QTRY_COMPARE(updates.count(), 2);
        QSet<QUrl> received; for (const auto &update : updates) received.insert(update.first().toUrl());
        QCOMPARE(received, QSet<QUrl>({first, second}));
        cache.setAccount("user:2"); QCOMPARE(resets.count(), 2);
        QVERIFY(cache.cached(first).isEmpty());
        cache.clear(); QCOMPARE(resets.count(), 3);
        QTRY_VERIFY(!cache.clearing()); QCOMPARE(resets.count(), 4);
    }

    void clearingBlocksOnlyItsAccountAndAllowsFreshDownloadsAfterwards()
    {
        QTemporaryDir directory; AssetNetwork network;
        MediaCache cache(directory.path(), nullptr, &network);
        const QUrl url("https://assets.test/clear-photo");
        cache.setAccount("user:2"); cache.source(url); QTRY_VERIFY(!cache.cached(url).isEmpty());
        const auto other = cache.cached(url);
        cache.setAccount("user:1"); cache.source(url); QTRY_VERIFY(!cache.cached(url).isEmpty());
        const auto old = cache.cached(url);
        cache.clear(); QVERIFY(cache.clearing()); QVERIFY(cache.cached(url).isEmpty());
        QVERIFY(cache.source(url).isEmpty()); QCOMPARE(cache.pending(), 0);
        cache.clear(); QVERIFY(cache.clearing()); // coalesce repeated requests
        cache.setAccount("user:2"); QCOMPARE(cache.source(url), other);
        const QUrl fresh("https://assets.test/other-account-fresh");
        cache.source(fresh); QTRY_VERIFY(!cache.cached(fresh).isEmpty());
        QTRY_VERIFY(!cache.clearing()); QVERIFY(cache.clearError().isEmpty());
        QVERIFY(!QFile::exists(old.toLocalFile())); QVERIFY(QFile::exists(other.toLocalFile()));
        cache.setAccount("user:1"); QVERIFY(cache.cached(url).isEmpty());
        cache.source(url); QTRY_VERIFY(!cache.cached(url).isEmpty());
        QCOMPARE(network.requests, 4);
        QTest::qWait(50); QVERIFY(QFile::exists(cache.cached(url).toLocalFile()));
    }

    void clearReportsFailureAndCanBeRetried()
    {
        QTemporaryDir directory; AssetNetwork network;
        const auto path = directory.filePath(QString::fromLatin1(QCryptographicHash::hash(QByteArray("user:1"), QCryptographicHash::Sha256).toHex()));
        QFile obstruction(path); QVERIFY(obstruction.open(QIODevice::WriteOnly)); obstruction.write("fixture"); obstruction.close();
        MediaCache cache(directory.path(), nullptr, &network); cache.setAccount("user:1");
        cache.clear(); QVERIFY(cache.clearing()); QTRY_VERIFY(!cache.clearing());
        QVERIFY(!cache.clearError().isEmpty()); QVERIFY(QFile::exists(path));
        QVERIFY(QFile::remove(path));
        cache.clear(); QTRY_VERIFY(!cache.clearing()); QVERIFY(cache.clearError().isEmpty());
    }

    void retryReplacesAnUndecodableCachedImage()
    {
        QTemporaryDir directory; AssetNetwork network;
        MediaCache cache(directory.path(), nullptr, &network); cache.setAccount("user:1");
        const QUrl url("https://assets.test/image.webp");
        network.content = "invalid-image"; cache.source(url); QTRY_VERIFY(!cache.cached(url).isEmpty());
        network.content = "replacement-image"; cache.retry(url);
        QTRY_COMPARE(network.requests, 2); QTRY_VERIFY(!cache.cached(url).isEmpty());
        QFile file(cache.cached(url).toLocalFile()); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), network.content);
    }
    void loweredBudgetRejectsInFlightDownloadWithoutReloadLoop_data()
    {
        QTest::addColumn<bool>("signalReadyRead");
        QTest::newRow("ready-read") << true;
        QTest::newRow("remaining-at-finish") << false;
    }
    void loweredBudgetRejectsInFlightDownloadWithoutReloadLoop()
    {
        QFETCH(bool, signalReadyRead);
        QTemporaryDir directory; AssetNetwork network;
        network.hold = true; network.content = QByteArray(65 * 1048576, 'a');
        MediaCache cache(directory.path(), nullptr, &network);
        cache.setLimit(128 * 1048576); cache.setAccount("user:1");
        const QUrl url("https://assets.test/large-image");
        // Visible images ask for their source again whenever this notification arrives.
        connect(&cache, &MediaCache::sourceUpdated, &cache, [&](const QUrl &remote) { cache.source(remote); });
        cache.source(url); QTRY_COMPARE(network.requests, 1); QCOMPARE(cache.pending(), 1);
        cache.setLimit(64 * 1048576);
        network.replies.first()->finish(signalReadyRead);
        QVERIFY(cache.cached(url).isEmpty()); QVERIFY(!cache.error(url).isEmpty());
        QCOMPARE(cache.pending(), 0); QCOMPARE(cache.bytes(), 0);
        QTest::qWait(20); QCOMPARE(network.requests, 1);

        cache.setLimit(128 * 1048576); cache.retry(url);
        QTRY_COMPARE(network.requests, 2); network.replies.last()->finish(signalReadyRead);
        QVERIFY(cache.error(url).isEmpty()); QVERIFY(!cache.cached(url).isEmpty());
        QCOMPARE(cache.pending(), 0); QCOMPARE(cache.bytes(), qint64(network.content.size()));
        QFile file(cache.cached(url).toLocalFile()); QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), network.content);
    }
    void byteBudgetEvictsOlderFiles()
    {
        QTemporaryDir directory;
        AssetNetwork network;
        network.content = QByteArray(35 * 1048576, 'a');
        MediaCache cache(directory.path(), nullptr, &network);
        cache.setLimit(64 * 1048576);
        cache.setAccount("user:1");
        const QUrl first("https://assets.test/first"), second("https://assets.test/second");
        cache.source(first); QTRY_VERIFY(!cache.cached(first).isEmpty());
        QTest::qWait(5);
        cache.source(second); QTRY_VERIFY(!cache.cached(second).isEmpty());
        QVERIFY(cache.cached(first).isEmpty()); QVERIFY(cache.bytes() <= cache.limit());
    }
    void repeatedAccessKeepsFrequentlyViewedFiles()
    {
        QTemporaryDir directory;
        AssetNetwork network;
        network.content = QByteArray(23 * 1048576, 'a');
        MediaCache cache(directory.path(), nullptr, &network);
        cache.setLimit(64 * 1048576);
        cache.setAccount("user:1");
        const QUrl first("https://assets.test/frequent"), second("https://assets.test/older"), third("https://assets.test/new");
        cache.source(first); QTRY_VERIFY(!cache.cached(first).isEmpty());
        const auto firstLocal = cache.source(first);
        QTest::qWait(5);
        cache.source(second); QTRY_VERIFY(!cache.cached(second).isEmpty());
        QTest::qWait(5);
        QCOMPARE(cache.source(first), firstLocal);
        QTest::qWait(5);
        cache.source(third); QTRY_VERIFY(!cache.cached(third).isEmpty());
        QCOMPARE(cache.cached(first), firstLocal);
        QVERIFY(cache.cached(second).isEmpty());
        QVERIFY(cache.bytes() <= cache.limit());
        QCOMPARE(network.requests, 3);
    }
};
QTEST_GUILESS_MAIN(MediaCacheTest)
#include "MediaCacheTest.moc"
