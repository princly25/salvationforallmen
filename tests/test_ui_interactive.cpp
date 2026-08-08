#include "core/ProfileInstance.hpp"
#include "ui/MainWindow.hpp"
#include "ui/ProfileCardWidget.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QHostAddress>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

namespace {
void replaceText(QLineEdit* input, const QString& value)
{
    QVERIFY(input != nullptr);
    QTest::mouseClick(input, Qt::LeftButton);
    QTest::keyClick(input, Qt::Key_A, Qt::ControlModifier);
    QTest::keyClicks(input, value);
    QCOMPARE(input->text(), value);
}

void clickNavigationRow(QListWidget* navigation, int row)
{
    QVERIFY(navigation != nullptr);
    const QRect itemRect = navigation->visualItemRect(navigation->item(row));
    QTest::mouseClick(navigation->viewport(), Qt::LeftButton, Qt::NoModifier,
                      itemRect.center());
    QCOMPARE(navigation->currentRow(), row);
}

ProfileConfig actionProfileConfig()
{
    ProfileConfig config;
    config.id = QStringLiteral("interactive-actions");
    config.name = QStringLiteral("Interactive Actions");
    config.userAgent = QStringLiteral(
        "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
        "(KHTML, like Gecko) Chrome/128.0.0.0 Safari/537.36");
    config.hardware.masterSeedHex = std::string(64, 'a');
    return config;
}

class MockProviderApi final : public QObject {
public:
    explicit MockProviderApi(quint16 proxyPort, QObject* parent = nullptr)
        : QObject(parent)
        , m_proxyPort(proxyPort)
    {
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (m_server.hasPendingConnections()) {
                QTcpSocket* socket = m_server.nextPendingConnection();
                connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
                    m_request += socket->readAll();
                    if (!m_request.contains("\r\n\r\n")
                        || socket->property("responded").toBool()) {
                        return;
                    }
                    socket->setProperty("responded", true);
                    const QByteArray body = QStringLiteral(
                        R"JSON({"proxies":[{"host":"127.0.0.1","port":%1,"country":"LOCAL"}]})JSON")
                                                .arg(m_proxyPort)
                                                .toUtf8();
                    socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                                  "Connection: close\r\nContent-Length: "
                                  + QByteArray::number(body.size()) + "\r\n\r\n" + body);
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
        return QUrl(QStringLiteral("http://127.0.0.1:%1/proxies").arg(m_server.serverPort()));
    }

    [[nodiscard]] QByteArray request() const
    {
        return m_request;
    }

private:
    QTcpServer m_server;
    quint16 m_proxyPort{0};
    QByteArray m_request;
};
}

class InteractiveUiTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void tabNavigationShowsEveryDashboardPage();
    void profileCreationModalValidatesAndCreatesAuthenticatedProfile();
    void profileCardButtonsInvokeConnectedWorkflows();
    void proxyFetcherUsesCredentialsAndPopulatesTables();
    void settingsControlsPersistAcrossWindows();
};

void InteractiveUiTest::initTestCase()
{
    QCoreApplication::setOrganizationName(QStringLiteral("AntiDetectBrowserTests"));
    QCoreApplication::setApplicationName(QStringLiteral("InteractiveUiHarness"));
    QSettings settings;
    settings.clear();
    settings.sync();
}

void InteractiveUiTest::tabNavigationShowsEveryDashboardPage()
{
    QTemporaryDir storageRoot;
    QVERIFY(storageRoot.isValid());
    MainWindow window(storageRoot.path());
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    auto* navigation = window.findChild<QListWidget*>(QStringLiteral("sidebarNavigation"));
    auto* pages = window.findChild<QStackedWidget*>(QStringLiteral("dashboardPages"));
    QVERIFY(navigation != nullptr);
    QVERIFY(pages != nullptr);
    QCOMPARE(navigation->count(), 5);
    QCOMPARE(pages->count(), 5);
    const QStringList pageNames{QStringLiteral("profilesPage"), QStringLiteral("proxyPoolPage"),
                                QStringLiteral("proxyAutoFetcherPage"),
                                QStringLiteral("diagnosticsPage"),
                                QStringLiteral("settingsPage")};

    for (int row = 0; row < pageNames.size(); ++row) {
        clickNavigationRow(navigation, row);
        QCOMPARE(pages->currentIndex(), row);
        QWidget* expectedPage = window.findChild<QWidget*>(pageNames.at(row));
        QVERIFY(expectedPage != nullptr);
        QCOMPARE(pages->currentWidget(), expectedPage);
        QVERIFY(expectedPage->isVisible());
        for (int other = 0; other < pageNames.size(); ++other) {
            if (other != row) {
                QVERIFY(!window.findChild<QWidget*>(pageNames.at(other))->isVisible());
            }
        }
    }
}

