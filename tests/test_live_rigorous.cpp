#include "core/CustomUrlInterceptor.hpp"
#include "core/ProfileInstance.hpp"
#include "crypto/ProfileSeedEngine.hpp"
#include "hooks/FingerprintEngine.hpp"
#include "network/KillSwitchEngine.hpp"

#include <QApplication>
#include <QElapsedTimer>
#include <QHash>
#include <QHostAddress>
#include <QJSEngine>
#include <QNetworkProxy>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>
#include <QVariantList>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineSettings>
#include <QWebEngineView>

#include <array>
#include <cstdint>

namespace {
struct HttpRequestRecord {
    QByteArray target;
    QHash<QByteArray, QByteArray> headers;
};

class MockHttpProxy final : public QObject {
public:
    explicit MockHttpProxy(bool holdResponses = false, QObject* parent = nullptr)
        : QObject(parent)
        , m_holdResponses(holdResponses)
    {
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (m_server.hasPendingConnections()) {
                QTcpSocket* socket = m_server.nextPendingConnection();
                m_sockets.insert(socket);
                connect(socket, &QTcpSocket::readyRead, this,
                        [this, socket] { consume(socket); });
                connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
                    m_buffers.remove(socket);
                    m_sockets.remove(socket);
                    socket->deleteLater();
                });
            }
        });
    }

    bool listen()
    {
        return m_server.listen(QHostAddress::LocalHost);
    }

    [[nodiscard]] quint16 port() const noexcept
    {
        return m_server.serverPort();
    }

    [[nodiscard]] int requestCount() const noexcept
    {
        return m_requests.size();
    }

    [[nodiscard]] const QList<HttpRequestRecord>& requests() const noexcept
    {
        return m_requests;
    }

    void stopAbruptly()
    {
        m_server.close();
        const auto sockets = m_sockets;
        for (QTcpSocket* socket : sockets) {
            socket->abort();
        }
    }

private:
    void consume(QTcpSocket* socket)
    {
        QByteArray& buffer = m_buffers[socket];
        buffer += socket->readAll();
        const qsizetype headerEnd = buffer.indexOf("\r\n\r\n");
        if (headerEnd < 0 || socket->property("requestHandled").toBool()) {
            return;
        }

        const QList<QByteArray> lines = buffer.left(headerEnd).split('\n');
        if (lines.isEmpty()) {
            return;
        }
        const QList<QByteArray> requestLine = lines.constFirst().trimmed().split(' ');
        HttpRequestRecord record;
        if (requestLine.size() >= 2) {
            record.target = requestLine.at(1);
        }
        for (qsizetype index = 1; index < lines.size(); ++index) {
            const QByteArray line = lines.at(index).trimmed();
            const qsizetype separator = line.indexOf(':');
            if (separator > 0) {
                record.headers.insert(line.left(separator).trimmed().toLower(),
                                      line.mid(separator + 1).trimmed());
            }
        }
        m_requests.append(record);
        socket->setProperty("requestHandled", true);

        if (m_holdResponses) {
            return;
        }
        const QByteArray body = R"JSON({"ip":"203.0.113.55","via":"mock-http-proxy"})JSON";
        const QByteArray response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
            "Cache-Control: no-store\r\nConnection: close\r\nContent-Length: "
            + QByteArray::number(body.size()) + "\r\n\r\n" + body;
        socket->write(response);
        socket->disconnectFromHost();
    }

    QTcpServer m_server;
    bool m_holdResponses{false};
    QSet<QTcpSocket*> m_sockets;
    QHash<QTcpSocket*, QByteArray> m_buffers;
    QList<HttpRequestRecord> m_requests;
};

