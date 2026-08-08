#include "network/ProxyFetcher.hpp"
#include "network/NetworkStackPolicy.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSet>
#include <QTcpSocket>
#include <QTimer>

#include <algorithm>
#include <utility>

namespace {
QString firstString(const QJsonObject& object, std::initializer_list<const char*> keys)
{
    for (const char* key : keys) {
        const QJsonValue value = object.value(QLatin1String(key));
        if (value.isString() && !value.toString().trimmed().isEmpty()) {
            return value.toString().trimmed();
        }
        if (value.isDouble()) {
            return QString::number(value.toInteger());
        }
    }
    return {};
}

QJsonArray findProxyArray(const QJsonValue& value)
{
    if (value.isArray()) {
        return value.toArray();
    }
    if (!value.isObject()) {
        return {};
    }
    const QJsonObject object = value.toObject();
    for (const char* key : {"results", "proxies", "items", "data"}) {
        const QJsonValue candidate = object.value(QLatin1String(key));
        if (candidate.isArray()) {
            return candidate.toArray();
        }
        if (candidate.isObject()) {
            const QJsonArray nested = findProxyArray(candidate);
            if (!nested.isEmpty()) {
                return nested;
            }
        }
    }
    return {};
}

QNetworkProxy::ProxyType proxyTypeFromString(const QString& value)
{
    const QString normalized = value.trimmed().toLower();
    return normalized.contains(QStringLiteral("socks"))
        ? QNetworkProxy::Socks5Proxy
        : QNetworkProxy::HttpProxy;
}

bool parseProxyString(const QString& value, ProxyEndpoint& endpoint)
{
    const QString trimmed = value.trimmed();
    if (trimmed.contains(QStringLiteral("://"))) {
        const QUrl url(trimmed);
        if (!url.isValid() || url.host().isEmpty() || url.port() <= 0 || url.port() > 65535) {
            return false;
        }
        endpoint.host = url.host();
        endpoint.port = static_cast<quint16>(url.port());
        endpoint.username = url.userName();
        endpoint.password = url.password();
        endpoint.type = proxyTypeFromString(url.scheme());
        return true;
    }

    const QStringList fields = trimmed.split(QChar(':'), Qt::KeepEmptyParts);
    if (fields.size() < 2) {
        return false;
    }
    bool portOk = false;
    const int port = fields.at(1).toInt(&portOk);
    if (!portOk || port <= 0 || port > 65535 || fields.at(0).trimmed().isEmpty()) {
        return false;
    }
    endpoint.host = fields.at(0).trimmed();
    endpoint.port = static_cast<quint16>(port);
    if (fields.size() > 2) {
        endpoint.username = fields.at(2);
    }
    if (fields.size() > 3) {
        endpoint.password = fields.at(3);
    }
    return true;
}

bool parseProxyObject(const QJsonObject& object, ProxyEndpoint& endpoint,
                      ProxyProviderManager::ProviderFormat format)
{
    endpoint.providerId = firstString(object, {"id", "uuid", "proxy_id"});
    endpoint.host = firstString(object, {"proxy_address", "host", "hostname", "ip", "server"});
    const QString portText = firstString(object, {"port", "proxy_port"});
    bool portOk = false;
    const int port = portText.toInt(&portOk);
    if (endpoint.host.isEmpty() || !portOk || port <= 0 || port > 65535) {
        const QString combined = firstString(object, {"proxy", "url", "endpoint"});
        if (!parseProxyString(combined, endpoint)) {
            return false;
        }
    } else {
        endpoint.port = static_cast<quint16>(port);
    }

    const QString username = firstString(object, {"username", "user", "login"});
    const QString password = firstString(object, {"password", "pass"});
    if (!username.isEmpty()) {
        endpoint.username = username;
    }
    if (!password.isEmpty()) {
        endpoint.password = password;
    }
    endpoint.country = firstString(object, {"country_code", "country", "countryCode"});
    endpoint.city = firstString(object, {"city", "region", "location"});
    endpoint.type = proxyTypeFromString(firstString(object, {"protocol", "type", "scheme"}));
    if (format == ProxyProviderManager::ProviderFormat::IPRoyal
        && firstString(object, {"protocol", "type", "scheme"}).isEmpty()) {
        endpoint.type = QNetworkProxy::HttpProxy;
    }
    return true;
}
}

QString ProxyEndpoint::location() const
{
    if (city.isEmpty()) {
        return country;
    }
    if (country.isEmpty()) {
        return city;
    }
    return QStringLiteral("%1, %2").arg(city, country);
}

