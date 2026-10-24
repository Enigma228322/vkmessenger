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
    TransferReply(const QNetworkRequest &request, QByteArray data, QObject *parent)
        : QNetworkReply(parent), m_data(std::move(data)) {
        setRequest(request); setUrl(request.url()); open(QIODevice::ReadOnly | QIODevice::Unbuffered);
        QTimer::singleShot(0, this, [this] { setAttribute(QNetworkRequest::HttpStatusCodeAttribute, 200); setFinished(true); emit readyRead(); emit finished(); });
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
protected:
    QNetworkReply *createRequest(Operation, const QNetworkRequest &request, QIODevice *outgoing) override {
        urls.append(request.url()); body = outgoing ? outgoing->readAll() : QByteArray();
        return new TransferReply(request, response, this);
    }
};

class TransfersTest : public QObject {
    Q_OBJECT
private slots:
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