class MockSocks5Proxy final : public QObject {
public:
    explicit MockSocks5Proxy(QObject* parent = nullptr)
        : QObject(parent)
    {
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (m_server.hasPendingConnections()) {
                QTcpSocket* socket = m_server.nextPendingConnection();
                m_clients.insert(socket, Client{});
                connect(socket, &QTcpSocket::readyRead, this,
                        [this, socket] { consume(socket); });
                connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
                    m_clients.remove(socket);
                    socket->deleteLater();
                });
            }
        });
    }

    bool listen()
    {
        return m_server.listen(QHostAddress::LocalHost);
    }

    [[nodiscard]] quint16 port() const noexcept
    {
        return m_server.serverPort();
    }

    [[nodiscard]] bool observedRemoteDnsFor(const QString& host) const
    {
        return m_domainRequests.contains(host, Qt::CaseInsensitive);
    }

private:
    enum class Stage { Greeting, ConnectRequest, HttpRequest, Complete };
    struct Client {
        Stage stage{Stage::Greeting};
        QByteArray buffer;
    };

    void consume(QTcpSocket* socket)
    {
        auto found = m_clients.find(socket);
        if (found == m_clients.end()) {
            return;
        }
        Client& client = found.value();
        client.buffer += socket->readAll();

        bool progressed = true;
        while (progressed) {
            progressed = false;
            if (client.stage == Stage::Greeting && client.buffer.size() >= 2) {
                const int methodCount = static_cast<unsigned char>(client.buffer.at(1));
                if (client.buffer.size() < 2 + methodCount) {
                    return;
                }
                client.buffer.remove(0, 2 + methodCount);
                socket->write(QByteArray::fromHex("0500"));
                client.stage = Stage::ConnectRequest;
                progressed = true;
            }

            if (client.stage == Stage::ConnectRequest && client.buffer.size() >= 4) {
                const unsigned char addressType =
                    static_cast<unsigned char>(client.buffer.at(3));
                int requestSize = 0;
                QString host;
                if (addressType == 0x03) {
                    if (client.buffer.size() < 5) {
                        return;
                    }
                    const int hostLength = static_cast<unsigned char>(client.buffer.at(4));
                    requestSize = 5 + hostLength + 2;
                    if (client.buffer.size() < requestSize) {
                        return;
                    }
                    host = QString::fromUtf8(client.buffer.mid(5, hostLength));
                    m_domainRequests.append(host);
                } else if (addressType == 0x01) {
                    requestSize = 4 + 4 + 2;
                } else if (addressType == 0x04) {
                    requestSize = 4 + 16 + 2;
                } else {
                    socket->abort();
                    return;
                }
                if (client.buffer.size() < requestSize) {
                    return;
                }
                Q_UNUSED(host);
                client.buffer.remove(0, requestSize);
                socket->write(QByteArray::fromHex("050000017f0000010000"));
                client.stage = Stage::HttpRequest;
                progressed = true;
            }

            if (client.stage == Stage::HttpRequest) {
                const qsizetype headerEnd = client.buffer.indexOf("\r\n\r\n");
                if (headerEnd < 0) {
                    return;
                }
                client.buffer.remove(0, headerEnd + 4);
                const QByteArray body =
                    R"JSON({"ip":"198.51.100.88","dns":"resolved-by-socks5"})JSON";
                const QByteArray response =
                    "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                    "Connection: close\r\nContent-Length: "
                    + QByteArray::number(body.size()) + "\r\n\r\n" + body;
                socket->write(response);
                socket->disconnectFromHost();
                client.stage = Stage::Complete;
            }
        }
    }

    QTcpServer m_server;
    QHash<QTcpSocket*, Client> m_clients;
    QStringList m_domainRequests;
};

ProfileConfig liveConfig(const QString& id, const std::string& seed,
                         const QNetworkProxy& proxy = QNetworkProxy::NoProxy)
{
    ProfileConfig config;
    config.id = id;
    config.name = QStringLiteral("Rigorous Live Test");
    config.userAgent = QStringLiteral("AntiDetectBrowser-Rigorous/1.0");
    config.languages = {QStringLiteral("en-US"), QStringLiteral("en")};
    config.hardware.masterSeedHex = seed;
    config.proxy = proxy;
    return config;
}