void InteractiveUiTest::profileCreationModalValidatesAndCreatesAuthenticatedProfile()
{
    QTemporaryDir storageRoot;
    QVERIFY(storageRoot.isValid());
    MainWindow window(storageRoot.path());
    window.show();

    QTimer::singleShot(0, &window, [&window] {
        auto* dialog = window.findChild<QDialog*>(QStringLiteral("createProfileDialog"));
        QVERIFY(dialog != nullptr);

        auto* userAgent = dialog->findChild<QLineEdit*>(QStringLiteral("userAgentInput"));
        auto* seed = dialog->findChild<QLineEdit*>(QStringLiteral("masterSeedInput"));
        auto* windows = dialog->findChild<QPushButton*>(QStringLiteral("windowsPreset"));
        auto* mac = dialog->findChild<QPushButton*>(QStringLiteral("macPreset"));
        auto* linux = dialog->findChild<QPushButton*>(QStringLiteral("linuxPreset"));
        QVERIFY(windows != nullptr);
        QVERIFY(mac != nullptr);
        QVERIFY(linux != nullptr);

        QTest::mouseClick(windows, Qt::LeftButton);
        QVERIFY(userAgent->text().contains(QStringLiteral("Windows")));
        QVERIFY(userAgent->text().contains(QStringLiteral("Chrome/")));
        const QString windowsSeed = seed->text();
        QTest::mouseClick(mac, Qt::LeftButton);
        QVERIFY(userAgent->text().contains(QStringLiteral("Macintosh")));
        QVERIFY(userAgent->text().contains(QStringLiteral("Safari/")));
        QVERIFY(!userAgent->text().contains(QStringLiteral("Chrome/")));
        QVERIFY(seed->text() != windowsSeed);
        QTest::mouseClick(linux, Qt::LeftButton);
        QVERIFY(userAgent->text().contains(QStringLiteral("X11; Linux")));
        QTest::mouseClick(mac, Qt::LeftButton);

        replaceText(dialog->findChild<QLineEdit*>(QStringLiteral("profileIdInput")),
                    QStringLiteral("modal-profile"));
        replaceText(dialog->findChild<QLineEdit*>(QStringLiteral("profileNameInput")),
                    QStringLiteral("Modal Profile"));
        auto* proxyType = dialog->findChild<QComboBox*>(QStringLiteral("proxyTypeInput"));
        QVERIFY(proxyType != nullptr);
        proxyType->setCurrentIndex(proxyType->findData(QNetworkProxy::HttpProxy));
        QCOMPARE(proxyType->currentData().toInt(), static_cast<int>(QNetworkProxy::HttpProxy));
        replaceText(dialog->findChild<QLineEdit*>(QStringLiteral("proxyHostInput")),
                    QStringLiteral("127.0.0.1"));
        auto* port = dialog->findChild<QSpinBox*>(QStringLiteral("proxyPortInput"));
        QVERIFY(port != nullptr);
        port->setValue(3128);
        QCOMPARE(port->value(), 3128);
        replaceText(dialog->findChild<QLineEdit*>(QStringLiteral("proxyUsernameInput")),
                    QStringLiteral("interactive-user"));
        replaceText(dialog->findChild<QLineEdit*>(QStringLiteral("proxyPasswordInput")),
                    QStringLiteral("interactive-password"));
        replaceText(dialog->findChild<QLineEdit*>(QStringLiteral("expectedProxyIpInput")),
                    QStringLiteral("203.0.113.27"));
        replaceText(dialog->findChild<QLineEdit*>(QStringLiteral("proxyVerificationUrlInput")),
                    QStringLiteral("https://api.ipify.org"));
        replaceText(seed, QStringLiteral("invalid"));

        auto* buttons = dialog->findChild<QDialogButtonBox*>(QStringLiteral("createProfileButtons"));
        QVERIFY(buttons != nullptr);
        QPushButton* save = buttons->button(QDialogButtonBox::Ok);
        QVERIFY(save != nullptr);
        QCOMPARE(save->text(), QStringLiteral("Save"));
        QTimer::singleShot(0, dialog, [dialog] {
            auto* warning = dialog->findChild<QMessageBox*>();
            if (warning != nullptr) {
                QVERIFY(warning->text().contains(QStringLiteral("Master seed"), Qt::CaseInsensitive));
                if (auto* ok = warning->button(QMessageBox::Ok); ok != nullptr) {
                    QTest::mouseClick(ok, Qt::LeftButton);
                } else {
                    warning->done(QMessageBox::Ok);
                }
            } else {
                dialog->done(QDialog::Rejected);
            }
        });
        QTest::mouseClick(save, Qt::LeftButton);

        replaceText(seed, QString(64, QLatin1Char('b')));
        QTest::mouseClick(save, Qt::LeftButton);
    });

    auto* addProfile = window.findChild<QPushButton*>(QStringLiteral("createProfile"));
    QVERIFY(addProfile != nullptr);
    QTest::mouseClick(addProfile, Qt::LeftButton);
    QCOMPARE(window.profileManager().profileCount(), 1);
    ProfileCardWidget* card = window.profileCard(QStringLiteral("modal-profile"));
    QVERIFY(card != nullptr);
    QCOMPARE(card->findChild<QLabel*>(QStringLiteral("profileName"))->text(),
             QStringLiteral("Modal Profile"));
    const ProfileConfig& config = card->profile()->config();
    QCOMPARE(config.proxy.hostName(), QStringLiteral("127.0.0.1"));
    QCOMPARE(config.proxy.port(), quint16(3128));
    QCOMPARE(config.proxy.user(), QStringLiteral("interactive-user"));
    QCOMPARE(config.proxy.password(), QStringLiteral("interactive-password"));
    QVERIFY(config.userAgent.contains(QStringLiteral("Safari/")));
    QCOMPARE(QString::fromStdString(config.hardware.masterSeedHex), QString(64, QLatin1Char('b')));
}

