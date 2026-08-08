#include "core/CustomUrlInterceptor.hpp"

#include <QWebEngineUrlRequestInfo>

CustomUrlInterceptor::CustomUrlInterceptor(QObject* parent)
    : QWebEngineUrlRequestInterceptor(parent)
{
}

void CustomUrlInterceptor::interceptRequest(QWebEngineUrlRequestInfo& info)
{
    if (shouldBlock(info.requestUrl())) {
        info.block(true);
    }
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

