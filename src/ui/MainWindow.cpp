#include "ui/MainWindow.hpp"

#include "core/ProfileInstance.hpp"
#include "geo/GeoSyncEngine.hpp"
#include "network/KillSwitchEngine.hpp"
#include "ui/ProfileCardWidget.hpp"

#include <QDialog>
#include <QDialogButtonBox>
#include <QComboBox>
#include <QCheckBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QNetworkCookie>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QScrollArea>
#include <QSplitter>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QWebEngineCookieStore>
#include <QWebEngineProfile>
#include <QWebEngineView>

#include <optional>
#include <algorithm>
#include <stdexcept>

namespace {
QString generateMasterSeed()
{
    QString seed;
    seed.reserve(64);
    for (int index = 0; index < 8; ++index) {
        seed += QStringLiteral("%1").arg(QRandomGenerator::system()->generate(), 8, 16,
                                         QLatin1Char('0'));
    }
    return seed;
}
}

MainWindow::MainWindow(QString storageRoot, QWidget* parent)
    : QMainWindow(parent)
    , m_profileManager(std::make_unique<ProfileManager>(std::move(storageRoot)))
    , m_proxyProviderManager(std::make_unique<ProxyProviderManager>())
{
    buildUi();
}

MainWindow::~MainWindow() = default;

void MainWindow::buildUi()
{
    setObjectName(QStringLiteral("mainWindow"));
    setWindowTitle(QStringLiteral("AntiDetectBrowser"));
    resize(1360, 860);
    setStyleSheet(QStringLiteral(
        "QMainWindow, QWidget { background: #111827; color: #F9FAFB; font-family: 'Inter', 'Segoe UI'; }"
        "QWidget#sidebar { background: #0B1220; border-right: 1px solid #374151; }"
        "QLabel#brand { color: #F9FAFB; font-size: 20px; font-weight: 700; padding: 8px 4px 20px; }"
        "QListWidget#sidebarNavigation { background: transparent; border: none; outline: none; padding: 4px; }"
        "QListWidget#sidebarNavigation::item { color: #9CA3AF; padding: 12px 14px; margin: 2px 0; border-radius: 8px; }"
        "QListWidget#sidebarNavigation::item:hover { color: #F9FAFB; background: #1F2937; }"
        "QListWidget#sidebarNavigation::item:selected { color: #FFFFFF; background: #1D4ED8; }"
        "QLineEdit, QComboBox, QSpinBox, QTableWidget, QPlainTextEdit { background: #1F2937; border: 1px solid #374151; border-radius: 8px; color: #F9FAFB; padding: 8px; selection-background-color: #3B82F6; }"
        "QTableWidget { gridline-color: #374151; alternate-background-color: #172235; }"
        "QHeaderView::section { background: #1F2937; border: none; border-bottom: 1px solid #374151; color: #9CA3AF; padding: 10px; font-weight: 600; }"
        "QPushButton { background: #3B82F6; color: #FFFFFF; border: none; border-radius: 8px; padding: 9px 14px; font-weight: 600; }"
        "QPushButton:hover { background: #2563EB; } QPushButton:pressed { background: #1D4ED8; }"
        "QPushButton#secondaryAction { background: #1F2937; border: 1px solid #374151; }"
        "QPushButton#secondaryAction:hover { background: #374151; }"
        "QPushButton:disabled { background: #374151; color: #6B7280; }"
        "QGroupBox { background: #1F2937; border: 1px solid #374151; border-radius: 8px; margin-top: 12px; padding: 16px; font-weight: 600; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 14px; padding: 0 6px; color: #D1D5DB; }"
        "QFrame[profileCard=\"true\"] { background: #1F2937; border: 1px solid #374151; border-radius: 8px; }"
        "QFrame[profileCard=\"true\"] QLabel { background: transparent; border: none; }"
        "QScrollArea { border: none; background: transparent; }"
        "QStatusBar { background: #0B1220; border-top: 1px solid #374151; color: #9CA3AF; }"));

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setObjectName(QStringLiteral("dashboardSplitter"));
    auto* sidebar = new QWidget(splitter);
    sidebar->setObjectName(QStringLiteral("sidebar"));
    sidebar->setFixedWidth(248);
    auto* sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setContentsMargins(18, 24, 18, 18);
    auto* brand = new QLabel(QStringLiteral("AntiDetect Browser"), sidebar);
    brand->setObjectName(QStringLiteral("brand"));
    sidebarLayout->addWidget(brand);
    m_navigation = new QListWidget(sidebar);
    m_navigation->setObjectName(QStringLiteral("sidebarNavigation"));
    m_navigation->addItems({QStringLiteral("Profiles"), QStringLiteral("Proxy Pool"),
                            QStringLiteral("Proxy Auto-Fetcher"),
                            QStringLiteral("Diagnostics & Logs"), QStringLiteral("Settings")});
    sidebarLayout->addWidget(m_navigation, 1);
    auto* version = new QLabel(QStringLiteral("v0.1 · hardened desktop"), sidebar);
    version->setStyleSheet(QStringLiteral("color: #6B7280; font-size: 11px;"));
    sidebarLayout->addWidget(version);

    m_pages = new QStackedWidget(splitter);
    m_pages->setObjectName(QStringLiteral("dashboardPages"));
    m_pages->addWidget(buildProfilesPage());
    m_pages->addWidget(buildProxyPage());
    m_pages->addWidget(buildProxyFetcherPage());
    m_pages->addWidget(buildDiagnosticsPage());
    m_pages->addWidget(buildSettingsPage());
    splitter->addWidget(sidebar);
    splitter->addWidget(m_pages);
    splitter->setStretchFactor(1, 1);
    setCentralWidget(splitter);

    connect(m_navigation, &QListWidget::currentRowChanged,
            m_pages, &QStackedWidget::setCurrentIndex);
    m_navigation->setCurrentRow(0);

    m_latencyStatus = new QLabel(QStringLiteral("Ping: -- ms"), this);
    m_latencyStatus->setObjectName(QStringLiteral("latencyStatus"));
    m_proxyStatus = new QLabel(QStringLiteral("Proxy IP: --"), this);
    m_proxyStatus->setObjectName(QStringLiteral("activeProxyStatus"));
    m_killSwitchStatus = new QLabel(this);
    m_killSwitchStatus->setObjectName(QStringLiteral("killSwitchStatus"));
    statusBar()->addPermanentWidget(m_latencyStatus);
    statusBar()->addPermanentWidget(m_proxyStatus);
    statusBar()->addPermanentWidget(m_killSwitchStatus);
    updateNetworkStatus(NetworkStatus::Healthy);
}

