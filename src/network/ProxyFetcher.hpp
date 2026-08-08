#pragma once

#include <QElapsedTimer>
#include <QList>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>

#include <map>
#include <memory>

class QNetworkReply;
class QTcpSocket;

struct ProxyEndpoint {
    QString providerId;
    QString host;
    quint16 port{0};
    QString username;
    QString password;
    QString country;
    QString city;
    QNetworkProxy::ProxyType type{QNetworkProxy::HttpProxy};
    int latencyMs{-1};
    bool reachable{false};

    [[nodiscard]] QString location() const;
    [[nodiscard]] QString poolKey() const;
    bool operator==(const ProxyEndpoint&) const = default;
};

Q_DECLARE_METATYPE(ProxyEndpoint)
Q_DECLARE_METATYPE(QList<ProxyEndpoint>)

class ProxyProviderManager final : public QObject {
    Q_OBJECT

public:
    enum class ProviderFormat { AutoDetect, Webshare, IPRoyal, Custom };
    Q_ENUM(ProviderFormat)

    explicit ProxyProviderManager(QObject* parent = nullptr);
    ~ProxyProviderManager() override;

    void fetchProxies(const QUrl& apiUrl, const QString& apiToken,
                      ProviderFormat format = ProviderFormat::AutoDetect);
    void testLatencies(const QList<ProxyEndpoint>& proxies, int timeoutMs = 1500,
                       int maximumConcurrency = 8);
    void cancel();

    [[nodiscard]] bool isBusy() const noexcept;
    [[nodiscard]] const QList<ProxyEndpoint>& activeProxyPool() const noexcept;

    [[nodiscard]] static QList<ProxyEndpoint>
    parseProxyPayload(const QByteArray& payload, QString* error = nullptr,
                      ProviderFormat format = ProviderFormat::AutoDetect);

signals:
    void fetchStarted(const QUrl& apiUrl);
    void proxiesFetched(const QList<ProxyEndpoint>& proxies);
    void fetchFailed(const QString& error);
    void latencyProgress(int completed, int total);
    void latencyTestsFinished(const QList<ProxyEndpoint>& proxies);

private:
    struct LatencyProbe {
        ProxyEndpoint endpoint;
        QElapsedTimer timer;
    };

    void onFetchFinished();
    void startNextLatencyChecks();
    void finishLatencyCheck(QTcpSocket* socket, bool reachable);
    static ProviderFormat inferFormat(const QUrl& apiUrl, ProviderFormat requested);

    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_pendingReply;
    ProviderFormat m_pendingFormat{ProviderFormat::AutoDetect};
    QList<ProxyEndpoint> m_latencyQueue;
    QList<ProxyEndpoint> m_latencyResults;
    QList<ProxyEndpoint> m_activeProxyPool;
    std::map<QTcpSocket*, std::unique_ptr<LatencyProbe>> m_activeProbes;
    int m_latencyTimeoutMs{1500};
    int m_maximumConcurrency{8};
    int m_latencyTotal{0};
};
