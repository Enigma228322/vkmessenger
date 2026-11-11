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
};
QTEST_GUILESS_MAIN(LinuxKeychainTest)
#include "LinuxKeychainTest.moc"