QWidget* MainWindow::buildProfilesPage()
{
    auto* page = new QWidget;
    page->setObjectName(QStringLiteral("profilesPage"));
    auto* pageLayout = new QVBoxLayout(page);
    auto* headingLayout = new QHBoxLayout;
    auto* heading = new QLabel(QStringLiteral("Isolated Browser Profiles"), page);
    QFont headingFont = heading->font();
    headingFont.setBold(true);
    headingFont.setPointSize(headingFont.pointSize() + 4);
    heading->setFont(headingFont);
    heading->setStyleSheet(QStringLiteral("color: #F9FAFB;"));
    headingLayout->addWidget(heading);
    headingLayout->addStretch(1);
    auto* createButton = new QPushButton(QStringLiteral("Add Profile"), page);
    createButton->setObjectName(QStringLiteral("createProfile"));
    connect(createButton, &QPushButton::clicked, this, &MainWindow::showCreateProfileDialog);
    headingLayout->addWidget(createButton);
    pageLayout->addLayout(headingLayout);
    auto* subtitle = new QLabel(
        QStringLiteral("Manage isolated environments with deterministic fingerprints and verified exits."), page);
    subtitle->setStyleSheet(QStringLiteral("color: #9CA3AF;"));
    pageLayout->addWidget(subtitle);

    m_emptyProfilesLabel = new QLabel(
        QStringLiteral("No profiles yet. Add a profile to create its isolated storage sandbox."), page);
    m_emptyProfilesLabel->setObjectName(QStringLiteral("emptyProfiles"));
    m_emptyProfilesLabel->setAlignment(Qt::AlignCenter);
    m_emptyProfilesLabel->setMinimumHeight(120);
    pageLayout->addWidget(m_emptyProfilesLabel);

    auto* scrollArea = new QScrollArea(page);
    scrollArea->setObjectName(QStringLiteral("profileScrollArea"));
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    auto* gridContainer = new QWidget(scrollArea);
    gridContainer->setObjectName(QStringLiteral("profileGridContainer"));
    m_profileGrid = new QGridLayout(gridContainer);
    m_profileGrid->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_profileGrid->setSpacing(16);
    scrollArea->setWidget(gridContainer);
    pageLayout->addWidget(scrollArea, 1);
    return page;
}