QVariantList evaluateNoiseMath(const ProfileConfig& config)
{
    QJSEngine engine;
    const QString prelude = QStringLiteral(R"JS(
var navigator = {};
var screen = {};
var globalThis = this;
function WebGLRenderingContext() {}
WebGLRenderingContext.prototype.getParameter = function(parameter) { return 4096; };
function CanvasRenderingContext2D() { this.samples = [64, 0, 0, 255]; }
CanvasRenderingContext2D.prototype.getImageData = function() { return { data: this.samples.slice() }; };
CanvasRenderingContext2D.prototype.putImageData = function() {};
function AudioBuffer() { this.samples = [440.0]; }
AudioBuffer.prototype.getChannelData = function() { return this.samples; };
var document = { fonts: { check: function() { return true; } } };
)JS");
    QJSValue result = engine.evaluate(prelude);
    if (result.isError()) {
        return {};
    }
    const ProfileSeedEngine seedEngine(config.hardware.masterSeedHex);
    result = engine.evaluate(FingerprintEngine::generateInjectionScript(config, seedEngine));
    if (result.isError()) {
        return {};
    }
    return engine.evaluate(QStringLiteral(R"JS([
  (new CanvasRenderingContext2D()).getImageData().data[0],
  (new WebGLRenderingContext()).getParameter(0x0D33),
  (new AudioBuffer()).getChannelData(0)[0]
])JS"))
        .toVariant()
        .toList();
}

bool waitForLoad(QWebEnginePage* page, const QUrl& url, int timeoutMs = 8000)
{
    QSignalSpy loadSpy(page, &QWebEnginePage::loadFinished);
    page->setUrl(url);
    if (!loadSpy.wait(timeoutMs) || loadSpy.isEmpty()) {
        return false;
    }
    return loadSpy.constLast().constFirst().toBool();
}

QString pageText(QWebEnginePage* page)
{
    QString text;
    bool complete = false;
    page->toPlainText([&](const QString& value) {
        text = value;
        complete = true;
    });
    QElapsedTimer timer;
    timer.start();
    while (!complete && timer.elapsed() < 3000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    return text;
}
}

class LiveRigorousTest final : public QObject {
    Q_OBJECT

private slots:
    void cleanup();
    void deterministicPrngMathematicalConsistency();
    void simulatedRealHttpProxyHandshake();
    void dynamicKillSwitchFailsClosedWithoutNativeFallback();
    void webRtcAndSocks5DnsLeakAudit();
};

void LiveRigorousTest::cleanup()
{
    QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);
}