void InteractiveUiTest::profileCardButtonsInvokeConnectedWorkflows()
{
    QTemporaryDir storageRoot;
    QTemporaryDir exportRoot;
    QVERIFY(storageRoot.isValid());
    QVERIFY(exportRoot.isValid());
    MainWindow window(storageRoot.path());
    window.show();
    ProfileInstance& profile = window.addProfile(actionProfileConfig());
    ProfileCardWidget* card = window.profileCard(QStringLiteral("interactive-actions"));
    QVERIFY(card != nullptr);

    QSignalSpy launchSpy(card, &ProfileCardWidget::launchRequested);
    QSignalSpy syncSpy(card, &ProfileCardWidget::geoSyncRequested);
    QSignalSpy inspectorSpy(card, &ProfileCardWidget::cookieInspectorRequested);
    QSignalSpy exportSpy(card, &ProfileCardWidget::exportCookiesRequested);
    QSignalSpy deleteSpy(card, &ProfileCardWidget::deleteRequested);
    QSignalSpy terminateSpy(card, &ProfileCardWidget::terminateRequested);

    auto* launch = card->findChild<QPushButton*>(QStringLiteral("launchProfile"));
    QTest::mouseClick(launch, Qt::LeftButton);
    QCOMPARE(launchSpy.count(), 1);
    QTRY_COMPARE_WITH_TIMEOUT(profile.state(), ProfileInstance::State::Running, 8000);
    QVERIFY(profile.webEngineProfile() != nullptr);
    QVERIFY(profile.view() != nullptr);
    QVERIFY(window.findChild<QPlainTextEdit*>(QStringLiteral("runtimeLogs"))
                ->toPlainText().contains(QStringLiteral("Profile launched")));

    auto* sync = card->findChild<QPushButton*>(QStringLiteral("syncProxy"));
    QTest::mouseClick(sync, Qt::LeftButton);
    QCOMPARE(syncSpy.count(), 1);
    QCOMPARE(window.findChild<QListWidget*>(QStringLiteral("sidebarNavigation"))->currentRow(), 1);
    window.findChild<QListWidget*>(QStringLiteral("sidebarNavigation"))->setCurrentRow(0);

    auto* inspector = card->findChild<QPushButton*>(QStringLiteral("inspectCookies"));
    QTest::mouseClick(inspector, Qt::LeftButton);
    QCOMPARE(inspectorSpy.count(), 1);
    auto* inspectorDialog = window.findChild<QDialog*>(QStringLiteral("cookieInspectorDialog"));
    QVERIFY(inspectorDialog != nullptr);
    QVERIFY(inspectorDialog->isVisible());
    QVERIFY(inspectorDialog->findChild<QTableWidget*>(QStringLiteral("cookieInspectorTable")) != nullptr);
    inspectorDialog->close();

    const QString exportPath = exportRoot.filePath(QStringLiteral("cookies.json"));
    QTimer::singleShot(0, &window, [&window, exportPath] {
        auto* fileDialog = window.findChild<QFileDialog*>();
        QVERIFY(fileDialog != nullptr);
        fileDialog->selectFile(exportPath);
        auto* buttons = fileDialog->findChild<QDialogButtonBox*>();
        QVERIFY(buttons != nullptr);
        QPushButton* save = buttons->button(QDialogButtonBox::Save);
        QVERIFY(save != nullptr);
        QTest::mouseClick(save, Qt::LeftButton);
    });
    auto* exportButton = card->findChild<QPushButton*>(QStringLiteral("exportCookies"));
    QTest::mouseClick(exportButton, Qt::LeftButton);
    QCOMPARE(exportSpy.count(), 1);
    QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(exportPath), 3000);
    QFile exported(exportPath);
    QVERIFY(exported.open(QIODevice::ReadOnly));
    QVERIFY(exported.readAll().trimmed().startsWith('['));

    auto* freeze = card->findChild<QPushButton*>(QStringLiteral("freezeProfile"));
    QTest::mouseClick(freeze, Qt::LeftButton);
    QCOMPARE(profile.state(), ProfileInstance::State::Frozen);
    QCOMPARE(freeze->text(), QStringLiteral("Unfreeze"));
    QTest::mouseClick(freeze, Qt::LeftButton);
    QCOMPARE(profile.state(), ProfileInstance::State::Running);

    auto* terminate = card->findChild<QPushButton*>(QStringLiteral("terminateProfile"));
    QTest::mouseClick(terminate, Qt::LeftButton);
    QCOMPARE(terminateSpy.count(), 1);
    QCOMPARE(profile.state(), ProfileInstance::State::Terminated);

    auto* deleteButton = card->findChild<QPushButton*>(QStringLiteral("deleteProfile"));
    QTest::mouseClick(deleteButton, Qt::LeftButton);
    QCOMPARE(deleteSpy.count(), 1);
    QCOMPARE(window.profileManager().profileCount(), 0);
    QVERIFY(window.profileCard(QStringLiteral("interactive-actions")) == nullptr);
}