QWidget* MainWindow::buildProxyPage()
{
    auto* page = new QWidget;
    page->setObjectName(QStringLiteral("proxyPoolPage"));
    auto* layout = new QVBoxLayout(page);
    auto* heading = new QLabel(QStringLiteral("Proxy Pool"), page);
    heading->setStyleSheet(QStringLiteral("font-size: 22px; font-weight: 700;"));
    layout->addWidget(heading);
    auto* description = new QLabel(
        QStringLiteral("Inspect assigned exits, latency, and location synchronization for every profile."), page);
    description->setStyleSheet(QStringLiteral("color: #9CA3AF;"));
    layout->addWidget(description);

    auto* provider = new QGroupBox(QStringLiteral("Provider API"), page);
    auto* providerForm = new QFormLayout(provider);
    m_proxyProviderFormat = new QComboBox(provider);
    m_proxyProviderFormat->setObjectName(QStringLiteral("proxyProviderFormat"));
    m_proxyProviderFormat->addItem(QStringLiteral("Auto-detect"),
                                   static_cast<int>(ProxyProviderManager::ProviderFormat::AutoDetect));
    m_proxyProviderFormat->addItem(QStringLiteral("Webshare"),
                                   static_cast<int>(ProxyProviderManager::ProviderFormat::Webshare));
    m_proxyProviderFormat->addItem(QStringLiteral("IPRoyal"),
                                   static_cast<int>(ProxyProviderManager::ProviderFormat::IPRoyal));
    m_proxyProviderFormat->addItem(QStringLiteral("Custom JSON API"),
                                   static_cast<int>(ProxyProviderManager::ProviderFormat::Custom));
    m_proxyProviderApiUrl = new QLineEdit(provider);
    m_proxyProviderApiUrl->setObjectName(QStringLiteral("proxyProviderApiUrl"));
    m_proxyProviderApiUrl->setPlaceholderText(
        QStringLiteral("https://proxy-provider.example/api/proxies"));
    m_proxyProviderToken = new QLineEdit(provider);
    m_proxyProviderToken->setObjectName(QStringLiteral("proxyProviderToken"));
    m_proxyProviderToken->setEchoMode(QLineEdit::Password);
    m_proxyProviderToken->setPlaceholderText(QStringLiteral("API key or token"));
    m_fetchProxiesButton = new QPushButton(QStringLiteral("Fetch and Test Proxies"), provider);
    m_fetchProxiesButton->setObjectName(QStringLiteral("fetchProxies"));
    m_proxyFetcherStatus = new QLabel(QStringLiteral("Ready to connect."), provider);
    m_proxyFetcherStatus->setObjectName(QStringLiteral("proxyFetcherStatus"));
    m_proxyFetcherStatus->setStyleSheet(QStringLiteral("color: #9CA3AF;"));
    providerForm->addRow(QStringLiteral("Provider"), m_proxyProviderFormat);
    providerForm->addRow(QStringLiteral("API URL"), m_proxyProviderApiUrl);
    providerForm->addRow(QStringLiteral("Token"), m_proxyProviderToken);
    providerForm->addRow(m_fetchProxiesButton);
    providerForm->addRow(m_proxyFetcherStatus);
    layout->addWidget(provider);

    m_proxyTable = new QTableWidget(0, 7, page);
    m_proxyTable->setObjectName(QStringLiteral("proxyTable"));
    m_proxyTable->setHorizontalHeaderLabels({QStringLiteral("Profile"), QStringLiteral("Host"),
                                             QStringLiteral("Port"), QStringLiteral("Type"),
                                             QStringLiteral("Expected Exit IP"),
                                             QStringLiteral("Location"),
                                             QStringLiteral("Latency")});
    m_proxyTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_proxyTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(m_proxyTable, 1);

    auto* geoGroup = new QGroupBox(QStringLiteral("GeoIP Lookup"), page);
    auto* geoLayout = new QFormLayout(geoGroup);
    m_geoDatabasePath = new QLineEdit(geoGroup);
    m_geoDatabasePath->setObjectName(QStringLiteral("geoDatabasePath"));
    m_geoDatabasePath->setText(QStringLiteral("data/GeoLite2-City.mmdb"));
    m_geoIpAddress = new QLineEdit(geoGroup);
    m_geoIpAddress->setObjectName(QStringLiteral("geoIpAddress"));
    m_geoIpAddress->setPlaceholderText(QStringLiteral("Proxy exit IP"));
    auto* resolveButton = new QPushButton(QStringLiteral("Resolve and Preview"), geoGroup);
    resolveButton->setObjectName(QStringLiteral("resolveGeoIp"));
    m_geoResult = new QLabel(QStringLiteral("No lookup performed."), geoGroup);
    m_geoResult->setObjectName(QStringLiteral("geoResult"));
    m_geoResult->setWordWrap(true);
    geoLayout->addRow(QStringLiteral("Database"), m_geoDatabasePath);
    geoLayout->addRow(QStringLiteral("IP address"), m_geoIpAddress);
    geoLayout->addRow(resolveButton);
    geoLayout->addRow(m_geoResult);
    connect(resolveButton, &QPushButton::clicked, this, &MainWindow::resolveGeoIp);
    layout->addWidget(geoGroup);

    connect(m_fetchProxiesButton, &QPushButton::clicked, this, [this] {
        const QUrl apiUrl = QUrl::fromUserInput(m_proxyProviderApiUrl->text().trimmed());
        const auto format = static_cast<ProxyProviderManager::ProviderFormat>(
            m_proxyProviderFormat->currentData().toInt());
        m_fetchProxiesButton->setEnabled(false);
        m_proxyFetcherStatus->setStyleSheet(QStringLiteral("color: #93C5FD;"));
        m_proxyFetcherStatus->setText(QStringLiteral("Fetching provider inventory…"));
        m_proxyProviderManager->fetchProxies(apiUrl, m_proxyProviderToken->text(), format);
    });
    connect(m_proxyProviderManager.get(), &ProxyProviderManager::proxiesFetched, this,
            [this](const QList<ProxyEndpoint>& proxies) {
                m_proxyFetcherStatus->setText(
                    QStringLiteral("Fetched %1 proxies. Testing TCP latency…").arg(proxies.size()));
                m_proxyProviderManager->testLatencies(proxies);
            });
    connect(m_proxyProviderManager.get(), &ProxyProviderManager::fetchFailed, this,
            [this](const QString& error) {
                m_fetchProxiesButton->setEnabled(true);
                m_proxyFetcherStatus->setStyleSheet(QStringLiteral("color: #F87171;"));
                m_proxyFetcherStatus->setText(error);
                addLogMessage(QStringLiteral("[proxy-provider] %1").arg(error));
            });
    connect(m_proxyProviderManager.get(), &ProxyProviderManager::latencyProgress, this,
            [this](int completed, int total) {
                m_proxyFetcherStatus->setText(
                    QStringLiteral("Testing proxy latency: %1 / %2").arg(completed).arg(total));
            });
    connect(m_proxyProviderManager.get(), &ProxyProviderManager::latencyTestsFinished, this,
            [this](const QList<ProxyEndpoint>& proxies) {
                m_fetchProxiesButton->setEnabled(true);
                populateFetchedProxyPool(proxies);
                const int reachable = static_cast<int>(std::count_if(
                    proxies.cbegin(), proxies.cend(),
                    [](const ProxyEndpoint& proxy) { return proxy.reachable; }));
                m_proxyFetcherStatus->setStyleSheet(QStringLiteral("color: #4ADE80;"));
                m_proxyFetcherStatus->setText(
                    QStringLiteral("Active pool updated: %1 / %2 proxies reachable.")
                        .arg(reachable)
                        .arg(proxies.size()));
                addLogMessage(QStringLiteral("[proxy-provider] Pool updated with %1 reachable proxies.")
                                  .arg(reachable));
            });
    return page;
}

QWidget* MainWindow::buildProxyFetcherPage()
{
    auto* page = new QWidget;
    page->setObjectName(QStringLiteral("proxyAutoFetcherPage"));
    auto* layout = new QVBoxLayout(page);
    auto* heading = new QLabel(QStringLiteral("Proxy Auto-Fetcher"), page);
    heading->setStyleSheet(QStringLiteral("font-size: 22px; font-weight: 700;"));
    layout->addWidget(heading);
    auto* description = new QLabel(
        QStringLiteral("Connect a provider API, validate latency, and keep the active proxy pool fresh."), page);
    description->setStyleSheet(QStringLiteral("color: #9CA3AF;"));
    description->setWordWrap(true);
    layout->addWidget(description);

    m_fetchedProxyTable = new QTableWidget(0, 6, page);
    m_fetchedProxyTable->setObjectName(QStringLiteral("fetchedProxyTable"));
    m_fetchedProxyTable->setHorizontalHeaderLabels(
        {QStringLiteral("Host"), QStringLiteral("Port"), QStringLiteral("Type"),
         QStringLiteral("Location"), QStringLiteral("Latency"), QStringLiteral("Authentication")});
    m_fetchedProxyTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_fetchedProxyTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_fetchedProxyTable->setAlternatingRowColors(true);
    layout->addWidget(m_fetchedProxyTable, 1);
    auto* hint = new QLabel(
        QStringLiteral("Enter provider credentials in Proxy Pool, then fetch and test the inventory."), page);
    hint->setStyleSheet(QStringLiteral("color: #6B7280;"));
    layout->addWidget(hint);
    return page;
}