QString ProxyEndpoint::poolKey() const
{
    return QStringLiteral("%1|%2|%3|%4")
        .arg(static_cast<int>(type))
        .arg(host.toLower())
        .arg(port)
        .arg(username);
}

ProxyProviderManager::ProxyProviderManager(QObject* parent)
    : QObject(parent)
{
    qRegisterMetaType<QList<ProxyEndpoint>>();
}

ProxyProviderManager::~ProxyProviderManager()
{
    cancel();
}

void ProxyProviderManager::fetchProxies(const QUrl& apiUrl, const QString& apiToken,
                                        ProviderFormat format)
{
    cancel();
    const QString scheme = apiUrl.scheme().toLower();
    if (!apiUrl.isValid() || (scheme != QStringLiteral("http") && scheme != QStringLiteral("https"))) {
        emit fetchFailed(QStringLiteral("The provider API URL must use HTTP or HTTPS."));
        return;
    }

    m_pendingFormat = inferFormat(apiUrl, format);
    QNetworkRequest request(apiUrl);
    NetworkStackPolicy::apply(request);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("AntiDetectBrowser/0.1 ProxyProviderManager"));
    request.setRawHeader("Accept", "application/json");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(10000);

    const QByteArray token = apiToken.trimmed().toUtf8();
    if (!token.isEmpty()) {
        QByteArray authorization = token;
        if (!token.startsWith("Bearer ") && !token.startsWith("Token ")) {
            authorization = m_pendingFormat == ProviderFormat::Webshare
                ? QByteArray("Token ") + token
                : QByteArray("Bearer ") + token;
        }
        request.setRawHeader("Authorization", authorization);
        request.setRawHeader("X-API-Key", token);
    }

    emit fetchStarted(apiUrl);
    m_pendingReply = m_network.get(request);
    connect(m_pendingReply, &QNetworkReply::finished,
            this, &ProxyProviderManager::onFetchFinished);
}

void ProxyProviderManager::onFetchFinished()
{
    QNetworkReply* reply = m_pendingReply;
    m_pendingReply = nullptr;
    if (reply == nullptr) {
        return;
    }
    const QByteArray payload = reply->readAll();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const bool success = reply->error() == QNetworkReply::NoError && status >= 200 && status < 300;
    const QString networkError = reply->errorString();
    reply->deleteLater();
    if (!success) {
        emit fetchFailed(QStringLiteral("Provider request failed (%1): %2")
                             .arg(status)
                             .arg(networkError));
        return;
    }

    QString parseError;
    const QList<ProxyEndpoint> proxies = parseProxyPayload(payload, &parseError, m_pendingFormat);
    if (proxies.isEmpty()) {
        emit fetchFailed(parseError.isEmpty() ? QStringLiteral("The provider returned no valid proxies.")
                                              : parseError);
        return;
    }
    emit proxiesFetched(proxies);
}

QList<ProxyEndpoint>
ProxyProviderManager::parseProxyPayload(const QByteArray& payload, QString* error,
                                        ProviderFormat format)
{
    if (error != nullptr) {
        error->clear();
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (error != nullptr) {
            *error = QStringLiteral("Invalid provider JSON: %1").arg(parseError.errorString());
        }
        return {};
    }

    const QJsonValue root = document.isArray() ? QJsonValue(document.array())
                                               : QJsonValue(document.object());
    const QJsonArray entries = findProxyArray(root);
    if (entries.isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("The provider JSON does not contain a proxy array.");
        }
        return {};
    }

    QList<ProxyEndpoint> proxies;
    QSet<QString> seen;
    for (const QJsonValue& entry : entries) {
        ProxyEndpoint endpoint;
        bool valid = false;
        if (entry.isObject()) {
            valid = parseProxyObject(entry.toObject(), endpoint, format);
        } else if (entry.isString()) {
            valid = parseProxyString(entry.toString(), endpoint);
        }
        if (!valid || seen.contains(endpoint.poolKey())) {
            continue;
        }
        seen.insert(endpoint.poolKey());
        proxies.append(endpoint);
    }
    if (proxies.isEmpty() && error != nullptr) {
        *error = QStringLiteral("No valid host and port pairs were found in the provider response.");
    }
    return proxies;
}

