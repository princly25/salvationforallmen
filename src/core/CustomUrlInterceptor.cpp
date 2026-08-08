#include "core/CustomUrlInterceptor.hpp"

#include <QRegularExpression>
#include <QWebEngineUrlRequestInfo>

CustomUrlInterceptor::CustomUrlInterceptor(QObject* parent)
    : QWebEngineUrlRequestInterceptor(parent)
{
}

CustomUrlInterceptor::CustomUrlInterceptor(const QString& userAgent, const QString& platform,
                                           QObject* parent)
    : QWebEngineUrlRequestInterceptor(parent)
{
    setClientHints(userAgent, platform);
}

void CustomUrlInterceptor::interceptRequest(QWebEngineUrlRequestInfo& info)
{
    if (shouldBlock(info.requestUrl())) {
        m_blockedRequestCount.fetch_add(1, std::memory_order_relaxed);
        info.block(true);
        return;
    }

    // Chromium sends client hints on Google navigations before origin policy
    // negotiation has completed. Qt exposes these headers through the request
    // interceptor, which keeps the behavior deterministic for every profile.
    const auto resourceType = info.resourceType();
    const bool navigation = resourceType == QWebEngineUrlRequestInfo::ResourceTypeMainFrame
        || resourceType == QWebEngineUrlRequestInfo::ResourceTypeSubFrame
        || resourceType == QWebEngineUrlRequestInfo::ResourceTypeNavigationPreloadMainFrame
        || resourceType == QWebEngineUrlRequestInfo::ResourceTypeNavigationPreloadSubFrame;
    if (navigation && isGoogleService(info.requestUrl()) && !m_clientHintsUserAgent.isEmpty()) {
        info.setHttpHeader("Sec-CH-UA", m_clientHintsUserAgent);
        info.setHttpHeader("Sec-CH-UA-Mobile", m_clientHintsMobile);
        info.setHttpHeader("Sec-CH-UA-Platform", m_clientHintsPlatform);
    }
}

void CustomUrlInterceptor::setClientHints(const QString& userAgent, const QString& platform)
{
    const QRegularExpression chromeVersion(
        QStringLiteral("(?:Chrome|Chromium)/([0-9]+)(?:\\.[0-9]+)*"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = chromeVersion.match(userAgent);
    if (!match.hasMatch()) {
        m_clientHintsUserAgent.clear();
        return;
    }
    const QByteArray major = match.captured(1).toUtf8();
    m_clientHintsUserAgent = QByteArrayLiteral("\"Not/A)Brand\";v=\"8\", \"Chromium\";v=\"")
        + major + QByteArrayLiteral("\"");
    if (userAgent.contains(QStringLiteral("Chrome/"), Qt::CaseInsensitive)) {
        m_clientHintsUserAgent += QByteArrayLiteral(", \"Google Chrome\";v=\"")
            + major + QByteArrayLiteral("\"");
    }
    m_clientHintsPlatform = QByteArrayLiteral("\"") + platform.toUtf8() + QByteArrayLiteral("\"");
    m_clientHintsMobile = userAgent.contains(QStringLiteral("Mobile"), Qt::CaseInsensitive)
        ? QByteArrayLiteral("?1")
        : QByteArrayLiteral("?0");
}

bool CustomUrlInterceptor::isGoogleService(const QUrl& url) const noexcept
{
    const QString host = url.host().toLower();
    for (const QString& suffix : {QStringLiteral("google.com"), QStringLiteral("googleapis.com"),
                                  QStringLiteral("gstatic.com"), QStringLiteral("googleusercontent.com"),
                                  QStringLiteral("youtube.com")}) {
        if (host == suffix || host.endsWith(QStringLiteral(".") + suffix)) {
            return true;
        }
    }
    return false;
}

void CustomUrlInterceptor::setNetworkFrozen(bool frozen) noexcept
{
    m_networkFrozen.store(frozen, std::memory_order_release);
}

bool CustomUrlInterceptor::isNetworkFrozen() const noexcept
{
    return m_networkFrozen.load(std::memory_order_acquire);
}

bool CustomUrlInterceptor::shouldBlock(const QUrl& url) const noexcept
{
    if (isNetworkFrozen()) {
        return true;
    }

    const QString scheme = url.scheme().toLower();
    return scheme == QStringLiteral("file") || scheme == QStringLiteral("qrc");
}

std::uint64_t CustomUrlInterceptor::blockedRequestCount() const noexcept
{
    return m_blockedRequestCount.load(std::memory_order_relaxed);
}