QWidget* MainWindow::buildDiagnosticsPage()
{
    auto* page = new QWidget;
    page->setObjectName(QStringLiteral("diagnosticsPage"));
    auto* layout = new QVBoxLayout(page);
    auto* heading = new QLabel(QStringLiteral("Diagnostics & Logs"), page);
    heading->setStyleSheet(QStringLiteral("font-size: 22px; font-weight: 700;"));
    layout->addWidget(heading);
    auto* description = new QLabel(
        QStringLiteral("Proxy heartbeats run asynchronously. A failed heartbeat triggers immediate "
                       "request containment; hardware loss is contained on the "
                       "first OS reachability callback."),
        page);
    description->setWordWrap(true);
    layout->addWidget(description);
    auto* status = new QLabel(
        QStringLiteral("Restoration requires a proxy-routed exit-IP response matching the profile."),
        page);
    status->setObjectName(QStringLiteral("diagnosticsSummary"));
    status->setWordWrap(true);
    layout->addWidget(status);
    auto* logsLabel = new QLabel(QStringLiteral("Runtime logs"), page);
    logsLabel->setStyleSheet(QStringLiteral("font-size: 16px; font-weight: 600; margin-top: 12px;"));
    layout->addWidget(logsLabel);
    m_logs = new QPlainTextEdit(page);
    m_logs->setObjectName(QStringLiteral("runtimeLogs"));
    m_logs->setReadOnly(true);
    layout->addWidget(m_logs, 1);
    return page;
}

QWidget* MainWindow::buildSettingsPage()
{
    auto* page = new QWidget;
    page->setObjectName(QStringLiteral("settingsPage"));
    auto* layout = new QVBoxLayout(page);
    auto* heading = new QLabel(QStringLiteral("Settings"), page);
    heading->setStyleSheet(QStringLiteral("font-size: 22px; font-weight: 700;"));
    layout->addWidget(heading);
    auto* appearance = new QGroupBox(QStringLiteral("Appearance"), page);
    auto* appearanceLayout = new QVBoxLayout(appearance);
    auto* compact = new QCheckBox(QStringLiteral("Use compact profile cards"), appearance);
    compact->setObjectName(QStringLiteral("compactCards"));
    compact->setChecked(false);
    appearanceLayout->addWidget(compact);
    auto* privacy = new QCheckBox(QStringLiteral("Keep WebRTC on proxied interfaces only"), appearance);
    privacy->setChecked(true);
    privacy->setEnabled(false);
    appearanceLayout->addWidget(privacy);
    layout->addWidget(appearance);
    layout->addStretch(1);
    return page;
}

ProfileInstance& MainWindow::addProfile(const ProfileConfig& config)
{
    ProfileInstance& profile = m_profileManager->createProfile(config);
    auto* card = new ProfileCardWidget(&profile);
    const int index = static_cast<int>(m_profileCards.size());
    m_profileGrid->addWidget(card, index / 2, index % 2);
    m_profileCards.emplace(config.id, card);
    m_emptyProfilesLabel->hide();

    connect(card, &ProfileCardWidget::launchRequested, this, &MainWindow::launchProfile);
    connect(card, &ProfileCardWidget::terminateRequested, this, &MainWindow::terminateProfile);
    connect(card, &ProfileCardWidget::cookieInspectorRequested,
            this, &MainWindow::showCookieInspector);
    connect(card, &ProfileCardWidget::geoSyncRequested, this, &MainWindow::prepareGeoSync);
    connect(card, &ProfileCardWidget::exportCookiesRequested, this, &MainWindow::exportCookies);
    connect(card, &ProfileCardWidget::deleteRequested, this, &MainWindow::deleteProfile);

    auto engine = std::make_unique<KillSwitchEngine>(&profile);
    KillSwitchEngine* enginePointer = engine.get();
    connect(enginePointer, &KillSwitchEngine::containmentChanged, this,
            [this, id = config.id](bool contained, NetworkStatus status) {
                handleContainment(id, contained, status);
            });
    connect(enginePointer, &KillSwitchEngine::restorationRejected, this,
            [this, id = config.id](const QString& reason) {
                addLogMessage(QStringLiteral("[%1] Restoration rejected: %2").arg(id, reason));
            });
    connect(enginePointer, &KillSwitchEngine::restorationSucceeded, this,
            [this, id = config.id] {
                addLogMessage(QStringLiteral("[%1] Proxy verification passed; profile resumed.").arg(id));
            });
    connect(enginePointer->networkMonitor(), &NetworkMonitor::heartbeatSucceeded, this,
            [this, id = config.id, card, enginePointer](int latencyMs) {
                const ProfileInstance* activeProfile = m_profileManager->profile(id);
                card->setProxyStatus(true, latencyMs,
                                     activeProfile == nullptr
                                         ? QString{}
                                         : activeProfile->config().expectedProxyIp);
                if (!enginePointer->isContained() && !hasContainedProfile()) {
                    updateNetworkStatus(NetworkStatus::Healthy,
                                        activeProfile == nullptr
                                            ? QString{}
                                            : activeProfile->config().expectedProxyIp,
                                        latencyMs);
                }
            });
    m_killSwitches.emplace(config.id, std::move(engine));
    addProxyTableRow(profile.config());
    addLogMessage(QStringLiteral("[%1] Profile added and sandbox prepared.").arg(config.id));
    return profile;
}

ProfileManager& MainWindow::profileManager() noexcept
{
    return *m_profileManager;
}

