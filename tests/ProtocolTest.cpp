#include "OAuth.h"
#include "Form.h"
#include <QSignalSpy>
#include <QtTest>

class ProtocolTest : public QObject {
    Q_OBJECT
private slots:
    void formPreservesMessageText()
    {
        const QString original = "Привет + C++ & Qt = 100% %2B %26 %20\n✨";
        auto encoded = formBody(Form{{"message", original}, {"token", "a+b/c="}});
        QVERIFY(!encoded.contains('+'));
        // Emulate an application/x-www-form-urlencoded parser.
        encoded.replace('+', ' ');
        const QUrlQuery decoded(QString::fromUtf8(encoded));
        QCOMPARE(decoded.queryItemValue("message", QUrl::FullyDecoded), original);
        QCOMPARE(decoded.queryItemValue("token", QUrl::FullyDecoded), QString("a+b/c="));
    }
    void pkceRfc7636Vector()
    {
        QCOMPARE(OAuth::challenge("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk"),
                 QString("E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM"));
    }
    void verifierFormat()
    {
        const auto first = OAuth::randomString();
        QCOMPARE(first.size(), 43);
        QVERIFY(QRegularExpression("^[A-Za-z0-9_-]{43}$").match(first).hasMatch());
        QVERIFY(first != OAuth::randomString());
    }
    void redirectMustMatch()
    {
        const QUrl expected("http://127.0.0.1:43821/callback");
        QVERIFY(OAuth::sameRedirect(QUrl("http://127.0.0.1:43821/callback?code=test"), expected));
        QVERIFY(!OAuth::sameRedirect(QUrl("http://127.0.0.1:43822/callback?code=test"), expected));
        QVERIFY(!OAuth::sameRedirect(QUrl("http://127.0.0.1:43821/other?code=test"), expected));
        QVERIFY(!OAuth::sameRedirect(QUrl("http://evil.example:43821/callback?code=test"), expected));
        QVERIFY(!OAuth::sameRedirect(QUrl("http://user@127.0.0.1:43821/callback"), expected));
        QVERIFY(!OAuth::sameRedirect(QUrl("http://127.0.0.1:43821/callback#code=test"), expected));
    }
    void registeredHttpsRedirectMustMatch()
    {
        const QUrl expected("https://153-51-114-47.sslip.io/callback");
        QVERIFY(OAuth::sameRedirect(QUrl("https://153-51-114-47.sslip.io/callback?code=test&device_id=test"), expected));
        QVERIFY(!OAuth::sameRedirect(QUrl("http://153-51-114-47.sslip.io/callback?code=test"), expected));
        QVERIFY(!OAuth::sameRedirect(QUrl("https://153-51-114-47.sslip.io.evil.test/?code=test"), expected));
        QVERIFY(!OAuth::sameRedirect(QUrl("https://153-51-114-47.sslip.io/other?code=test"), expected));
        QVERIFY(!OAuth::sameRedirect(QUrl("https://153-51-114-47.sslip.io/callback#code=test"), expected));
    }
    void unsolicitedCallbackRejected()
    {
        OAuth oauth;
        QSignalSpy failed(&oauth, &OAuth::failed);
        QSignalSpy success(&oauth, &OAuth::authenticated);
        oauth.complete("http://127.0.0.1:43821/callback?code=stolen&state=invalid&device_id=x");
        QCOMPARE(failed.count(), 1);
        QCOMPARE(success.count(), 0);
    }
    void insecureRedirectRejected()
    {
        OAuth oauth;
        QSignalSpy failed(&oauth, &OAuth::failed);
        oauth.start("123", "http://example.org/callback", "");
        QCOMPARE(failed.count(), 1);
    }
};

QTEST_GUILESS_MAIN(ProtocolTest)
#include "ProtocolTest.moc"
