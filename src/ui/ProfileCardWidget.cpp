#include "ui/ProfileCardWidget.hpp"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSizePolicy>
#include <QVBoxLayout>

#include <algorithm>
#include <stdexcept>

ProfileCardWidget::ProfileCardWidget(ProfileInstance* profile, QWidget* parent)
    : QFrame(parent)
    , m_profile(profile)
{
    if (m_profile == nullptr) {
        throw std::invalid_argument("ProfileCardWidget requires a profile");
    }

    setObjectName(QStringLiteral("profileCard_%1").arg(m_profile->config().id));
    setProperty("profileCard", true);
    setFrameShape(QFrame::NoFrame);
    setMinimumSize(360, 248);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 18, 18, 16);
    layout->setSpacing(10);

    auto* header = new QHBoxLayout;
    auto* title = new QLabel(m_profile->config().name, this);
    QFont titleFont = title->font();
    titleFont.setBold(true);
    titleFont.setPointSize(titleFont.pointSize() + 2);
    title->setFont(titleFont);
    title->setObjectName(QStringLiteral("profileName"));
    header->addWidget(title);
    header->addStretch(1);
    m_proxyStatusDot = new QLabel(this);
    m_proxyStatusDot->setObjectName(QStringLiteral("proxyStatusDot"));
    m_proxyStatusDot->setFixedSize(10, 10);
    header->addWidget(m_proxyStatusDot);
    m_proxyStatusLabel = new QLabel(this);
    m_proxyStatusLabel->setObjectName(QStringLiteral("proxyStatus"));
    header->addWidget(m_proxyStatusLabel);
    layout->addLayout(header);

    auto* identifier = new QLabel(QStringLiteral("ID: %1").arg(m_profile->config().id), this);
    identifier->setObjectName(QStringLiteral("profileId"));
    identifier->setStyleSheet(QStringLiteral("color: #9CA3AF;"));
    layout->addWidget(identifier);

    auto* details = new QGridLayout;
    details->setHorizontalSpacing(14);
    details->setVerticalSpacing(6);
    auto addDetail = [details, this](const QString& caption, QLabel*& value, int row, int column) {
        auto* label = new QLabel(caption, this);
        label->setStyleSheet(QStringLiteral("color: #9CA3AF; font-size: 11px;"));
        details->addWidget(label, row, column * 2);
        value = new QLabel(this);
        value->setWordWrap(true);
        details->addWidget(value, row, column * 2 + 1);
    };
    addDetail(QStringLiteral("Assigned proxy"), m_proxyLocationLabel, 0, 0);
    addDetail(QStringLiteral("PRNG seed"), m_seedLabel, 0, 1);
    addDetail(QStringLiteral("Cookies"), m_cookieCountLabel, 1, 0);
    m_proxyLocationLabel->setObjectName(QStringLiteral("proxyLocation"));
    m_seedLabel->setObjectName(QStringLiteral("prngSeed"));
    m_cookieCountLabel->setObjectName(QStringLiteral("cookieCount"));
    auto* platformLabel = new QLabel(QStringLiteral("Platform"), this);
    platformLabel->setStyleSheet(QStringLiteral("color: #9CA3AF; font-size: 11px;"));
    auto* platformValue = new QLabel(platformSummary(m_profile->config().userAgent), this);
    details->addWidget(platformLabel, 1, 2);
    details->addWidget(platformValue, 1, 3);
    layout->addLayout(details);

    m_stateLabel = new QLabel(this);
    m_stateLabel->setObjectName(QStringLiteral("profileState"));
    layout->addWidget(m_stateLabel);

    auto* actionLayout = new QGridLayout;
    m_launchButton = new QPushButton(QStringLiteral("Launch Profile"), this);
    m_launchButton->setObjectName(QStringLiteral("launchProfile"));
    auto* syncButton = new QPushButton(QStringLiteral("Sync Proxy"), this);
    syncButton->setObjectName(QStringLiteral("syncProxy"));
    auto* inspectCookiesButton = new QPushButton(QStringLiteral("Inspect Cookies"), this);
    inspectCookiesButton->setObjectName(QStringLiteral("inspectCookies"));
    auto* cookiesButton = new QPushButton(QStringLiteral("Export Cookies"), this);
    cookiesButton->setObjectName(QStringLiteral("exportCookies"));
    auto* deleteButton = new QPushButton(QStringLiteral("Delete"), this);
    deleteButton->setObjectName(QStringLiteral("deleteProfile"));
    m_freezeButton = new QPushButton(QStringLiteral("Freeze"), this);
    m_freezeButton->setObjectName(QStringLiteral("freezeProfile"));
    m_terminateButton = new QPushButton(QStringLiteral("Terminate"), this);
    m_terminateButton->setObjectName(QStringLiteral("terminateProfile"));

    actionLayout->addWidget(m_launchButton, 0, 0);
    actionLayout->addWidget(syncButton, 0, 1);
    actionLayout->addWidget(inspectCookiesButton, 1, 0);
    actionLayout->addWidget(cookiesButton, 1, 1);
    actionLayout->addWidget(deleteButton, 2, 0);
    actionLayout->addWidget(m_freezeButton, 2, 1);
    actionLayout->addWidget(m_terminateButton, 3, 0, 1, 2);
    layout->addLayout(actionLayout);

    connect(m_launchButton, &QPushButton::clicked, this, [this] {
        emit launchRequested(m_profile->config().id);
    });
    connect(m_freezeButton, &QPushButton::clicked, this, &ProfileCardWidget::toggleFreeze);
    connect(m_terminateButton, &QPushButton::clicked, this, [this] {
        emit terminateRequested(m_profile->config().id);
    });
    connect(syncButton, &QPushButton::clicked, this, [this] {
        emit geoSyncRequested(m_profile->config().id);
    });
    connect(inspectCookiesButton, &QPushButton::clicked, this, [this] {
        emit cookieInspectorRequested(m_profile->config().id);
    });
    connect(cookiesButton, &QPushButton::clicked, this, [this] {
        emit exportCookiesRequested(m_profile->config().id);
    });
    connect(deleteButton, &QPushButton::clicked, this, [this] {
        emit deleteRequested(m_profile->config().id);
    });
    m_proxyLocationLabel->setText(proxySummary(m_profile->config().proxy));
    m_seedLabel->setText(seedSummary(m_profile->config().hardware.masterSeedHex));
    setCookieCount(0);
    setProxyStatus(false, -1, m_profile->config().expectedProxyIp);
    connect(m_profile, &ProfileInstance::stateChanged,
            this, &ProfileCardWidget::refreshState);
    refreshState(m_profile->state());
}