ProfileCardWidget* MainWindow::profileCard(const QString& profileId) const noexcept
{
    const auto found = m_profileCards.find(profileId);
    return found == m_profileCards.end() ? nullptr : found->second;
}

KillSwitchEngine* MainWindow::killSwitch(const QString& profileId) const noexcept
{
    const auto found = m_killSwitches.find(profileId);
    return found == m_killSwitches.end() ? nullptr : found->second.get();
}

void MainWindow::showCreateProfileDialog()
{
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("createProfileDialog"));
    dialog.setWindowTitle(QStringLiteral("Add Isolated Profile"));
    dialog.setMinimumWidth(640);

    auto* layout = new QVBoxLayout(&dialog);
    auto* presets = new QGroupBox(QStringLiteral("One-click fingerprint presets"), &dialog);
    auto* presetLayout = new QHBoxLayout(presets);
    auto* windowsPreset = new QPushButton(QStringLiteral("Windows 11"), presets);
    windowsPreset->setObjectName(QStringLiteral("windowsPreset"));
    auto* macPreset = new QPushButton(QStringLiteral("macOS"), presets);
    macPreset->setObjectName(QStringLiteral("macPreset"));
    auto* linuxPreset = new QPushButton(QStringLiteral("Linux"), presets);
    linuxPreset->setObjectName(QStringLiteral("linuxPreset"));
    presetLayout->addWidget(windowsPreset);
    presetLayout->addWidget(macPreset);
    presetLayout->addWidget(linuxPreset);
    layout->addWidget(presets);

    auto* form = new QFormLayout;
    auto* id = new QLineEdit(&dialog);
    id->setObjectName(QStringLiteral("profileIdInput"));
    id->setText(QStringLiteral("profile-%1")
                    .arg(QRandomGenerator::global()->bounded(100000U), 5, 10, QLatin1Char('0')));
    auto* name = new QLineEdit(&dialog);
    name->setObjectName(QStringLiteral("profileNameInput"));
    name->setText(QStringLiteral("New Profile"));
    auto* userAgent = new QLineEdit(&dialog);
    userAgent->setObjectName(QStringLiteral("userAgentInput"));
    userAgent->setText(QStringLiteral(
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
        "(KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36"));

    auto* proxyType = new QComboBox(&dialog);
    proxyType->setObjectName(QStringLiteral("proxyTypeInput"));
    proxyType->addItem(QStringLiteral("No proxy"), QNetworkProxy::NoProxy);
    proxyType->addItem(QStringLiteral("HTTP"), QNetworkProxy::HttpProxy);
    proxyType->addItem(QStringLiteral("SOCKS5"), QNetworkProxy::Socks5Proxy);
    auto* proxyHost = new QLineEdit(&dialog);
    proxyHost->setObjectName(QStringLiteral("proxyHostInput"));
    auto* proxyPort = new QSpinBox(&dialog);
    proxyPort->setObjectName(QStringLiteral("proxyPortInput"));
    proxyPort->setRange(1, 65535);
    proxyPort->setValue(8080);
    auto* expectedIp = new QLineEdit(&dialog);
    expectedIp->setObjectName(QStringLiteral("expectedProxyIpInput"));
    auto* verificationUrl = new QLineEdit(&dialog);
    verificationUrl->setObjectName(QStringLiteral("proxyVerificationUrlInput"));
    verificationUrl->setPlaceholderText(QStringLiteral("https://api.ipify.org"));

    auto* timezone = new QLineEdit(QStringLiteral("UTC"), &dialog);
    timezone->setObjectName(QStringLiteral("timezoneInput"));
    auto* timezoneOffset = new QSpinBox(&dialog);
    timezoneOffset->setObjectName(QStringLiteral("timezoneOffsetInput"));
    timezoneOffset->setRange(-840, 840);
    timezoneOffset->setSuffix(QStringLiteral(" min"));
    auto* languages = new QLineEdit(QStringLiteral("en-US, en"), &dialog);
    languages->setObjectName(QStringLiteral("languagesInput"));
    auto* masterSeed = new QLineEdit(generateMasterSeed(), &dialog);
    masterSeed->setObjectName(QStringLiteral("masterSeedInput"));
    masterSeed->setMaxLength(64);
    auto* regenerateSeed = new QPushButton(QStringLiteral("Generate New Seed"), &dialog);
    regenerateSeed->setObjectName(QStringLiteral("regenerateSeed"));

    form->addRow(QStringLiteral("Profile ID"), id);
    form->addRow(QStringLiteral("Name"), name);
    form->addRow(QStringLiteral("User-Agent"), userAgent);
    form->addRow(QStringLiteral("Proxy type"), proxyType);
    form->addRow(QStringLiteral("Proxy host"), proxyHost);
    form->addRow(QStringLiteral("Proxy port"), proxyPort);
    form->addRow(QStringLiteral("Expected exit IP"), expectedIp);
    form->addRow(QStringLiteral("Verification URL"), verificationUrl);
    form->addRow(QStringLiteral("Timezone"), timezone);
    form->addRow(QStringLiteral("UTC offset"), timezoneOffset);
    form->addRow(QStringLiteral("Languages"), languages);
    form->addRow(QStringLiteral("Master seed"), masterSeed);
    form->addRow(QString{}, regenerateSeed);
    layout->addLayout(form);

    const auto applyPreset = [userAgent, masterSeed](const QString& preset) {
        if (preset == QStringLiteral("windows")) {
            userAgent->setText(QStringLiteral(
                "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                "(KHTML, like Gecko) Chrome/128.0.0.0 Safari/537.36"));
        } else if (preset == QStringLiteral("mac")) {
            userAgent->setText(QStringLiteral(
                "Mozilla/5.0 (Macintosh; Intel Mac OS X 14_6) AppleWebKit/537.36 "
                "(KHTML, like Gecko) Chrome/128.0.0.0 Safari/537.36"));
        } else {
            userAgent->setText(QStringLiteral(
                "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
                "(KHTML, like Gecko) Chrome/128.0.0.0 Safari/537.36"));
        }
        masterSeed->setText(generateMasterSeed());
    };
    connect(windowsPreset, &QPushButton::clicked, &dialog,
            [applyPreset] { applyPreset(QStringLiteral("windows")); });
    connect(macPreset, &QPushButton::clicked, &dialog,
            [applyPreset] { applyPreset(QStringLiteral("mac")); });
    connect(linuxPreset, &QPushButton::clicked, &dialog,
            [applyPreset] { applyPreset(QStringLiteral("linux")); });
    connect(regenerateSeed, &QPushButton::clicked, &dialog,
            [masterSeed] { masterSeed->setText(generateMasterSeed()); });

    const auto updateProxyFields = [proxyType, proxyHost, proxyPort, expectedIp, verificationUrl] {
        const bool enabled = proxyType->currentData().toInt() != QNetworkProxy::NoProxy;
        proxyHost->setEnabled(enabled);
        proxyPort->setEnabled(enabled);
        expectedIp->setEnabled(enabled);
        verificationUrl->setEnabled(enabled);
    };
    connect(proxyType, &QComboBox::currentIndexChanged, &dialog,
            [updateProxyFields](int) { updateProxyFields(); });
    updateProxyFields();

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         Qt::Horizontal, &dialog);
    buttons->setObjectName(QStringLiteral("createProfileButtons"));
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    while (dialog.exec() == QDialog::Accepted) {
        ProfileConfig config;
        config.id = id->text().trimmed();
        config.name = name->text().trimmed();
        config.userAgent = userAgent->text().trimmed();
        config.geoDatabasePath = m_geoDatabasePath->text().trimmed();
        config.timezone = timezone->text().trimmed();
        config.timezoneOffsetMinutes = timezoneOffset->value();
        config.languages.clear();
        for (const QString& language : languages->text().split(',', Qt::SkipEmptyParts)) {
            config.languages.append(language.trimmed());
        }
        config.hardware.masterSeedHex = masterSeed->text().trimmed().toStdString();

        const auto selectedProxyType = static_cast<QNetworkProxy::ProxyType>(
            proxyType->currentData().toInt());
        if (selectedProxyType != QNetworkProxy::NoProxy) {
            config.proxy = QNetworkProxy(selectedProxyType, proxyHost->text().trimmed(),
                                         static_cast<quint16>(proxyPort->value()));
            config.expectedProxyIp = expectedIp->text().trimmed();
            config.proxyVerificationUrl = QUrl::fromUserInput(verificationUrl->text().trimmed());
        }

        try {
            addProfile(config);
            return;
        } catch (const std::exception& error) {
            QMessageBox::warning(&dialog, QStringLiteral("Profile Validation Failed"),
                                 QString::fromUtf8(error.what()));
        }
    }
}

