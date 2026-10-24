#include "LongPoll.h"
#include "LongPollEvent.h"
#include <QJsonDocument>
#include <QNetworkReply>
#include <QSignalSpy>
#include <QUrlQuery>
#include <QtTest>
#include <cstring>

class Reply : public QNetworkReply {
public:
    Reply(const QNetworkRequest &request, const QByteArray &body, QObject *parent)
        : QNetworkReply(parent), m_body(body) {
        setRequest(request); setUrl(request.url()); open(QIODevice::ReadOnly | QIODevice::Unbuffered);
        if (!body.isEmpty()) QTimer::singleShot(0, this, [this] {
            setAttribute(QNetworkRequest::HttpStatusCodeAttribute, 200); setFinished(true); emit readyRead(); emit finished();
        });
    }
    void abort() override { if (!isFinished()) { setError(OperationCanceledError, "cancelled"); setFinished(true); emit finished(); } }
    qint64 bytesAvailable() const override { return m_body.size() - m_offset + QIODevice::bytesAvailable(); }
protected:
    qint64 readData(char *data, qint64 max) override {
        const auto n = qMin(max, qint64(m_body.size() - m_offset)); if (n <= 0) return -1;
        std::memcpy(data, m_body.constData() + m_offset, n); m_offset += n; return n;
    }
private:
    QByteArray m_body;
    qint64 m_offset = 0;
};

class Network : public QNetworkAccessManager {
public:
    QList<QByteArray> responses, bodies;
    QList<QUrl> requests;
    QList<Operation> operations;
protected:
    QNetworkReply *createRequest(Operation operation, const QNetworkRequest &request, QIODevice *outgoing) override {
        requests.append(request.url()); operations.append(operation);
        bodies.append(outgoing ? outgoing->readAll() : QByteArray());
        return new Reply(request, responses.isEmpty() ? QByteArray() : responses.takeFirst(), this);
    }
};

static QByteArray json(const QJsonObject &value) { return QJsonDocument(value).toJson(QJsonDocument::Compact); }
static QJsonObject credentials(QString ts = "100") { return {{"server_lp", "lp.test/im"}, {"key", "test-key"}, {"ts", ts}}; }

class LongPollTest : public QObject {
    Q_OBJECT
private slots:
    void postV21AndCursorAdvancement() {
        Network network;
        network.responses = {json({{"ts", "101"}, {"pts", 11}, {"updates", QJsonArray{QJsonValue(QJsonArray{10004, 5, 1, 0, 42})}}}),
                             json({{"ts", 102}, {"pts", 12}, {"updates", QJsonArray{}}})};
        LongPoll poll(nullptr, &network); QSignalSpy events(&poll, &LongPoll::batch);
        poll.start(credentials());
        QTRY_COMPARE(network.requests.size(), 3);
        QCOMPARE(events.count(), 2);
        QCOMPARE(network.operations[0], QNetworkAccessManager::PostOperation);
        QCOMPARE(QUrlQuery(network.requests[0]).queryItemValue("mode"), QString("682"));
        QCOMPARE(QUrlQuery(network.requests[0]).queryItemValue("version"), QString("21"));
        QCOMPARE(QUrlQuery(QString::fromUtf8(network.bodies[1])).queryItemValue("ts"), QString("101"));
        QCOMPARE(QUrlQuery(QString::fromUtf8(network.bodies[2])).queryItemValue("ts"), QString("102"));
        QVERIFY(!network.requests[0].query().contains("test-key"));
        poll.stop(); QTest::qWait(30); QCOMPARE(events.count(), 2);
    }
    void failuresRequestDiff_data() {
        QTest::addColumn<int>("failure");
        QTest::newRow("expired-ts") << 1; QTest::newRow("expired-key") << 2; QTest::newRow("lost-session") << 3;
    }
    void failuresRequestDiff() {
        QFETCH(int, failure);
        Network network; network.responses = {json({{"failed", failure}, {"ts", "200"}})};
        LongPoll poll(nullptr, &network); QSignalSpy resync(&poll, &LongPoll::resyncRequired);
        poll.start(credentials()); QTRY_COMPARE_WITH_TIMEOUT(resync.count(), 1, 2000);
        QCOMPARE(network.requests.size(), 1);
        // The owner now obtains new credentials through getDiff, not getLongPollServer.
        poll.start(credentials("300")); QCOMPARE(network.requests.size(), 2);
        QCOMPARE(QUrlQuery(QString::fromUtf8(network.bodies.last())).queryItemValue("ts"), QString("300"));
    }
    void malformedJsonDoesNotCommitCursor() {
        Network network; network.responses = {"invalid-json"};
        LongPoll poll(nullptr, &network); QSignalSpy resync(&poll, &LongPoll::resyncRequired); QSignalSpy batches(&poll, &LongPoll::batch);
        poll.start(credentials()); QTRY_COMPARE_WITH_TIMEOUT(resync.count(), 1, 2000); QCOMPARE(batches.count(), 0);
    }
    void unsupportedVersionStops() {
        Network network; network.responses = {json({{"failed", 4}})};
        LongPoll poll(nullptr, &network); QSignalSpy fatal(&poll, &LongPoll::fatalError);
        poll.start(credentials()); QTRY_COMPARE(fatal.count(), 1); QCOMPARE(network.requests.size(), 1);
    }
    void stopCancelsRecovery() {
        Network network; network.responses = {json({{"failed", 2}})};
        LongPoll poll(nullptr, &network); QSignalSpy resync(&poll, &LongPoll::resyncRequired);
        poll.start(credentials()); QTest::qWait(50); poll.stop(); QTest::qWait(800); QCOMPARE(resync.count(), 0);
    }
    void stopInsideBatchDoesNotRestart() {
        Network network; network.responses = {json({{"ts", "101"}, {"updates", QJsonArray{}}})};
        LongPoll poll(nullptr, &network); connect(&poll, &LongPoll::batch, &poll, [&poll] { poll.stop(); });
        poll.start(credentials()); QTest::qWait(50); QCOMPARE(network.requests.size(), 1);
    }
    void modernEventsUseCorrectPeerAndCmidPositions() {
        QCOMPARE(LongPollEvent::parse(QJsonArray{10004, 91, 1, 999, 42}).peer, QString("42"));
        for (int type : {10001, 10002, 10003, 10005, 10018}) {
            const auto event = LongPollEvent::parse(QJsonArray{type, 91, 1, 42});
            QCOMPARE(event.peer, QString("42")); QCOMPARE(event.kind, LongPollEvent::Persistent);
        }
        for (int type : {10006, 10007, 10013, 10014})
            QCOMPARE(LongPollEvent::parse(QJsonArray{type, 42, 91}).peer, QString("42"));
        QCOMPARE(LongPollEvent::parse(QJsonArray{10019, 91, 42}).peer, QString("42"));
        QCOMPARE(LongPollEvent::parse(QJsonArray{601, 2, 42, 91}).peer, QString("42"));
        QCOMPARE(LongPollEvent::parse(QJsonArray{10004, 1}).kind, LongPollEvent::Invalid);
        QCOMPARE(LongPollEvent::parse(QJsonArray{98765, "extension"}).kind, LongPollEvent::Unknown);
        for (int type : {501, 502, 503, 504, 505, 506, 507, 508, 509, 10501, 10502, 10503, 10504, 10505, 10506, 10507, 10508, 10509}) QCOMPARE(LongPollEvent::parse(QJsonArray{type, QJsonObject{}}).kind, LongPollEvent::Folders);
    }
};
QTEST_GUILESS_MAIN(LongPollTest)
#include "LongPollTest.moc"
