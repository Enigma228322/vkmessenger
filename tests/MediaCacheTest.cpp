#include "media/MediaCache.h"
#include <QFile>
#include <QNetworkReply>
#include <QTemporaryDir>
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
        }
        MediaCache cache(directory.path(), nullptr, &network);
        cache.setAccount("user:1"); QCOMPARE(cache.source(url), first); QCOMPARE(network.requests, 2);
        QVERIFY(cache.source(QUrl("file:///etc/passwd")).isEmpty());
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
};
QTEST_GUILESS_MAIN(MediaCacheTest)
#include "MediaCacheTest.moc"