void MainWindow::launchProfile(const QString& profileId)
{
    ProfileInstance* profile = m_profileManager->profile(profileId);
    if (profile == nullptr) {
        addLogMessage(QStringLiteral("[%1] Launch failed: profile not found.").arg(profileId));
        return;
    }
    try {
        profile->launch();
        if (profile->view() != nullptr) {
            profile->view()->setWindowTitle(profile->config().name);
            profile->view()->resize(1100, 720);
            profile->view()->show();
        }
        if (profile->config().proxy.type() != QNetworkProxy::NoProxy) {
            m_killSwitches.at(profileId)->startMonitoring();
        }
        if (ProfileCardWidget* card = profileCard(profileId); card != nullptr
            && profile->webEngineProfile() != nullptr) {
            QWebEngineCookieStore* store = profile->webEngineProfile()->cookieStore();
            if (!store->property("cookieCountTracking").toBool()) {
                store->setProperty("cookieCountTracking", true);
                auto count = std::make_shared<int>(0);
                connect(store, &QWebEngineCookieStore::cookieAdded, card,
                        [card, count](const QNetworkCookie&) {
                            card->setCookieCount(++(*count));
                        });
                store->loadAllCookies();
            }
        }
        addLogMessage(QStringLiteral("[%1] Profile launched.").arg(profileId));
    } catch (const std::exception& error) {
        addLogMessage(QStringLiteral("[%1] Launch failed: %2")
                          .arg(profileId, QString::fromUtf8(error.what())));
    }
}

void MainWindow::terminateProfile(const QString& profileId)
{
    ProfileInstance* profile = m_profileManager->profile(profileId);
    if (profile == nullptr) {
        return;
    }
    m_killSwitches.at(profileId)->stopMonitoring();
    profile->terminate();
    addLogMessage(QStringLiteral("[%1] Profile terminated.").arg(profileId));
}

void MainWindow::showCookieInspector(const QString& profileId)
{
    ProfileInstance* profile = m_profileManager->profile(profileId);
    if (profile == nullptr) {
        return;
    }
    auto* dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(QStringLiteral("Cookies — %1").arg(profile->config().name));
    dialog->resize(760, 420);
    auto* layout = new QVBoxLayout(dialog);
    auto* table = new QTableWidget(0, 5, dialog);
    table->setHorizontalHeaderLabels({QStringLiteral("Domain"), QStringLiteral("Name"),
                                      QStringLiteral("Path"), QStringLiteral("Secure"),
                                      QStringLiteral("HTTP Only")});
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(table);

    if (profile->webEngineProfile() == nullptr) {
        layout->insertWidget(0, new QLabel(
            QStringLiteral("Launch the profile before loading its isolated cookie store."), dialog));
    } else {
        QWebEngineCookieStore* store = profile->webEngineProfile()->cookieStore();
        connect(store, &QWebEngineCookieStore::cookieAdded, dialog,
                [table](const QNetworkCookie& cookie) {
                    const int row = table->rowCount();
                    table->insertRow(row);
                    table->setItem(row, 0, new QTableWidgetItem(cookie.domain()));
                    table->setItem(row, 1, new QTableWidgetItem(QString::fromUtf8(cookie.name())));
                    table->setItem(row, 2, new QTableWidgetItem(cookie.path()));
                    table->setItem(row, 3, new QTableWidgetItem(cookie.isSecure()
                                                                   ? QStringLiteral("Yes")
                                                                   : QStringLiteral("No")));
                    table->setItem(row, 4, new QTableWidgetItem(cookie.isHttpOnly()
                                                                   ? QStringLiteral("Yes")
                                                                   : QStringLiteral("No")));
                });
        store->loadAllCookies();
    }
    dialog->show();
}

