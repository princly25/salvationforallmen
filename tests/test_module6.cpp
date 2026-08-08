#include "core/ProfileInstance.hpp"
#include "network/KillSwitchEngine.hpp"
#include "ui/MainWindow.hpp"
#include "ui/ProfileCardWidget.hpp"

#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QHostAddress>
#include <QPushButton>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

namespace {
ProfileConfig validConfig(const QString& id, bool withProxy = false)
{
    ProfileConfig config;
    config.id = id;
    config.name = QStringLiteral("Dashboard Profile");
    config.userAgent = QStringLiteral(
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");
    config.hardware.masterSeedHex = std::string(64, 'a');
    if (withProxy) {
        config.proxy = QNetworkProxy(QNetworkProxy::HttpProxy,
                                     QStringLiteral("proxy.example.test"), 8080);
        config.expectedProxyIp = QStringLiteral("203.0.113.21");
        config.proxyVerificationUrl = QUrl(QStringLiteral("https://probe.example.test/ip"));
    }
    return config;
}
}

class Module6Test final : public QObject {
    Q_OBJECT

private slots:
    void dashboardBuildsRequiredSurfaces();
    void profileCreationDialogCreatesProfile();
    void profileCardControlsLifecycle();
    void proxyAndGeoControlsReflectProfile();
    void proxyProviderFetchPopulatesActivePool();
    void statusBarReflectsNetworkHealth();
};

void Module6Test::dashboardBuildsRequiredSurfaces()
{
    QTemporaryDir temporaryRoot;
    MainWindow window(temporaryRoot.path());

    auto* navigation = window.findChild<QListWidget*>(QStringLiteral("sidebarNavigation"));
    QVERIFY(navigation != nullptr);
    QCOMPARE(navigation->count(), 5);
    QCOMPARE(navigation->item(0)->text(), QStringLiteral("Profiles"));
    QCOMPARE(navigation->item(1)->text(), QStringLiteral("Proxy Pool"));
    QCOMPARE(navigation->item(2)->text(), QStringLiteral("Proxy Auto-Fetcher"));
    QCOMPARE(navigation->item(3)->text(), QStringLiteral("Diagnostics & Logs"));
    QCOMPARE(navigation->item(4)->text(), QStringLiteral("Settings"));
    QVERIFY(window.findChild<QPushButton*>(QStringLiteral("createProfile")) != nullptr);
    QVERIFY(window.findChild<QWidget*>(QStringLiteral("proxyAutoFetcherPage")) != nullptr);
    QVERIFY(window.findChild<QLineEdit*>(QStringLiteral("proxyProviderApiUrl")) != nullptr);
    QVERIFY(window.findChild<QLineEdit*>(QStringLiteral("proxyProviderToken")) != nullptr);
    QVERIFY(window.findChild<QPushButton*>(QStringLiteral("fetchProxies")) != nullptr);
    auto* fetchedTable = window.findChild<QTableWidget*>(QStringLiteral("fetchedProxyTable"));
    QVERIFY(fetchedTable != nullptr);
    QCOMPARE(fetchedTable->columnCount(), 6);
    QVERIFY(window.styleSheet().contains(QStringLiteral("#111827")));
    QVERIFY(window.styleSheet().contains(QStringLiteral("#1F2937")));
    QVERIFY(window.styleSheet().contains(QStringLiteral("#3B82F6")));
    auto* emptyState = window.findChild<QLabel*>(QStringLiteral("emptyProfiles"));
    QVERIFY(emptyState != nullptr);
    QVERIFY(!emptyState->isHidden());

    auto* secureStatus = window.findChild<QLabel*>(QStringLiteral("killSwitchStatus"));
    QVERIFY(secureStatus != nullptr);
    QCOMPARE(secureStatus->text(), QStringLiteral("SECURE"));
}

void Module6Test::profileCreationDialogCreatesProfile()
{
    QTemporaryDir temporaryRoot;
    MainWindow window(temporaryRoot.path());
    QTimer::singleShot(0, &window, [&window] {
        auto* dialog = window.findChild<QDialog*>(QStringLiteral("createProfileDialog"));
        QVERIFY(dialog != nullptr);
        auto* seed = dialog->findChild<QLineEdit*>(QStringLiteral("masterSeedInput"));
        const QString initialSeed = seed->text();
        dialog->findChild<QPushButton*>(QStringLiteral("macPreset"))->click();
        QVERIFY(dialog->findChild<QLineEdit*>(QStringLiteral("userAgentInput"))
                    ->text().contains(QStringLiteral("Macintosh")));
        QVERIFY(seed->text() != initialSeed);
        dialog->findChild<QLineEdit*>(QStringLiteral("profileIdInput"))
            ->setText(QStringLiteral("created-profile"));
        dialog->findChild<QLineEdit*>(QStringLiteral("profileNameInput"))
            ->setText(QStringLiteral("Created Profile"));
        auto* buttons = dialog->findChild<QDialogButtonBox*>(QStringLiteral("createProfileButtons"));
        buttons->button(QDialogButtonBox::Ok)->click();
    });

    window.findChild<QPushButton*>(QStringLiteral("createProfile"))->click();
    QCOMPARE(window.profileManager().profileCount(), 1);
    QVERIFY(window.profileCard(QStringLiteral("created-profile")) != nullptr);
}

void Module6Test::profileCardControlsLifecycle()
{
    QTemporaryDir temporaryRoot;
    MainWindow window(temporaryRoot.path());
    ProfileInstance& profile = window.addProfile(validConfig(QStringLiteral("card-profile")));
    ProfileCardWidget* card = window.profileCard(QStringLiteral("card-profile"));
    QVERIFY(card != nullptr);
    QCOMPARE(card->profile(), &profile);

    auto* emptyState = window.findChild<QLabel*>(QStringLiteral("emptyProfiles"));
    QVERIFY(emptyState->isHidden());
    auto* freezeButton = card->findChild<QPushButton*>(QStringLiteral("freezeProfile"));
    auto* terminateButton = card->findChild<QPushButton*>(QStringLiteral("terminateProfile"));
    auto* launchButton = card->findChild<QPushButton*>(QStringLiteral("launchProfile"));
    auto* syncButton = card->findChild<QPushButton*>(QStringLiteral("syncProxy"));
    auto* inspectButton = card->findChild<QPushButton*>(QStringLiteral("inspectCookies"));
    auto* exportButton = card->findChild<QPushButton*>(QStringLiteral("exportCookies"));
    auto* deleteButton = card->findChild<QPushButton*>(QStringLiteral("deleteProfile"));
    QVERIFY(freezeButton != nullptr);
    QVERIFY(terminateButton != nullptr);
    QVERIFY(launchButton != nullptr);
    QVERIFY(syncButton != nullptr);
    QVERIFY(inspectButton != nullptr);
    QVERIFY(exportButton != nullptr);
    QVERIFY(deleteButton != nullptr);
    QCOMPARE(launchButton->text(), QStringLiteral("Launch Profile"));
    QCOMPARE(syncButton->text(), QStringLiteral("Sync Proxy"));
    QCOMPARE(inspectButton->text(), QStringLiteral("Inspect Cookies"));
    QCOMPARE(exportButton->text(), QStringLiteral("Export Cookies"));
    QCOMPARE(deleteButton->text(), QStringLiteral("Delete"));

    freezeButton->click();
    QCOMPARE(profile.state(), ProfileInstance::State::Frozen);
    QCOMPARE(freezeButton->text(), QStringLiteral("Unfreeze"));
    freezeButton->click();
    QCOMPARE(profile.state(), ProfileInstance::State::Ready);

    terminateButton->click();
    QCOMPARE(profile.state(), ProfileInstance::State::Terminated);
    QVERIFY(!launchButton->isEnabled());
    QVERIFY(!freezeButton->isEnabled());
    QVERIFY(!terminateButton->isEnabled());
    deleteButton->click();
    QCOMPARE(window.profileManager().profileCount(), 0);
}

void Module6Test::proxyAndGeoControlsReflectProfile()
{
    QTemporaryDir temporaryRoot;
    MainWindow window(temporaryRoot.path());
    window.addProfile(validConfig(QStringLiteral("proxy-profile"), true));

    auto* table = window.findChild<QTableWidget*>(QStringLiteral("proxyTable"));
    QVERIFY(table != nullptr);
    QCOMPARE(table->rowCount(), 1);
    QCOMPARE(table->item(0, 1)->text(), QStringLiteral("proxy.example.test"));
    QCOMPARE(table->item(0, 4)->text(), QStringLiteral("203.0.113.21"));

    ProfileCardWidget* card = window.profileCard(QStringLiteral("proxy-profile"));
    card->findChild<QPushButton*>(QStringLiteral("syncProxy"))->click();
    auto* navigation = window.findChild<QListWidget*>(QStringLiteral("sidebarNavigation"));
    auto* ipAddress = window.findChild<QLineEdit*>(QStringLiteral("geoIpAddress"));
    QCOMPARE(navigation->currentRow(), 1);
    QCOMPARE(ipAddress->text(), QStringLiteral("203.0.113.21"));
    QVERIFY(window.killSwitch(QStringLiteral("proxy-profile")) != nullptr);

    auto* statusDot = card->findChild<QLabel*>(QStringLiteral("proxyStatusDot"));
    QVERIFY(statusDot != nullptr);
    card->setProxyStatus(true, 42, QStringLiteral("203.0.113.21 · US"));
    QVERIFY(card->findChild<QLabel*>(QStringLiteral("proxyStatus"))->text().contains("42"));
}

void Module6Test::proxyProviderFetchPopulatesActivePool()
{
    QTcpServer latencyTarget;
    QVERIFY(latencyTarget.listen(QHostAddress::LocalHost));
    QTcpServer providerApi;
    QVERIFY(providerApi.listen(QHostAddress::LocalHost));
    connect(&providerApi, &QTcpServer::newConnection, &providerApi,
            [&providerApi, port = latencyTarget.serverPort()] {
                QTcpSocket* socket = providerApi.nextPendingConnection();
                connect(socket, &QTcpSocket::readyRead, socket, [socket, port] {
                    socket->readAll();
                    const QByteArray body = QStringLiteral(
                        R"JSON({"proxies":[{"host":"127.0.0.1","port":%1,"country":"LOCAL"}]})JSON")
                                                .arg(port)
                                                .toUtf8();
                    socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                                  "Connection: close\r\nContent-Length: "
                                  + QByteArray::number(body.size()) + "\r\n\r\n" + body);
                    socket->disconnectFromHost();
                });
            });

