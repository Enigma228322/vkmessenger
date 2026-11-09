#include "Transfers.h"
#include <QNetworkReply>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonArray>
#include <QSignalSpy>
#include <QtTest>
#include <cstring>

class TransferReply : public QNetworkReply {
public:
    TransferReply(const QNetworkRequest &request, QByteArray data, QObject *parent, NetworkError failure = NoError, int status = 200)
        : QNetworkReply(parent), m_data(std::move(data)) {
        setRequest(request); setUrl(request.url()); open(QIODevice::ReadOnly | QIODevice::Unbuffered);
        QTimer::singleShot(0, this, [this, failure, status] {
            setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
            if (failure != NoError) setError(failure, "https://pu.upload.test/private?hash=secret-upload-signature");
            setFinished(true); emit readyRead(); emit finished();
        });
    }
    void abort() override { if (!isFinished()) { setError(OperationCanceledError, "cancelled"); setFinished(true); emit finished(); } }
    qint64 bytesAvailable() const override { return m_data.size() - m_offset + QIODevice::bytesAvailable(); }
protected:
    qint64 readData(char *data, qint64 max) override {
        const auto size = qMin(max, qint64(m_data.size() - m_offset)); if (size <= 0) return -1;
        std::memcpy(data, m_data.constData() + m_offset, size); m_offset += size; return size;
    }
private:
    QByteArray m_data;
    qint64 m_offset = 0;
};
class TransferNetwork : public QNetworkAccessManager {
public:
    QByteArray body, response = R"({"file":"uploaded-file"})";
    QList<QUrl> urls;
    QList<QNetworkRequest> requests;
    QList<QByteArray> responses;
    QList<QNetworkReply::NetworkError> failures;
    QList<int> statuses;
protected:
    QNetworkReply *createRequest(Operation, const QNetworkRequest &request, QIODevice *outgoing) override {
        urls.append(request.url()); body = outgoing ? outgoing->readAll() : QByteArray();
        requests.append(request);
        return new TransferReply(request, responses.isEmpty() ? response : responses.takeFirst(), this,
            failures.isEmpty() ? QNetworkReply::NoError : failures.takeFirst(), statuses.isEmpty() ? 200 : statuses.takeFirst());
    }
};