void MainWindow::exportCookies(const QString& profileId)
{
    ProfileInstance* profile = m_profileManager->profile(profileId);
    if (profile == nullptr || profile->webEngineProfile() == nullptr) {
        addLogMessage(QStringLiteral("[%1] Launch the profile before exporting cookies.").arg(profileId));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Export Cookies"),
        QStringLiteral("%1-cookies.json").arg(profileId), QStringLiteral("JSON (*.json)"));
    if (path.isEmpty()) {
        return;
    }

    auto cookies = std::make_shared<QJsonArray>();
    auto connection = std::make_shared<QMetaObject::Connection>();
    QWebEngineCookieStore* store = profile->webEngineProfile()->cookieStore();
    *connection = connect(store, &QWebEngineCookieStore::cookieAdded, this,
                          [cookies](const QNetworkCookie& cookie) {
                              QJsonObject object;
                              object.insert(QStringLiteral("domain"), cookie.domain());
                              object.insert(QStringLiteral("name"), QString::fromUtf8(cookie.name()));
                              object.insert(QStringLiteral("value"), QString::fromUtf8(cookie.value()));
                              object.insert(QStringLiteral("path"), cookie.path());
                              object.insert(QStringLiteral("secure"), cookie.isSecure());
                              object.insert(QStringLiteral("httpOnly"), cookie.isHttpOnly());
                              cookies->append(object);
                          });
    store->loadAllCookies();
    QTimer::singleShot(300, this, [this, connection, cookies, path, profileId] {
        disconnect(*connection);
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly)
            || file.write(QJsonDocument(*cookies).toJson(QJsonDocument::Indented)) < 0
            || !file.commit()) {
            addLogMessage(QStringLiteral("[%1] Cookie export failed.").arg(profileId));
            return;
        }
        addLogMessage(QStringLiteral("[%1] Exported %2 cookies.")
                          .arg(profileId)
                          .arg(cookies->size()));
    });
}

void MainWindow::deleteProfile(const QString& profileId)
{
    const auto cardIt = m_profileCards.find(profileId);
    if (cardIt == m_profileCards.end()) {
        return;
    }
    ProfileCardWidget* card = cardIt->second;
    const auto engineIt = m_killSwitches.find(profileId);
    if (engineIt != m_killSwitches.end()) {
        engineIt->second->stopMonitoring();
        m_killSwitches.erase(engineIt);
    }
    m_profileGrid->removeWidget(card);
    m_profileCards.erase(cardIt);
    card->deleteLater();
    m_profileManager->removeProfile(profileId);
    for (int row = m_proxyTable->rowCount() - 1; row >= 0; --row) {
        if (m_proxyTable->item(row, 0) != nullptr
            && m_proxyTable->item(row, 0)->data(Qt::UserRole).toString() == profileId) {
            m_proxyTable->removeRow(row);
        }
    }
    m_emptyProfilesLabel->setVisible(m_profileCards.empty());
    addLogMessage(QStringLiteral("[%1] Profile deleted.").arg(profileId));
}

void MainWindow::prepareGeoSync(const QString& profileId)
{
    const ProfileInstance* profile = m_profileManager->profile(profileId);
    if (profile == nullptr) {
        return;
    }
    m_navigation->setCurrentRow(1);
    const QString candidate = profile->config().expectedProxyIp.isEmpty()
        ? profile->config().proxy.hostName()
        : profile->config().expectedProxyIp;
    m_geoIpAddress->setText(candidate);
}

void MainWindow::resolveGeoIp()
{
    const QString databasePath = m_geoDatabasePath->text().trimmed();
    const QString ipAddress = m_geoIpAddress->text().trimmed();
    GeoSyncEngine engine(databasePath.toStdString());
    if (!engine.isOpen()) {
        m_geoResult->setText(QStringLiteral("Lookup unavailable: %1").arg(engine.lastError()));
        return;
    }
    const std::optional<GeoLocationData> location = engine.resolveProxyIp(ipAddress);
    if (!location.has_value()) {
        m_geoResult->setText(QStringLiteral("Lookup failed: %1").arg(engine.lastError()));
        return;
    }
    m_geoResult->setText(
        QStringLiteral("%1 · %2 · %3 · UTC offset %4 minutes")
            .arg(location->countryCode, location->timezone, location->languages.join(','))
            .arg(location->timezoneOffsetMinutes));
}

void MainWindow::addProxyTableRow(const ProfileConfig& config)
{
    const int row = m_proxyTable->rowCount();
    m_proxyTable->insertRow(row);
    const QString type = config.proxy.type() == QNetworkProxy::NoProxy
        ? QStringLiteral("None")
        : config.proxy.type() == QNetworkProxy::Socks5Proxy ? QStringLiteral("SOCKS5")
                                                            : QStringLiteral("HTTP");
    auto* profileItem = new QTableWidgetItem(config.name);
    profileItem->setData(Qt::UserRole, config.id);
    m_proxyTable->setItem(row, 0, profileItem);
    m_proxyTable->setItem(row, 1, new QTableWidgetItem(config.proxy.hostName()));
    m_proxyTable->setItem(row, 2, new QTableWidgetItem(
        config.proxy.port() == 0 ? QStringLiteral("--") : QString::number(config.proxy.port())));
    m_proxyTable->setItem(row, 3, new QTableWidgetItem(type));
    m_proxyTable->setItem(row, 4, new QTableWidgetItem(config.expectedProxyIp));
    QString location = config.countryCode;
    if (!config.timezone.isEmpty()) {
        location = location.isEmpty() ? config.timezone
                                      : QStringLiteral("%1 · %2").arg(location, config.timezone);
    }
    m_proxyTable->setItem(row, 5, new QTableWidgetItem(
        location.isEmpty() ? QStringLiteral("--") : location));
    m_proxyTable->setItem(row, 6, new QTableWidgetItem(QStringLiteral("--")));
}

