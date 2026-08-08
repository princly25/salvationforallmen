#include "network/ProxyFetcher.hpp"

#include <QCoreApplication>
#include <QHostAddress>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>

namespace {
class MockProviderApi final : public QObject {
public:
    explicit MockProviderApi(QByteArray responseBody, QObject* parent = nullptr)
        : QObject(parent)
        , m_responseBody(std::move(responseBody))
    {
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (m_server.hasPendingConnections()) {
                QTcpSocket* socket = m_server.nextPendingConnection();
                connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
                    const QByteArray request = socket->readAll();
                    m_request += request;
                    if (!m_request.contains("\r\n\r\n")) {
                        return;
                    }
                    const QByteArray response =
                        "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\n"
                        "Content-Length: "
                        + QByteArray::number(m_responseBody.size()) + "\r\n\r\n" + m_responseBody;
                    socket->write(response);
                    socket->disconnectFromHost();
                });
            }
        });
    }

    bool listen()
    {
        return m_server.listen(QHostAddress::LocalHost);
    }

    [[nodiscard]] QUrl url() const
    {
        return QUrl(QStringLiteral("http://127.0.0.1:%1/api/proxies").arg(m_server.serverPort()));
    }

    [[nodiscard]] QByteArray request() const
    {
        return m_request;
    }

private:
    QTcpServer m_server;
    QByteArray m_responseBody;
    QByteArray m_request;
};
}

class ProxyFetcherTest final : public QObject {
    Q_OBJECT

private slots:
    void parsesWebsharePayload();
    void parsesIpRoyalAndCustomFormats();
    void authenticatedFetchAndLatencyTest();
    void rejectsMalformedPayload();
};

void ProxyFetcherTest::parsesWebsharePayload()
{
    const QByteArray payload = R"JSON({
        "count": 2,
        "results": [
            {"id":"ws-1","proxy_address":"198.51.100.10","port":8000,
             "username":"alice","password":"secret","country_code":"US","city":"Austin"},
            {"id":"ws-2","proxy_address":"198.51.100.11","port":9000,
             "username":"bob","password":"secret","country_code":"DE","city":"Berlin",
             "protocol":"socks5"}
        ]
    })JSON";
    QString error;
    const QList<ProxyEndpoint> proxies = ProxyProviderManager::parseProxyPayload(
        payload, &error, ProxyProviderManager::ProviderFormat::Webshare);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(proxies.size(), 2);
    QCOMPARE(proxies.at(0).host, QStringLiteral("198.51.100.10"));
    QCOMPARE(proxies.at(0).port, quint16(8000));
    QCOMPARE(proxies.at(0).location(), QStringLiteral("Austin, US"));
    QCOMPARE(proxies.at(1).type, QNetworkProxy::Socks5Proxy);
}

void ProxyFetcherTest::parsesIpRoyalAndCustomFormats()
{
    const QByteArray ipRoyal = R"JSON({"data":{"proxies":[
        {"host":"isp.example.test","port":"1234","user":"royal","pass":"key","country":"GB"}
    ]}})JSON";
    const QList<ProxyEndpoint> royal = ProxyProviderManager::parseProxyPayload(
        ipRoyal, nullptr, ProxyProviderManager::ProviderFormat::IPRoyal);
    QCOMPARE(royal.size(), 1);
    QCOMPARE(royal.constFirst().username, QStringLiteral("royal"));
    QCOMPARE(royal.constFirst().password, QStringLiteral("key"));

    const QByteArray custom = R"JSON({"items":[
        "socks5://user:pass@203.0.113.7:1080",
        "203.0.113.8:8080:custom:token",
        "203.0.113.8:8080:custom:token"
    ]})JSON";
    const QList<ProxyEndpoint> parsed = ProxyProviderManager::parseProxyPayload(custom);
    QCOMPARE(parsed.size(), 2);
    QCOMPARE(parsed.at(0).type, QNetworkProxy::Socks5Proxy);
    QCOMPARE(parsed.at(1).username, QStringLiteral("custom"));
}

void ProxyFetcherTest::authenticatedFetchAndLatencyTest()
{
    QTcpServer latencyTarget;
    QVERIFY(latencyTarget.listen(QHostAddress::LocalHost));
    const QByteArray body = QStringLiteral(
        R"JSON({"proxies":[{"host":"127.0.0.1","port":%1,"country":"LOCAL"}]})JSON")
                                .arg(latencyTarget.serverPort())
                                .toUtf8();
    MockProviderApi api(body);
    QVERIFY(api.listen());

    ProxyProviderManager manager;
    QSignalSpy fetchedSpy(&manager, &ProxyProviderManager::proxiesFetched);
    QSignalSpy failedSpy(&manager, &ProxyProviderManager::fetchFailed);
    manager.fetchProxies(api.url(), QStringLiteral("integration-token"),
                         ProxyProviderManager::ProviderFormat::Custom);
    QTRY_VERIFY_WITH_TIMEOUT(fetchedSpy.count() + failedSpy.count() == 1, 3000);
    QCOMPARE(failedSpy.count(), 0);
    QVERIFY(api.request().contains("Authorization: Bearer integration-token"));
    QVERIFY(api.request().contains("X-API-Key: integration-token"));

    const QList<ProxyEndpoint> fetched =
        qvariant_cast<QList<ProxyEndpoint>>(fetchedSpy.constFirst().constFirst());
    QCOMPARE(fetched.size(), 1);
    QSignalSpy latencySpy(&manager, &ProxyProviderManager::latencyTestsFinished);
    manager.testLatencies(fetched, 500, 2);
    QTRY_COMPARE_WITH_TIMEOUT(latencySpy.count(), 1, 2000);
    const QList<ProxyEndpoint> tested =
        qvariant_cast<QList<ProxyEndpoint>>(latencySpy.constFirst().constFirst());
    QCOMPARE(tested.size(), 1);
    QVERIFY(tested.constFirst().reachable);
    QVERIFY(tested.constFirst().latencyMs >= 0);
    QCOMPARE(manager.activeProxyPool().size(), 1);
    QCOMPARE(manager.activeProxyPool().constFirst().host, QStringLiteral("127.0.0.1"));
}

void ProxyFetcherTest::rejectsMalformedPayload()
{
    QString error;
    const QList<ProxyEndpoint> proxies =
        ProxyProviderManager::parseProxyPayload("{not-json", &error);
    QVERIFY(proxies.isEmpty());
    QVERIFY(error.contains(QStringLiteral("Invalid provider JSON")));
}

QTEST_GUILESS_MAIN(ProxyFetcherTest)
#include "test_proxy_fetcher.moc"
