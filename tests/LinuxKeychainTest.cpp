#include "Keychain.h"
#include <QtTest>

class LinuxKeychainTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        if (!qEnvironmentVariableIsSet("VKM_ISOLATED_SECRET_SERVICE"))
            QSKIP("Run inside an isolated D-Bus session with a temporary keyring.");
    }
    void roundTripAndReplaceBinarySession() {
        Keychain::clear();
        QVERIFY(Keychain::read().isEmpty());
        const auto first = QByteArray::fromHex("000102ff") + QString::fromUtf8("Тестовая сессия").toUtf8();
        QVERIFY(Keychain::write(first)); QCOMPARE(Keychain::read(), first);
        const QByteArray replacement("synthetic-session");
        QVERIFY(Keychain::write(replacement)); QCOMPARE(Keychain::read(), replacement);
        Keychain::clear(); QVERIFY(Keychain::read().isEmpty());
    }
    void polzaTokensAreScopedToVkAccounts() {
        const QByteArray first = "auto-reply/user:42/polza";
        const QByteArray second = "auto-reply/user:77/polza";
        QVERIFY(Keychain::write("vk-session-fixture"));
        QVERIFY(Keychain::write("first-fixture", first));
        QVERIFY(Keychain::write("second-fixture", second));
        QVERIFY(Keychain::write("replacement-fixture", first));
        QCOMPARE(Keychain::read(), "vk-session-fixture");
        QCOMPARE(Keychain::read(first), "replacement-fixture");
        QCOMPARE(Keychain::read(second), "second-fixture");
        Keychain::clear();
        QCOMPARE(Keychain::read(first), "replacement-fixture");
        Keychain::clear(first);
        QVERIFY(Keychain::read(first).isEmpty());
        QCOMPARE(Keychain::read(second), "second-fixture");
        Keychain::clear(second);
    }
};
QTEST_GUILESS_MAIN(LinuxKeychainTest)
#include "LinuxKeychainTest.moc"