void InteractiveUiTest::proxyFetcherUsesCredentialsAndPopulatesTables()
{
    QTcpServer latencyTarget;
    QVERIFY(latencyTarget.listen(QHostAddress::LocalHost));
    MockProviderApi provider(latencyTarget.serverPort());
    QVERIFY(provider.listen());
    QTemporaryDir storageRoot;
    MainWindow window(storageRoot.path());
    window.show();

    auto* navigation = window.findChild<QListWidget*>(QStringLiteral("sidebarNavigation"));
    clickNavigationRow(navigation, 2);
    auto* apiUrl = window.findChild<QLineEdit*>(QStringLiteral("proxyProviderApiUrl"));
    auto* token = window.findChild<QLineEdit*>(QStringLiteral("proxyProviderToken"));
    auto* format = window.findChild<QComboBox*>(QStringLiteral("proxyProviderFormat"));
    auto* fetch = window.findChild<QPushButton*>(QStringLiteral("fetchProxies"));
    replaceText(apiUrl, provider.url().toString());
    replaceText(token, QStringLiteral("interactive-api-token"));
    format->setCurrentIndex(format->findData(
        static_cast<int>(ProxyProviderManager::ProviderFormat::Custom)));
    QTest::mouseClick(fetch, Qt::LeftButton);
    QVERIFY(!fetch->isEnabled());

    auto* fetched = window.findChild<QTableWidget*>(QStringLiteral("fetchedProxyTable"));
    auto* active = window.findChild<QTableWidget*>(QStringLiteral("proxyTable"));
    QTRY_COMPARE_WITH_TIMEOUT(fetched->rowCount(), 1, 5000);
    QCOMPARE(active->rowCount(), 1);
    QVERIFY(fetch->isEnabled());
    QCOMPARE(fetched->item(0, 0)->text(), QStringLiteral("127.0.0.1"));
    QCOMPARE(fetched->item(0, 1)->text(), QString::number(latencyTarget.serverPort()));
    QVERIFY(window.findChild<QLabel*>(QStringLiteral("proxyFetcherStatus"))
                ->text().contains(QStringLiteral("Active pool updated")));
    QVERIFY(provider.request().contains("Authorization: Bearer interactive-api-token"));
    QVERIFY(provider.request().contains("X-API-Key: interactive-api-token"));
}