void MainWindow::populateFetchedProxyPool(const QList<ProxyEndpoint>& proxies)
{
    if (m_fetchedProxyTable == nullptr) {
        return;
    }
    m_fetchedProxyTable->setRowCount(0);
    for (int row = m_proxyTable->rowCount() - 1; row >= 0; --row) {
        QTableWidgetItem* item = m_proxyTable->item(row, 0);
        if (item != nullptr && item->data(Qt::UserRole).toString().startsWith(QStringLiteral("provider:"))) {
            m_proxyTable->removeRow(row);
        }
    }

    for (const ProxyEndpoint& proxy : proxies) {
        const QString type = proxy.type == QNetworkProxy::Socks5Proxy
            ? QStringLiteral("SOCKS5")
            : QStringLiteral("HTTP");
        const QString latency = proxy.reachable
            ? QStringLiteral("%1 ms").arg(proxy.latencyMs)
            : QStringLiteral("Offline");
        const int fetchedRow = m_fetchedProxyTable->rowCount();
        m_fetchedProxyTable->insertRow(fetchedRow);
        m_fetchedProxyTable->setItem(fetchedRow, 0, new QTableWidgetItem(proxy.host));
        m_fetchedProxyTable->setItem(fetchedRow, 1,
                                     new QTableWidgetItem(QString::number(proxy.port)));
        m_fetchedProxyTable->setItem(fetchedRow, 2, new QTableWidgetItem(type));
        m_fetchedProxyTable->setItem(fetchedRow, 3, new QTableWidgetItem(proxy.location()));
        m_fetchedProxyTable->setItem(fetchedRow, 4, new QTableWidgetItem(latency));
        m_fetchedProxyTable->setItem(
            fetchedRow, 5,
            new QTableWidgetItem(proxy.username.isEmpty() ? QStringLiteral("None")
                                                          : QStringLiteral("Username/password")));

        if (!proxy.reachable) {
            continue;
        }
        const int poolRow = m_proxyTable->rowCount();
        m_proxyTable->insertRow(poolRow);
        auto* available = new QTableWidgetItem(QStringLiteral("Available"));
        available->setData(Qt::UserRole, QStringLiteral("provider:%1").arg(proxy.poolKey()));
        m_proxyTable->setItem(poolRow, 0, available);
        m_proxyTable->setItem(poolRow, 1, new QTableWidgetItem(proxy.host));
        m_proxyTable->setItem(poolRow, 2, new QTableWidgetItem(QString::number(proxy.port)));
        m_proxyTable->setItem(poolRow, 3, new QTableWidgetItem(type));
        m_proxyTable->setItem(poolRow, 4, new QTableWidgetItem(QStringLiteral("--")));
        m_proxyTable->setItem(poolRow, 5, new QTableWidgetItem(proxy.location()));
        m_proxyTable->setItem(poolRow, 6, new QTableWidgetItem(latency));
    }
    m_navigation->setCurrentRow(2);
}

void MainWindow::handleContainment(const QString& profileId, bool contained, NetworkStatus status)
{
    const ProfileInstance* profile = m_profileManager->profile(profileId);
    if (profile == nullptr) {
        return;
    }
    if (ProfileCardWidget* card = profileCard(profileId); card != nullptr) {
        card->setProxyStatus(!contained, -1, profile->config().expectedProxyIp);
    }
    if (contained || !hasContainedProfile()) {
        updateNetworkStatus(contained ? status : NetworkStatus::Healthy,
                            profile->config().expectedProxyIp);
    }
    addLogMessage(contained
        ? QStringLiteral("[%1] Network containment activated: %2")
              .arg(profileId, networkStatusName(status))
        : QStringLiteral("[%1] Network containment cleared.").arg(profileId));
}

bool MainWindow::hasContainedProfile() const noexcept
{
    for (const auto& [id, engine] : m_killSwitches) {
        Q_UNUSED(id);
        if (engine->isContained()) {
            return true;
        }
    }
    return false;
}

void MainWindow::addLogMessage(const QString& message)
{
    m_logs->appendPlainText(message);
}

void MainWindow::updateNetworkStatus(NetworkStatus status, const QString& proxyIp, int latencyMs)
{
    m_latencyStatus->setText(latencyMs < 0 ? QStringLiteral("Ping: -- ms")
                                          : QStringLiteral("Ping: %1 ms").arg(latencyMs));
    m_proxyStatus->setText(QStringLiteral("Proxy IP: %1")
                               .arg(proxyIp.isEmpty() ? QStringLiteral("--") : proxyIp));
    const bool secure = status == NetworkStatus::Healthy;
    m_killSwitchStatus->setText(secure ? QStringLiteral("SECURE") : QStringLiteral("INTERRUPTED"));
    m_killSwitchStatus->setStyleSheet(secure
        ? QStringLiteral("QLabel { color: #7ee787; font-weight: bold; padding: 3px 8px; }")
        : QStringLiteral("QLabel { color: #ff7b72; font-weight: bold; padding: 3px 8px; }"));
    m_killSwitchStatus->setToolTip(networkStatusName(status));
}

QString MainWindow::networkStatusName(NetworkStatus status)
{
    switch (status) {
    case NetworkStatus::Healthy:
        return QStringLiteral("Healthy");
    case NetworkStatus::ProxyDegraded:
        return QStringLiteral("Proxy degraded");
    case NetworkStatus::HardwareDisconnected:
        return QStringLiteral("Hardware disconnected");
    case NetworkStatus::LeakedRisk:
        return QStringLiteral("Leak risk");
    }
    return QStringLiteral("Unknown");
}