ProfileInstance* ProfileCardWidget::profile() const noexcept
{
    return m_profile;
}

void ProfileCardWidget::setProxyStatus(bool reachable, int latencyMs, const QString& location)
{
    m_proxyStatusDot->setStyleSheet(QStringLiteral("border-radius: 5px; background: %1;")
                                        .arg(reachable ? QStringLiteral("#22C55E")
                                                       : QStringLiteral("#EF4444")));
    m_proxyStatusLabel->setText(reachable
                                    ? (latencyMs < 0 ? QStringLiteral("Online")
                                                     : QStringLiteral("Online · %1 ms").arg(latencyMs))
                                    : QStringLiteral("Offline"));
    if (!location.isEmpty()) {
        m_proxyLocationLabel->setText(location);
    }
}

void ProfileCardWidget::setCookieCount(int count)
{
    m_cookieCountLabel->setText(QString::number(std::max(0, count)));
}

void ProfileCardWidget::refreshState(ProfileInstance::State state)
{
    m_stateLabel->setText(QStringLiteral("Status: %1").arg(stateName(state)));
    const bool terminated = state == ProfileInstance::State::Terminated;
    m_launchButton->setEnabled(state == ProfileInstance::State::Ready);
    m_freezeButton->setEnabled(!terminated);
    m_freezeButton->setText(state == ProfileInstance::State::Frozen
                                ? QStringLiteral("Unfreeze")
                                : QStringLiteral("Freeze"));
    m_terminateButton->setEnabled(!terminated);
}

void ProfileCardWidget::toggleFreeze()
{
    if (m_profile->state() == ProfileInstance::State::Frozen) {
        m_profile->unfreezeNetworkAccess();
    } else {
        m_profile->freezeNetworkAccess();
    }
}

QString ProfileCardWidget::stateName(ProfileInstance::State state)
{
    switch (state) {
    case ProfileInstance::State::Ready:
        return QStringLiteral("READY");
    case ProfileInstance::State::Running:
        return QStringLiteral("RUNNING");
    case ProfileInstance::State::Frozen:
        return QStringLiteral("FROZEN");
    case ProfileInstance::State::Terminated:
        return QStringLiteral("TERMINATED");
    }
    return QStringLiteral("UNKNOWN");
}

QString ProfileCardWidget::proxySummary(const QNetworkProxy& proxy)
{
    if (proxy.type() == QNetworkProxy::NoProxy) {
        return QStringLiteral("Proxy: not configured");
    }
    return QStringLiteral("Proxy: %1:%2").arg(proxy.hostName()).arg(proxy.port());
}

QString ProfileCardWidget::seedSummary(const std::string& seed)
{
    const QString value = QString::fromStdString(seed);
    return value.size() > 16 ? value.left(16) + QChar(0x2026) : value;
}

QString ProfileCardWidget::platformSummary(const QString& userAgent)
{
    if (userAgent.contains(QStringLiteral("Windows"), Qt::CaseInsensitive)) {
        return QStringLiteral("Windows");
    }
    if (userAgent.contains(QStringLiteral("Macintosh"), Qt::CaseInsensitive)) {
        return QStringLiteral("macOS");
    }
    return QStringLiteral("Linux");
}