void InteractiveUiTest::settingsControlsPersistAcrossWindows()
{
    QTemporaryDir firstStorage;
    MainWindow first(firstStorage.path());
    first.show();
    QVERIFY(QTest::qWaitForWindowExposed(&first));
    auto* navigation = first.findChild<QListWidget*>(QStringLiteral("sidebarNavigation"));
    clickNavigationRow(navigation, 4);

    auto* compact = first.findChild<QCheckBox*>(QStringLiteral("compactCards"));
    auto* killSwitch = first.findChild<QCheckBox*>(QStringLiteral("killSwitchEnabled"));
    auto* timezone = first.findChild<QComboBox*>(QStringLiteral("defaultTimezone"));
    QVERIFY(compact != nullptr);
    QVERIFY(killSwitch != nullptr);
    QVERIFY(timezone != nullptr);
    if (!compact->isChecked()) {
        compact->click();
    }
    if (killSwitch->isChecked()) {
        killSwitch->click();
    }
    replaceText(timezone->lineEdit(), QStringLiteral("Pacific/Auckland"));
    QTest::keyClick(timezone->lineEdit(), Qt::Key_Enter);
    QSettings().sync();

    QTemporaryDir secondStorage;
    MainWindow second(secondStorage.path());
    auto* persistedCompact = second.findChild<QCheckBox*>(QStringLiteral("compactCards"));
    auto* persistedKillSwitch = second.findChild<QCheckBox*>(QStringLiteral("killSwitchEnabled"));
    auto* persistedTimezone = second.findChild<QComboBox*>(QStringLiteral("defaultTimezone"));
    QVERIFY(persistedCompact->isChecked());
    QVERIFY(!persistedKillSwitch->isChecked());
    QCOMPARE(persistedTimezone->currentText(), QStringLiteral("Pacific/Auckland"));

    QTimer::singleShot(0, &second, [&second] {
        auto* dialog = second.findChild<QDialog*>(QStringLiteral("createProfileDialog"));
        QVERIFY(dialog != nullptr);
        QCOMPARE(dialog->findChild<QLineEdit*>(QStringLiteral("timezoneInput"))->text(),
                 QStringLiteral("Pacific/Auckland"));
        QTest::mouseClick(
            dialog->findChild<QDialogButtonBox*>(QStringLiteral("createProfileButtons"))
                ->button(QDialogButtonBox::Cancel),
            Qt::LeftButton);
    });
    QTest::mouseClick(second.findChild<QPushButton*>(QStringLiteral("createProfile")),
                      Qt::LeftButton);
}

int main(int argc, char* argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("QTWEBENGINE_DISABLE_SANDBOX", "1");
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication application(argc, argv);
    InteractiveUiTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_ui_interactive.moc"