    QTemporaryDir temporaryRoot;
    MainWindow window(temporaryRoot.path());
    window.findChild<QLineEdit*>(QStringLiteral("proxyProviderApiUrl"))
        ->setText(QStringLiteral("http://127.0.0.1:%1/proxies").arg(providerApi.serverPort()));
    window.findChild<QLineEdit*>(QStringLiteral("proxyProviderToken"))
        ->setText(QStringLiteral("ui-token"));
    window.findChild<QPushButton*>(QStringLiteral("fetchProxies"))->click();

    auto* fetched = window.findChild<QTableWidget*>(QStringLiteral("fetchedProxyTable"));
    auto* active = window.findChild<QTableWidget*>(QStringLiteral("proxyTable"));
    QTRY_COMPARE_WITH_TIMEOUT(fetched->rowCount(), 1, 3000);
    QCOMPARE(active->rowCount(), 1);
    QCOMPARE(active->item(0, 0)->text(), QStringLiteral("Available"));
    QVERIFY(window.findChild<QLabel*>(QStringLiteral("proxyFetcherStatus"))
                ->text().contains(QStringLiteral("Active pool updated")));
}

void Module6Test::statusBarReflectsNetworkHealth()
{
    QTemporaryDir temporaryRoot;
    MainWindow window(temporaryRoot.path());
    window.updateNetworkStatus(NetworkStatus::ProxyDegraded,
                               QStringLiteral("203.0.113.44"), 87);

    QCOMPARE(window.findChild<QLabel*>(QStringLiteral("latencyStatus"))->text(),
             QStringLiteral("Ping: 87 ms"));
    QCOMPARE(window.findChild<QLabel*>(QStringLiteral("activeProxyStatus"))->text(),
             QStringLiteral("Proxy IP: 203.0.113.44"));
    auto* status = window.findChild<QLabel*>(QStringLiteral("killSwitchStatus"));
    QCOMPARE(status->text(), QStringLiteral("INTERRUPTED"));
    QCOMPARE(status->toolTip(), QStringLiteral("Proxy degraded"));
}

QTEST_MAIN(Module6Test)
#include "test_module6.moc"