void LiveRigorousTest::deterministicPrngMathematicalConsistency()
{
    QTemporaryDir firstRoot;
    QTemporaryDir secondRoot;
    QTemporaryDir changedRoot;
    QVERIFY(firstRoot.isValid());
    QVERIFY(secondRoot.isValid());
    QVERIFY(changedRoot.isValid());

    const ProfileConfig firstConfig = liveConfig(QStringLiteral("same-seed-a"), std::string(64, '1'));
    const ProfileConfig secondConfig = liveConfig(QStringLiteral("same-seed-b"), std::string(64, '1'));
    const ProfileConfig changedConfig = liveConfig(QStringLiteral("changed-seed"), std::string(64, '2'));
    ProfileInstance firstProfile(firstConfig, firstRoot.path());
    ProfileInstance secondProfile(secondConfig, secondRoot.path());
    ProfileInstance changedProfile(changedConfig, changedRoot.path());

    const ProfileSeedEngine firstSeed(firstProfile.config().hardware.masterSeedHex);
    const ProfileSeedEngine secondSeed(secondProfile.config().hardware.masterSeedHex);
    const ProfileSeedEngine changedSeed(changedProfile.config().hardware.masterSeedHex);
    const FingerprintNoiseParameters first = FingerprintEngine::deriveNoiseParameters(firstSeed);
    const FingerprintNoiseParameters second = FingerprintEngine::deriveNoiseParameters(secondSeed);
    const FingerprintNoiseParameters changed = FingerprintEngine::deriveNoiseParameters(changedSeed);

    QCOMPARE(first, second);
    QCOMPARE(evaluateNoiseMath(firstConfig), evaluateNoiseMath(secondConfig));
    const QVariantList firstMath = evaluateNoiseMath(firstConfig);
    const QVariantList changedMath = evaluateNoiseMath(changedConfig);
    QCOMPARE(firstMath.size(), 3);
    QCOMPARE(changedMath.size(), 3);
    for (qsizetype index = 0; index < firstMath.size(); ++index) {
        QVERIFY2(firstMath.at(index) != changedMath.at(index),
                 "Changing the master seed must alter every injected noise surface");
    }

    const std::array<std::uint32_t, 3> firstSeeds{
        first.canvasSeed, first.webglSeed, first.audioSeed};
    const std::array<std::uint32_t, 3> changedSeeds{
        changed.canvasSeed, changed.webglSeed, changed.audioSeed};
    for (const std::uint32_t firstValue : firstSeeds) {
        for (const std::uint32_t changedValue : changedSeeds) {
            QVERIFY(firstValue != changedValue);
        }
    }
}

void LiveRigorousTest::simulatedRealHttpProxyHandshake()
{
    MockHttpProxy proxy;
    QVERIFY(proxy.listen());
    const QNetworkProxy proxyConfig(QNetworkProxy::HttpProxy,
                                    QStringLiteral("127.0.0.1"), proxy.port());
    QNetworkProxy::setApplicationProxy(proxyConfig);

    QTemporaryDir storageRoot;
    ProfileConfig config = liveConfig(QStringLiteral("http-proxy-live"),
                                      std::string(64, '3'), proxyConfig);
    config.expectedProxyIp = QStringLiteral("203.0.113.55");
    config.proxyVerificationUrl = QUrl(QStringLiteral("http://rigorous-probe.invalid/ip"));
    ProfileInstance profile(config, storageRoot.path());
    profile.launch();

    QVERIFY(profile.webEngineProfile() != nullptr);
    QCOMPARE(profile.webEngineProfile()->httpUserAgent(), config.userAgent);
    QCOMPARE(profile.webEngineProfile()->httpAcceptLanguage(), QStringLiteral("en-US,en"));
    QVERIFY(waitForLoad(profile.view()->page(), config.proxyVerificationUrl));
    QTRY_VERIFY_WITH_TIMEOUT(proxy.requestCount() > 0, 3000);

    bool observedConfiguredRequest = false;
    for (const HttpRequestRecord& request : proxy.requests()) {
        if (!request.target.contains("rigorous-probe.invalid/ip")) {
            continue;
        }
        observedConfiguredRequest = true;
        QCOMPARE(request.headers.value("user-agent"), config.userAgent.toUtf8());
        QVERIFY(request.headers.value("accept-language").contains("en-US"));
    }
    QVERIFY(observedConfiguredRequest);
    const QString response = pageText(profile.view()->page());
    QVERIFY(response.contains(config.expectedProxyIp));
    QVERIFY(response.contains(QStringLiteral("mock-http-proxy")));
}

