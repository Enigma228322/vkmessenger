#include "Messenger.h"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQmlContext>
#include <QFileOpenEvent>
#include <QTemporaryDir>
#include "media/OggOpusWriter.h"
#include <QSqlDatabase>
#include <QJsonDocument>
#include <QQuickWindow>
#include <QTextStream>
#include <QTimer>
#include <QImageReader>
#include <QStandardPaths>
#include <QSettings>
#include <QDir>

class DeepLinkHandler : public QObject {
public:
    explicit DeepLinkHandler(Messenger &messenger) : m_messenger(messenger) {}
protected:
    bool eventFilter(QObject *, QEvent *event) override {
        if (event->type() != QEvent::FileOpen) return false;
        return m_messenger.openDeepLink(static_cast<QFileOpenEvent *>(event)->url());
    }
private:
    Messenger &m_messenger;
};

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setOrganizationName("VK");
    app.setOrganizationDomain("reforged.dev");
    app.setApplicationName("VK");
    app.setApplicationDisplayName("VK — VK Мессенджер");
    QQuickStyle::setStyle("Basic");
    const bool selfCheck = app.arguments().contains("--self-check");
    // Keep existing profiles discoverable under the stable organization domain.
    // The domain and Keychain service are persistent storage identifiers.
    if (!selfCheck) {
        QString legacyName = app.organizationDomain().section('.', 0, 0);
        if (!legacyName.isEmpty()) legacyName[0] = legacyName[0].toUpper();
        const auto organization = app.organizationName(); const auto application = app.applicationName();
        app.setOrganizationName(legacyName); app.setApplicationName(legacyName);
        const auto previousStorage = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/storage";
        const auto previousMedia = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/media";
        QVariantMap previousSettings;
        { QSettings settings; for (const auto &key : settings.allKeys()) previousSettings.insert(key, settings.value(key)); }
        app.setOrganizationName(organization); app.setApplicationName(application);
        QSettings settings;
        if (settings.allKeys().isEmpty()) for (auto it = previousSettings.begin(); it != previousSettings.end(); ++it) settings.setValue(it.key(), it.value());
        const auto storage = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/storage";
        if (!QDir(storage).exists() && QDir(previousStorage).exists()) app.setProperty("vkmStoragePath", previousStorage);
        const auto media = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/media";
        app.setProperty("vkmMediaPath", !QDir(media).exists() && QDir(previousMedia).exists() ? previousMedia : media);
    }
    QTemporaryDir isolatedStorage;
    Messenger messenger(nullptr, nullptr, selfCheck ? isolatedStorage.path() : QString(), !selfCheck);
    DeepLinkHandler deepLinks(messenger);
    app.installEventFilter(&deepLinks);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("messenger", &messenger);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &app, [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("VK", "Main");
    bool tokenEntryAvailable = false;
    if (selfCheck && !engine.rootObjects().isEmpty()) {
        auto field = engine.rootObjects().first()->findChild<QObject *>("accessTokenInput");
        tokenEntryAvailable = field && field->property("visible").toBool() && field->property("echoMode").toInt() == 2;
    }
    if (app.arguments().contains("--demo") || selfCheck) messenger.startDemo();
    if (selfCheck) QTimer::singleShot(500, &app, [&] {
        const bool webp = !QImage::fromData(QByteArray::fromBase64("UklGRiIAAABXRUJQVlA4IBYAAAAwAQCdASoBAAEADsD+JaQAA3AAAAAA"), "webp").isNull();
        const bool brand = app.applicationName() == "VK" && !engine.rootObjects().isEmpty() && engine.rootObjects().first()->property("title").toString() == "VK";
        QJsonObject result{{"vkIdConfigured", messenger.loginConfigured()}, {"vkAppId", messenger.property("clientId").toString()}, {"vkRedirectUri", messenger.property("redirectUri").toString()}, {"brand", brand}, {"webp", webp}, {"tokenEntry", tokenEntryAvailable}, {"apiInspector", messenger.apiInspectorEnabled()}, {"qml", !engine.rootObjects().isEmpty()}, {"sqlite", QSqlDatabase::isDriverAvailable("QSQLITE")}, {"gif", QImageReader::supportedImageFormats().contains("gif")},
            {"voiceOggOpus", OggOpusWriter::selfCheck()}, {"demoMessages", messenger.messages()->rowCount()}};
        for (const auto &arg : app.arguments()) if (arg.startsWith("--screenshot=")) {
            auto window = engine.rootObjects().isEmpty() ? nullptr : qobject_cast<QQuickWindow *>(engine.rootObjects().first());
            result.insert("screenshot", window && window->grabWindow().save(arg.mid(13)));
        }
        QTextStream(stdout) << QJsonDocument(result).toJson(QJsonDocument::Compact) << Qt::endl;
        app.exit(brand && webp && tokenEntryAvailable && result.value("qml").toBool() && result.value("sqlite").toBool() && result.value("gif").toBool() && result.value("voiceOggOpus").toBool() ? 0 : 2);
    });
    return app.exec();
}
