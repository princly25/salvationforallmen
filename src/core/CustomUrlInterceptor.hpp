#pragma once

#include <QUrl>
#include <QWebEngineUrlRequestInterceptor>

#include <atomic>
#include <cstdint>

class CustomUrlInterceptor final : public QWebEngineUrlRequestInterceptor {
    Q_OBJECT

public:
    explicit CustomUrlInterceptor(QObject* parent = nullptr);

    void interceptRequest(QWebEngineUrlRequestInfo& info) override;
    void setNetworkFrozen(bool frozen) noexcept;
    [[nodiscard]] bool isNetworkFrozen() const noexcept;
    [[nodiscard]] bool shouldBlock(const QUrl& url) const noexcept;
    [[nodiscard]] std::uint64_t blockedRequestCount() const noexcept;

private:
    std::atomic_bool m_networkFrozen{false};
    std::atomic_uint64_t m_blockedRequestCount{0};
};
