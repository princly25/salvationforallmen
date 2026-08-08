#include "core/ProfileInstance.hpp"
#include "network/KillSwitchEngine.hpp"
#include "network/NetworkMonitor.hpp"

#include <QByteArray>
#include <QCoreApplication>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>

namespace {
ProfileConfig validConfig(const QString& id, quint16 proxyPort)
{
    ProfileConfig config;
    config.id = id;
    config.name = QStringLiteral("Network Test");
    config.userAgent = QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64)");
    config.hardware.masterSeedHex = std::string(64, 'f');
    config.proxy = QNetworkProxy(QNetworkProxy::HttpProxy, QStringLiteral("127.0.0.1"), proxyPort);
    return config;
}
}

class Module4Test final : public QObject {
    Q_OBJECT

private slots:
    void monitorDetectsProxyLossAndRecovery();
    void killSwitchFreezesAndVerifiesProxyExit();
    void restorationFailsClosedOnMismatch();
};

void Module4Test::monitorDetectsProxyLossAndRecovery()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    const quint16 port = server.serverPort();
    NetworkMonitor monitor(QNetworkProxy(QNetworkProxy::HttpProxy,
                                         QStringLiteral("127.0.0.1"), port));
    QSignalSpy emergencySpy(&monitor, &NetworkMonitor::networkEmergencyTriggered);
    QSignalSpy restoredSpy(&monitor, &NetworkMonitor::networkRestored);

    monitor.startMonitoring(10);
    QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 2000);
    QCOMPARE(monitor.status(), NetworkStatus::Healthy);
    server.close();
    QTRY_VERIFY_WITH_TIMEOUT(emergencySpy.count() > 0, 3000);
    QCOMPARE(emergencySpy.at(0).at(0).value<NetworkStatus>(), NetworkStatus::ProxyDegraded);

    QVERIFY(server.listen(QHostAddress::LocalHost, port));
    QTRY_VERIFY_WITH_TIMEOUT(restoredSpy.count() > 0, 3000);
    QCOMPARE(monitor.status(), NetworkStatus::Healthy);
    monitor.stopMonitoring();
    QVERIFY(!monitor.isMonitoring());
}

void Module4Test::killSwitchFreezesAndVerifiesProxyExit()
{
    QTcpServer proxy;
    QVERIFY(proxy.listen(QHostAddress::LocalHost));
    QTemporaryDir temporaryRoot;
    ProfileInstance profile(validConfig(QStringLiteral("kill-switch"), proxy.serverPort()),
                            temporaryRoot.path());
    KillSwitchEngine engine(&profile);
    engine.setVerificationEndpoint(QUrl(QStringLiteral("http://probe.invalid/ip")),
                                   QStringLiteral("203.0.113.7"));
    QSignalSpy successSpy(&engine, &KillSwitchEngine::restorationSucceeded);
    QSignalSpy rejectSpy(&engine, &KillSwitchEngine::restorationRejected);

    connect(&proxy, &QTcpServer::newConnection, &proxy, [&proxy] {
        QTcpSocket* socket = proxy.nextPendingConnection();
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket] {
            socket->readAll();
            QObject::connect(socket, &QTcpSocket::bytesWritten, socket,
                             [socket](qint64) { socket->disconnectFromHost(); },
                             Qt::SingleShotConnection);
            socket->write("HTTP/1.1 200 OK\r\nContent-Length: 11\r\nConnection: close\r\n\r\n203.0.113.7");
        });
    });

    engine.handleEmergency(NetworkStatus::ProxyDegraded);
    QCOMPARE(profile.state(), ProfileInstance::State::Frozen);
    QVERIFY(engine.isContained());
    engine.handleRestoration();
    QTRY_VERIFY_WITH_TIMEOUT(successSpy.count() + rejectSpy.count() == 1, 3000);
    QCOMPARE(successSpy.count(), 1);
    QCOMPARE(rejectSpy.count(), 0);
    QCOMPARE(profile.state(), ProfileInstance::State::Ready);
    QVERIFY(!engine.isContained());
}

void Module4Test::restorationFailsClosedOnMismatch()
{
    QTcpServer proxy;
    QVERIFY(proxy.listen(QHostAddress::LocalHost));
    QTemporaryDir temporaryRoot;
    ProfileInstance profile(validConfig(QStringLiteral("kill-switch-mismatch"), proxy.serverPort()),
                            temporaryRoot.path());
    KillSwitchEngine engine(&profile);
    engine.setVerificationEndpoint(QUrl(QStringLiteral("http://probe.invalid/ip")),
                                   QStringLiteral("203.0.113.8"));
    QSignalSpy rejectSpy(&engine, &KillSwitchEngine::restorationRejected);
    connect(&proxy, &QTcpServer::newConnection, &proxy, [&proxy] {
        QTcpSocket* socket = proxy.nextPendingConnection();
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket] {
            socket->readAll();
            QObject::connect(socket, &QTcpSocket::bytesWritten, socket,
                             [socket](qint64) { socket->disconnectFromHost(); },
                             Qt::SingleShotConnection);
            socket->write("HTTP/1.1 200 OK\r\nContent-Length: 11\r\nConnection: close\r\n\r\n203.0.113.7");
        });
    });

    engine.handleEmergency(NetworkStatus::LeakedRisk);
    engine.handleRestoration();
    QTRY_VERIFY_WITH_TIMEOUT(rejectSpy.count() == 1, 3000);
    QCOMPARE(profile.state(), ProfileInstance::State::Frozen);
    QVERIFY(engine.isContained());
}

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    Module4Test test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_module4.moc"