void LiveRigorousTest::dynamicKillSwitchFailsClosedWithoutNativeFallback()
{
    MockHttpProxy proxy(true);
    QVERIFY(proxy.listen());
    const QNetworkProxy proxyConfig(QNetworkProxy::HttpProxy,
                                    QStringLiteral("127.0.0.1"), proxy.port());
    QNetworkProxy::setApplicationProxy(proxyConfig);

    QTemporaryDir storageRoot;
    ProfileConfig config = liveConfig(QStringLiteral("fail-closed-live"),
                                      std::string(64, '4'), proxyConfig);
    ProfileInstance profile(config, storageRoot.path());
    profile.launch();
    KillSwitchEngine killSwitch(&profile);
    QSignalSpy containmentSpy(&killSwitch, &KillSwitchEngine::containmentChanged);
    killSwitch.startMonitoring(10);

    profile.view()->setUrl(QUrl(QStringLiteral("http://midflight.invalid/slow")));
    QTRY_VERIFY_WITH_TIMEOUT(proxy.requestCount() > 0, 5000);
    QElapsedTimer failClosedTimer;
    failClosedTimer.start();
    proxy.stopAbruptly();
    QTRY_VERIFY_WITH_TIMEOUT(!containmentSpy.isEmpty(), 1000);
    QVERIFY2(failClosedTimer.elapsed() < 500,
             "The network kill switch did not engage within the fail-closed deadline");
    QCOMPARE(containmentSpy.constFirst().at(0).toBool(), true);
    QCOMPARE(containmentSpy.constFirst().at(1).value<NetworkStatus>(),
             NetworkStatus::ProxyDegraded);
    QCOMPARE(profile.state(), ProfileInstance::State::Frozen);
    QVERIFY(killSwitch.isContained());
    QVERIFY(profile.urlInterceptor()->isNetworkFrozen());

    QTcpServer nativeEndpoint;
    QVERIFY(nativeEndpoint.listen(QHostAddress::LocalHost));
    QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);
    const std::uint64_t blockedBefore = profile.urlInterceptor()->blockedRequestCount();
    profile.view()->setUrl(QUrl(QStringLiteral("http://127.0.0.1:%1/native-ip")
                                    .arg(nativeEndpoint.serverPort())));
    QTRY_VERIFY_WITH_TIMEOUT(profile.urlInterceptor()->blockedRequestCount() > blockedBefore, 2000);
    QTest::qWait(250);
    QVERIFY(!nativeEndpoint.hasPendingConnections());
    killSwitch.stopMonitoring();
}

void LiveRigorousTest::webRtcAndSocks5DnsLeakAudit()
{
    MockSocks5Proxy proxy;
    QVERIFY(proxy.listen());
    const QNetworkProxy proxyConfig(QNetworkProxy::Socks5Proxy,
                                    QStringLiteral("127.0.0.1"), proxy.port());
    QVERIFY(proxyConfig.capabilities().testFlag(QNetworkProxy::HostNameLookupCapability));
    QNetworkProxy::setApplicationProxy(proxyConfig);

    QTemporaryDir storageRoot;
    ProfileConfig config = liveConfig(QStringLiteral("socks-dns-live"),
                                      std::string(64, '5'), proxyConfig);
    config.expectedProxyIp = QStringLiteral("198.51.100.88");
    ProfileInstance profile(config, storageRoot.path());
    profile.launch();

    QWebEngineSettings* settings = profile.webEngineProfile()->settings();
    QVERIFY(settings->testAttribute(QWebEngineSettings::WebRTCPublicInterfacesOnly));
    QVERIFY(!settings->testAttribute(QWebEngineSettings::DnsPrefetchEnabled));

    const QUrl dnsAuditUrl(QStringLiteral("http://dns-audit.invalid/ip"));
    QVERIFY(waitForLoad(profile.view()->page(), dnsAuditUrl));
    QTRY_VERIFY_WITH_TIMEOUT(proxy.observedRemoteDnsFor(QStringLiteral("dns-audit.invalid")), 3000);
    const QString response = pageText(profile.view()->page());
    QVERIFY(response.contains(config.expectedProxyIp));
    QVERIFY(response.contains(QStringLiteral("resolved-by-socks5")));
}

int main(int argc, char* argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("QTWEBENGINE_DISABLE_SANDBOX", "1");
    QApplication application(argc, argv);
    LiveRigorousTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_live_rigorous.moc"