void ProxyProviderManager::testLatencies(const QList<ProxyEndpoint>& proxies, int timeoutMs,
                                         int maximumConcurrency)
{
    for (auto& [socket, probe] : m_activeProbes) {
        Q_UNUSED(probe);
        socket->abort();
        socket->deleteLater();
    }
    m_activeProbes.clear();
    m_latencyQueue = proxies;
    m_latencyResults.clear();
    m_latencyTimeoutMs = std::clamp(timeoutMs, 100, 30000);
    m_maximumConcurrency = std::clamp(maximumConcurrency, 1, 64);
    m_latencyTotal = proxies.size();
    if (proxies.isEmpty()) {
        m_activeProxyPool.clear();
        emit latencyTestsFinished({});
        return;
    }
    startNextLatencyChecks();
}

void ProxyProviderManager::startNextLatencyChecks()
{
    while (!m_latencyQueue.isEmpty()
           && static_cast<int>(m_activeProbes.size()) < m_maximumConcurrency) {
        ProxyEndpoint endpoint = m_latencyQueue.takeFirst();
        auto* socket = new QTcpSocket(this);
        auto probe = std::make_unique<LatencyProbe>();
        probe->endpoint = endpoint;
        probe->timer.start();
        m_activeProbes.emplace(socket, std::move(probe));
        connect(socket, &QTcpSocket::connected, this,
                [this, socket] { finishLatencyCheck(socket, true); });
        connect(socket, &QTcpSocket::errorOccurred, this,
                [this, socket](QAbstractSocket::SocketError) {
                    finishLatencyCheck(socket, false);
                });
        QTimer::singleShot(m_latencyTimeoutMs, socket,
                           [this, socket] { finishLatencyCheck(socket, false); });
        socket->connectToHost(endpoint.host, endpoint.port);
    }
}

void ProxyProviderManager::finishLatencyCheck(QTcpSocket* socket, bool reachable)
{
    const auto found = m_activeProbes.find(socket);
    if (found == m_activeProbes.end()) {
        return;
    }
    ProxyEndpoint endpoint = found->second->endpoint;
    endpoint.reachable = reachable;
    endpoint.latencyMs = reachable ? static_cast<int>(found->second->timer.elapsed()) : -1;
    m_activeProbes.erase(found);
    socket->abort();
    socket->deleteLater();
    m_latencyResults.append(endpoint);
    emit latencyProgress(m_latencyResults.size(), m_latencyTotal);
    startNextLatencyChecks();

    if (m_latencyQueue.isEmpty() && m_activeProbes.empty()) {
        std::sort(m_latencyResults.begin(), m_latencyResults.end(),
                  [](const ProxyEndpoint& left, const ProxyEndpoint& right) {
                      if (left.reachable != right.reachable) {
                          return left.reachable;
                      }
                      if (left.latencyMs != right.latencyMs) {
                          return left.latencyMs < right.latencyMs;
                      }
                      return left.host < right.host;
                  });
        m_activeProxyPool.clear();
        for (const ProxyEndpoint& proxy : std::as_const(m_latencyResults)) {
            if (proxy.reachable) {
                m_activeProxyPool.append(proxy);
            }
        }
        emit latencyTestsFinished(m_latencyResults);
    }
}

void ProxyProviderManager::cancel()
{
    if (m_pendingReply != nullptr) {
        m_pendingReply->abort();
        m_pendingReply->deleteLater();
        m_pendingReply = nullptr;
    }
    for (auto& [socket, probe] : m_activeProbes) {
        Q_UNUSED(probe);
        socket->abort();
        socket->deleteLater();
    }
    m_activeProbes.clear();
    m_latencyQueue.clear();
    m_latencyResults.clear();
    m_latencyTotal = 0;
}

bool ProxyProviderManager::isBusy() const noexcept
{
    return m_pendingReply != nullptr || !m_activeProbes.empty() || !m_latencyQueue.isEmpty();
}

const QList<ProxyEndpoint>& ProxyProviderManager::activeProxyPool() const noexcept
{
    return m_activeProxyPool;
}

ProxyProviderManager::ProviderFormat
ProxyProviderManager::inferFormat(const QUrl& apiUrl, ProviderFormat requested)
{
    if (requested != ProviderFormat::AutoDetect) {
        return requested;
    }
    const QString host = apiUrl.host().toLower();
    if (host.contains(QStringLiteral("webshare"))) {
        return ProviderFormat::Webshare;
    }
    if (host.contains(QStringLiteral("iproyal"))) {
        return ProviderFormat::IPRoyal;
    }
    return ProviderFormat::Custom;
}