class TransfersTest : public QObject {
    Q_OBJECT
private slots:
    void uploadRefreshesAddressAndReplaysStagedFileOnce()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("Документ.txt");
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("staged payload"); file.close();
        TransferNetwork network; network.failures = {QNetworkReply::RemoteHostClosedError, QNetworkReply::NoError};
        int requests = 0;
        Transfers transfers([&](const QString &method, Form, Transfers::Result result) {
            if (method == "docs.getMessagesUploadServer") {
                if (++requests == 1) QVERIFY(QFile::remove(path)); // only the staged copy survives
                result(QJsonObject{{"upload_url", QString("https://pu%1.upload.test/file?hash=private").arg(requests)}}, {});
            } else result(QJsonObject{{"type", "doc"}, {"doc", QJsonObject{{"id", 9}, {"owner_id", 42}}}}, {});
        }, nullptr, &network);
        transfers.setWorkspace(directory.filePath("uploads"));
        transfers.upload(QUrl::fromLocalFile(path), "77", "doc");
        QTRY_COMPARE(transfers.attachments("77").size(), 1);
        QCOMPARE(requests, 2); QCOMPARE(network.urls.size(), 2);
        QCOMPARE(network.urls.last().host(), QString("pu2.upload.test"));
        QVERIFY(!network.requests.last().attribute(QNetworkRequest::Http2AllowedAttribute).toBool());
        QVERIFY(network.body.contains("staged payload")); QVERIFY(network.body.contains(QString("Документ.txt").toUtf8()));
        QVERIFY(network.body.toLower().contains("content-type: text/plain"));
        QVERIFY(transfers.property("error").toString().isEmpty());
    }

    void persistentUploadFailureCanBeRetriedWithoutExposingSignedUrl()
    {
        QTemporaryDir directory; const auto path = directory.filePath("file.txt");
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("retry payload"); file.close();
        TransferNetwork network; network.failures = {QNetworkReply::HostNotFoundError, QNetworkReply::HostNotFoundError};
        Transfers transfers([](const QString &method, Form, Transfers::Result result) {
            if (method == "docs.getMessagesUploadServer") result(QJsonObject{{"upload_url", "https://pu.upload.test/file?hash=secret-upload-signature"}}, {});
            else result(QJsonObject{{"type", "doc"}, {"doc", QJsonObject{{"id", 9}, {"owner_id", 42}}}}, {});
        }, nullptr, &network);
        transfers.upload(QUrl::fromLocalFile(path), "77", "doc");
        QTRY_VERIFY(!transfers.property("error").toString().isEmpty());
        QCOMPARE(network.urls.size(), 2); QVERIFY(transfers.canRetry());
        const auto error = transfers.property("error").toString();
        QVERIFY(error.contains("pu.upload.test")); QVERIFY(error.contains("DNS"));
        QVERIFY(!error.contains("secret")); QVERIFY(!error.contains("https://"));
        transfers.retry(); QTRY_COMPARE(transfers.attachments("77").size(), 1);
        QCOMPARE(network.urls.size(), 3);
    }

    void cancelStopsScheduledUploadRetry()
    {
        QTemporaryDir directory; QFile file(directory.filePath("file.txt"));
        QVERIFY(file.open(QIODevice::WriteOnly)); file.write("payload"); file.close();
        TransferNetwork network; network.failures = {QNetworkReply::RemoteHostClosedError};
        int requests = 0;
        Transfers transfers([&](const QString &, Form, Transfers::Result result) { ++requests; result(QJsonObject{{"upload_url", "https://pu.upload.test/file"}}, {}); }, nullptr, &network);
        transfers.upload(QUrl::fromLocalFile(file.fileName()), "77", "doc");
        QTRY_COMPARE(transfers.property("status").toString(), QString("Повторяем соединение…"));
        transfers.cancel(); QTest::qWait(450);
        QCOMPARE(requests, 1); QVERIFY(!transfers.busy()); QVERIFY(!transfers.canRetry());
    }

    void photoUploadChecksPayloadAndSendsMimeType()
    {
        QTemporaryDir directory; QFile file(directory.filePath("Фото.png"));
        QVERIFY(file.open(QIODevice::WriteOnly)); file.write("png-payload"); file.close();
        TransferNetwork network;
        network.responses = {R"({"server":1,"photo":"[]","hash":""})", R"({"server":1,"photo":"uploaded-photo","hash":"hash"})"};
        int saves = 0;
        Transfers transfers([&](const QString &method, Form params, Transfers::Result result) {
            if (method == "photos.getMessagesUploadServer") result(QJsonObject{{"upload_url", "https://pu.upload.test/photo"}}, {});
            else { ++saves; QCOMPARE(method, QString("photos.saveMessagesPhoto")); QVERIFY(params.contains(qMakePair(QString("photo"), QString("uploaded-photo")))); result(QJsonArray{QJsonObject{{"id", 8}, {"owner_id", 42}}}, {}); }
        }, nullptr, &network);
        transfers.upload(QUrl::fromLocalFile(file.fileName()), "77", "photo");
        QTRY_VERIFY(transfers.canRetry()); QCOMPARE(saves, 0);
        QVERIFY(transfers.property("error").toString().contains("JPEG"));
        transfers.retry(); QTRY_COMPARE(transfers.attachments("77"), QStringList{"photo42_8"});
        QCOMPARE(saves, 1); QVERIFY(network.body.toLower().contains("content-type: image/png"));
        QVERIFY(network.body.contains("name=\"photo\"")); QVERIFY(network.body.contains(QString("Фото.png").toUtf8()));
    }

    void failedDownloadRetryReusesDestination()
    {
        QTemporaryDir directory;
        TransferNetwork network; network.failures = {QNetworkReply::RemoteHostClosedError}; network.response = "downloaded-file";
        Transfers transfers([](const QString &, Form, Transfers::Result) {}, nullptr, &network);
        const auto path = directory.filePath("download.bin");
        transfers.download(QUrl("https://pu.upload.test/file?hash=private"), QUrl::fromLocalFile(path));
        QTRY_VERIFY(transfers.canRetry()); QVERIFY(!QFile::exists(path));
        QSignalSpy done(&transfers, &Transfers::downloadFinished);
        transfers.retry(); QTRY_COMPARE(done.count(), 1);
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), network.response);
    }

    void httpFailureShowsStatusAndVideoIsNotDuplicatedByAutomaticRetry()
    {
        QTemporaryDir directory; QFile file(directory.filePath("clip.mp4"));
        QVERIFY(file.open(QIODevice::WriteOnly)); file.write("video payload"); file.close();
        TransferNetwork network; network.failures = {QNetworkReply::ContentAccessDenied}; network.statuses = {403};
        int requests = 0;
        Transfers transfers([&](const QString &method, Form, Transfers::Result result) {
            QCOMPARE(method, QString("video.save")); ++requests;
            result(QJsonObject{{"upload_url", "https://pu.vk.ru/upload.php?hash=private"}, {"owner_id", 42}, {"video_id", 9}}, {});
        }, nullptr, &network);
        transfers.upload(QUrl::fromLocalFile(file.fileName()), "77", "video");
        QTRY_VERIFY(transfers.canRetry()); QCOMPARE(requests, 1);
        QVERIFY(transfers.property("error").toString().contains("HTTP 403"));
        QVERIFY(!transfers.property("error").toString().contains("private"));
        network.failures = {QNetworkReply::RemoteHostClosedError}; network.statuses = {200};
        transfers.retry(); QTRY_VERIFY(transfers.canRetry()); QCOMPARE(requests, 2);
        QTest::qWait(450); QCOMPARE(requests, 2);
    }

    void pendingChangesDoNotFollowTransferProgress()
    {
        QTemporaryDir directory;
        TransferNetwork network; network.response = "download";
        Transfers transfers([](const QString &, Form, Transfers::Result) {}, nullptr, &network);
        QSignalSpy pending(&transfers, &Transfers::pendingChanged), changed(&transfers, &Transfers::changed);
        const QJsonObject state{{"pending", QJsonArray{
            QJsonObject{{"peer", "77"}, {"attachment", "doc42_1"}, {"name", "Первый файл"}},
            QJsonObject{{"peer", "88"}, {"attachment", "doc42_2"}, {"name", "Другой диалог"}}
        }}};
        transfers.restoreState(state); QCOMPARE(pending.count(), 1);
        changed.clear();
        QSignalSpy downloaded(&transfers, &Transfers::downloadFinished);
        transfers.download(QUrl("https://cdn.test/file"), QUrl::fromLocalFile(directory.filePath("file.bin")));
        QTRY_COMPARE(downloaded.count(), 1);
        QVERIFY(changed.count() >= 2); QCOMPARE(pending.count(), 1);
        transfers.remove(1); QCOMPARE(pending.count(), 2);
        QCOMPARE(transfers.attachments("77"), QStringList{"doc42_1"});
        transfers.acknowledge("77", {"doc42_1"}); QCOMPARE(pending.count(), 3);
        transfers.restoreState(state); QCOMPARE(pending.count(), 4);
        transfers.reset(); QCOMPARE(pending.count(), 5);
        transfers.reset(); transfers.remove(0); QCOMPARE(pending.count(), 5);
    }

    void recordedVoiceMovesToDurableQueueWithoutLeakingOriginal()
    {
        QTemporaryDir directory; const auto path = directory.filePath("recording.ogg");
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("OggS-recorded-voice"); file.close();
        TransferNetwork network;
        Transfers transfers([](const QString &, Form, Transfers::Result) {}, nullptr, &network);
        transfers.setWorkspace(directory.filePath("uploads")); transfers.pause();
        QVERIFY(transfers.adoptRecording(QUrl::fromLocalFile(path), "77"));
        QVERIFY(!QFile::exists(path));
        const auto job = transfers.savedState().value("jobs").toArray().first().toObject();
        QVERIFY(job.value("managed").toBool()); QCOMPARE(job.value("kind").toString(), "audio_message");
        QFile copy(QUrl(job.value("file").toString()).toLocalFile()); QVERIFY(copy.open(QIODevice::ReadOnly)); QCOMPARE(copy.readAll(), "OggS-recorded-voice");
        QVERIFY(!transfers.adoptRecording(QUrl::fromLocalFile(path), "77")); // failure keeps the queued recording
        QCOMPARE(transfers.savedState().value("jobs").toArray().size(), 1);
    }
    void lateApiCallbackAfterDestructionIsIgnored()
    {
        QTemporaryDir directory; QFile file(directory.filePath("file.txt"));
        QVERIFY(file.open(QIODevice::WriteOnly)); file.write("draft"); file.close();
        TransferNetwork network; Transfers::Result late;
        {
            Transfers transfers([&](const QString &, Form, Transfers::Result result) { late = result; }, nullptr, &network);
            transfers.upload(QUrl::fromLocalFile(file.fileName()), "77", "doc");
            QVERIFY(bool(late));
        }
        late(QJsonObject{{"upload_url", "https://upload.test/file"}}, {});
        QVERIFY(network.urls.isEmpty());
    }
    void stagedUploadSurvivesRestartWithoutOriginalFile()
    {
        QTemporaryDir directory;
        const auto sourcePath = directory.filePath("draft.txt");
        QFile file(sourcePath); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("durable draft file"); file.close();
        const auto workspace = directory.filePath("uploads");
        QJsonObject saved;
        QString staged;
        TransferNetwork network;
        {
            bool requested = false;
            Transfers transfers([&](const QString &, Form, Transfers::Result) { requested = true; }, nullptr, &network);
            transfers.setWorkspace(workspace);
            transfers.upload(QUrl::fromLocalFile(sourcePath), "77", "doc");
            QTRY_VERIFY(requested);
            saved = transfers.savedState();
            const auto jobs = saved.value("jobs").toArray();
            QCOMPARE(jobs.size(), 1);
            staged = QUrl(jobs.first().toObject().value("file").toString()).toLocalFile();
            QVERIFY(staged != sourcePath); QVERIFY(QFile::exists(staged));
        }
        QVERIFY(QFile::remove(sourcePath));
        Transfers transfers([&](const QString &method, Form, Transfers::Result result) {
            if (method == "docs.getMessagesUploadServer") result(QJsonObject{{"upload_url", "https://upload.test/file"}}, {});
            else result(QJsonObject{{"type", "doc"}, {"doc", QJsonObject{{"id", 9}, {"owner_id", 42}}}}, {});
        }, nullptr, &network);
        transfers.setWorkspace(workspace);
        transfers.restoreState(saved);
        QVERIFY(!transfers.busy());
        transfers.resume();
        QTRY_COMPARE(transfers.attachments("77").size(), 1);
        QVERIFY(network.body.contains("durable draft file"));
        QVERIFY(network.body.contains("draft.txt"));
        transfers.acknowledge("77", transfers.attachments("77"));
        QVERIFY(!QFile::exists(staged));
    }
    void voiceUploadUsesDocumentAttachmentAndKeepsPeerIsolation()
    {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto path = directory.filePath("voice.ogg");
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("voice-payload"); file.close();
        TransferNetwork network;
        QStringList calls;
        Transfers transfers([&](const QString &method, Form params, Transfers::Result callback) {
            calls << method;
            if (method == "docs.getMessagesUploadServer") {
                QVERIFY(params.contains(qMakePair(QString("type"), QString("audio_message"))));
                callback(QJsonObject{{"upload_url", "https://upload.test/file"}}, {});
            } else {
                QCOMPARE(method, QString("docs.save"));
                QVERIFY(params.contains(qMakePair(QString("file"), QString("uploaded-file"))));
                callback(QJsonObject{{"type", "audio_message"}, {"audio_message", QJsonObject{{"id", 9}, {"owner_id", 42}, {"access_key", "key"}}}}, {});
            }
        }, nullptr, &network);
        transfers.upload(QUrl::fromLocalFile(path), "77", "audio_message");
        QTRY_VERIFY(!transfers.busy());
        QCOMPARE(calls.size(), 2);
        QVERIFY(network.body.contains("voice-payload"));
        QVERIFY(network.body.contains("voice.ogg"));
        QCOMPARE(transfers.attachments("77"), QStringList{"doc42_9_key"});
        QVERIFY(transfers.attachments("88").isEmpty());
        transfers.acknowledge("88", {"doc42_9_key"});
        QCOMPARE(transfers.attachments("77").size(), 1);
        transfers.acknowledge("77", {"doc42_9_key"});
        QVERIFY(transfers.attachments("77").isEmpty());
    }
    void cancelledUploadIgnoresLateServerResponse()
    {
        QTemporaryDir directory; const auto path = directory.filePath("doc.txt");
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("file"); file.close();
        TransferNetwork network; Transfers::Result complete;
        Transfers transfers([&](const QString &, Form, Transfers::Result callback) { complete = callback; }, nullptr, &network);
        transfers.upload(QUrl::fromLocalFile(path), "77", "doc");
        QVERIFY(transfers.busy()); transfers.cancel();
        complete(QJsonObject{{"upload_url", "https://upload.test/file"}}, {});
        QVERIFY(network.urls.isEmpty()); QVERIFY(!transfers.busy());
    }
    void downloadWritesCompleteFile()
    {
        QTemporaryDir directory;
        TransferNetwork network; network.response = QByteArray("binary\0payload", 14);
        Transfers transfers([](const QString &, Form, Transfers::Result) {}, nullptr, &network);
        QSignalSpy finished(&transfers, &Transfers::downloadFinished);
        const auto path = directory.filePath("download.bin");
        transfers.download(QUrl("https://cdn.test/file"), QUrl::fromLocalFile(path));
        QTRY_COMPARE(finished.count(), 1);
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), network.response);
    }
};
QTEST_GUILESS_MAIN(TransfersTest)
#include "TransfersTest.moc"
