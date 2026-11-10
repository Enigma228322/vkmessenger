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
    AssetReply(const QNetworkRequest &request, QByteArray content, QObject *parent) : QNetworkReply(parent), m_content(std::move(content)) {
        setRequest(request); setUrl(request.url()); open(QIODevice::ReadOnly | QIODevice::Unbuffered);
        QTimer::singleShot(0, this, [this] { setAttribute(QNetworkRequest::HttpStatusCodeAttribute, 200); setFinished(true); emit readyRead(); emit finished(); });
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
protected:
    QNetworkReply *createRequest(Operation, const QNetworkRequest &request, QIODevice *) override { ++requests; return new AssetReply(request, content, this); }
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
