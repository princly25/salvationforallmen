# `SPEC.md` — Native C++20 / Qt6 Standalone Desktop Browser Specification

**Product objective:** Build a standalone desktop software application for Windows 10/11 using C++20 and Qt6 WebEngine. The application focuses exclusively on isolated browser profiles, deterministic fingerprint configuration, proxy-aware geo synchronization, proactive network containment, and a native desktop dashboard. It contains no autonomous agent, mouse/keyboard automation, or machine-learning integration.

The project must also configure, compile, and run its core tests natively in a resource-constrained, headless Ubuntu Codespace. Windows-only integrations are guarded with `#ifdef _WIN32`; Linux builds use portable implementations or explicit no-op stubs.

## 1. System Architecture & Tech Stack

* **Language Standard**: C++20 (`-std=c++20` / `/std:c++20`)
* **Framework**: Qt 6.4+ (Qt WebEngine, Qt Widgets, Qt Network, Qt Test)
* **API Hooking Engine**: MinHook v1.3.3 on Windows only
* **JSON Serialization**: `nlohmann/json` v3.11+
* **Cryptography & Hashing**: OpenSSL v3.x (`libcrypto`)
* **Geo-Localization**: MaxMind `libmaxminddb`
* **Target Platform**: Windows 10/11 x64 (MSVC 2022 / Clang-CL), with a native headless Linux validation build
* **Isolation Model**: Dynamic heap-allocated `QWebEngineProfile` instances with strictly isolated on-disk storage directories. No shared caches, persistent cookies, indexedDB instances, or WebGL contexts.
* **Fail-Safe Mechanism**: Asynchronous dual-layer Network Monitor (OS Kernel Callback via `QNetworkInformation` + 50ms Proxy TCP Heartbeat Ping) linked to an atomic Kill Switch and Auto-Resumption Engine with pre-flight IP verification.

---

## 2. Directory & Component Structure

```text
├── CMakeLists.txt
├── config/
│   └── profile_schema.json
├── data/
│   └── GeoLite2-City.mmdb
├── src/
│   ├── main.cpp
│   ├── core/
│   │   ├── ProfileManager.hpp / .cpp
│   │   ├── ProfileInstance.hpp / .cpp
│   │   ├── CustomUrlInterceptor.hpp / .cpp
│   │   └── ProfileValidator.hpp / .cpp
│   ├── crypto/
│   │   └── ProfileSeedEngine.hpp / .cpp
│   ├── geo/
│   │   └── GeoSyncEngine.hpp / .cpp
│   ├── hooks/
│   │   ├── MinHookManager.hpp / .cpp
│   │   └── FingerprintEngine.hpp / .cpp
│   ├── network/
│   │   ├── NetworkMonitor.hpp / .cpp
│   │   └── KillSwitchEngine.hpp / .cpp
│   └── ui/
│       ├── MainWindow.hpp / .cpp
│       └── ProfileCardWidget.hpp / .cpp

```

---

## 3. Detailed Subsystem Specifications

> **Scope exclusion:** Module 5 (Autonomous AI Agent Interface) has been removed
> from the product architecture and build scope. The application must not include
> autonomous-agent code, mouse/keyboard trajectory automation, machine-learning
> runtimes, or related dependency hooks.

### Module 1: Profile Sandbox & Isolation (`src/core/`)

#### 1.1 Header Specification (`ProfileInstance.hpp`)

```cpp
#pragma once
#include <QString>
#include <QWebEngineProfile>
#include <QWebEnginePage>
#include <QWebEngineView>
#include <QNetworkProxy>
#include <memory>
#include <nlohmann/json.hpp>

struct HardwareFingerprint {
    int cpu_cores{8};
    int memory_gb{16};
    int screen_width{1920};
    int screen_height{1080};
    std::string webgl_vendor;
    std::string webgl_renderer;
    std::string master_seed_hex; // 64-character hex seed
};

struct ProfileConfig {
    QString id;
    QString name;
    QString user_agent;
    QNetworkProxy proxy;
    HardwareFingerprint hardware;
    QString timezone;
    QStringList languages;
};

class ProfileInstance : public QObject {
    Q_OBJECT
public:
    explicit ProfileInstance(const ProfileConfig& config, QObject* parent = nullptr);
    ~ProfileInstance();

    void launch();
    void terminate();
    void freezeNetworkAccess();
    void unfreezeNetworkAccess();

    [[nodiscard]] QWebEngineView* getView() const { return m_view.get(); }
    [[nodiscard]] const ProfileConfig& getConfig() const { return m_config; }

private:
    void setupStoragePaths();
    void setupNetworkProxy();
    void injectFingerprintScripts();

    ProfileConfig m_config;
    std::unique_ptr<QWebEngineProfile> m_profile;
    std::unique_ptr<QWebEnginePage> m_page;
    std::unique_ptr<QWebEngineView> m_view;
    bool m_isFrozen{false};
};

```

#### 1.2 Profile Pre-flight Validator Header (`ProfileValidator.hpp`)

```cpp
#pragma once
#include "ProfileInstance.hpp"
#include <QStringList>

struct ValidationResult {
    bool isValid{true};
    QStringList discrepancies;
};

class ProfileValidator {
public:
    static ValidationResult validateProfile(const ProfileConfig& config) {
        ValidationResult res;
        
        // 1. Cross-check User-Agent vs Hardware Platform sanity
        if (config.user_agent.contains("Windows") && config.hardware.webgl_renderer.find("Apple") != std::string::npos) {
            res.isValid = false;
            res.discrepancies.append("CRITICAL: Windows User-Agent paired with Apple WebGL Renderer.");
        }

        // 2. Validate Master Seed String Length
        if (config.hardware.master_seed_hex.length() != 64) {
            res.isValid = false;
            res.discrepancies.append("ERROR: Master Seed Hex must be exactly 64 characters.");
        }

        // 3. Screen Dimensions Sanity Check
        if (config.hardware.screen_width <= 0 || config.hardware.screen_height <= 0) {
            res.isValid = false;
            res.discrepancies.append("ERROR: Invalid Screen Dimensions.");
        }

        return res;
    }
};

```

---

### Module 2: Deterministic PRNG & Geo-Sync Engines (`src/crypto/`, `src/geo/`)

#### 2.1 Master Seed PRNG Engine (`ProfileSeedEngine.hpp`)

```cpp
#pragma once
#include <string>
#include <cstdint>

class ProfileSeedEngine {
public:
    explicit ProfileSeedEngine(const std::string& masterHexSeed);

    // Derive deterministic pseudo-random float in [min, max]
    double deriveFloat(const std::string& salt, double min, double max);
    
    // Derive deterministic uint32 seed
    uint32_t deriveSeed(const std::string& salt);

private:
    std::string m_masterSeed;
    uint32_t sha256ToUint32(const std::string& input);
};

```

#### 2.2 GeoIP Auto-Sync Engine (`GeoSyncEngine.hpp`)

```cpp
#pragma once
#include <QString>
#include <QStringList>
#include <maxminddb.h>

struct GeoLocationData {
    QString countryCode;
    QString timezone;
    QStringList languages;
    double latitude{0.0};
    double longitude{0.0};
    int timezoneOffsetMinutes{0};
};

class GeoSyncEngine {
public:
    explicit GeoSyncEngine(const std::string& mmdbPath);
    ~GeoSyncEngine();

    GeoLocationData resolveProxyIp(const QString& ipAddress);

private:
    MMDB_s m_mmdb;
    bool m_isOpen{false};
};

```

---

### Module 3: Win32 Hooking & JavaScript Fingerprint Engine (`src/hooks/`)

#### 3.1 Win32 Native API Hooking via MinHook (`MinHookManager.hpp/cpp`)

Intercept Win32 system calls at startup:

1. **`GetSystemInfo` / `GetNativeSystemInfo**`:

```cpp
typedef void (WINAPI* GetSystemInfo_t)(LPSYSTEM_INFO);
static GetSystemInfo_t fpGetSystemInfo = nullptr;

void WINAPI Hooked_GetSystemInfo(LPSYSTEM_INFO lpSystemInfo) {
    fpGetSystemInfo(lpSystemInfo);
    lpSystemInfo->dwNumberOfProcessors = TargetProfile::getCpuCores();
}

```

2. **`GlobalMemoryStatusEx`**:

```cpp
typedef BOOL (WINAPI* GlobalMemoryStatusEx_t)(LPMEMORYSTATUSEX);
static GlobalMemoryStatusEx_t fpGlobalMemoryStatusEx = nullptr;

BOOL WINAPI Hooked_GlobalMemoryStatusEx(LPMEMORYSTATUSEX lpBuffer) {
    BOOL result = fpGlobalMemoryStatusEx(lpBuffer);
    if (result) {
        lpBuffer->ullTotalPhys = static_cast<DWORDLONG>(TargetProfile::getMemoryGB()) * 1024 * 1024 * 1024;
    }
    return result;
}

```

#### 3.2 Injected JavaScript Engine with Font Sandboxing (`FingerprintEngine.cpp`)

Injected into `QWebEngineScript::MainWorld` at `QWebEngineScript::DocumentCreation`:

```cpp
QString FingerprintEngine::generateInjectionScript(const HardwareFingerprint& fp, ProfileSeedEngine& seedEngine) {
    uint32_t canvasSeed = seedEngine.deriveSeed("canvas_noise");
    uint32_t audioSeed = seedEngine.deriveSeed("audio_noise");

    return QString(R"(
        (function() {
            // 1. Hardware Concurrency, Memory & Automation Flag Removal
            Object.defineProperty(navigator, 'hardwareConcurrency', { get: () => %1 });
            Object.defineProperty(navigator, 'deviceMemory', { get: () => %2 });
            Object.defineProperty(navigator, 'webdriver', { get: () => undefined });

            // 2. Screen Dimensions Spoofing
            Object.defineProperty(screen, 'width', { get: () => %3 });
            Object.defineProperty(screen, 'height', { get: () => %4 });
            Object.defineProperty(screen, 'availWidth', { get: () => %3 });
            Object.defineProperty(screen, 'availHeight', { get: () => %4 - 40 });

            // 3. WebGL Vendor & Renderer Hooking
            const getParameterProxy = WebGLRenderingContext.prototype.getParameter;
            WebGLRenderingContext.prototype.getParameter = function(parameter) {
                if (parameter === 0x9245) return '%5'; // UNMASKED_VENDOR_WEBGL
                if (parameter === 0x9246) return '%6'; // UNMASKED_RENDERER_WEBGL
                return getParameterProxy.apply(this, arguments);
            };

            // 4. Deterministic Canvas Noise Injection
            const cSeed = %7;
            const originalToDataURL = HTMLCanvasElement.prototype.toDataURL;
            HTMLCanvasElement.prototype.toDataURL = function(type) {
                const context = this.getContext('2d');
                if (context) {
                    const imgData = context.getImageData(0, 0, this.width || 1, this.height || 1);
                    for (let i = 0; i < imgData.data.length; i += 4) {
                        imgData.data[i] = imgData.data[i] ^ (cSeed & 0x05);
                    }
                    context.putImageData(imgData, 0, 0);
                }
                return originalToDataURL.apply(this, arguments);
            };

            // 5. AudioContext Fingerprint Noise Injection
            const aSeed = %8;
            const originalGetChannelData = AudioBuffer.prototype.getChannelData;
            AudioBuffer.prototype.getChannelData = function(channel) {
                const data = originalGetChannelData.apply(this, arguments);
                const noise = (aSeed % 100) * 0.0000001;
                for (let i = 0; i < data.length; i += 100) {
                    data[i] += noise;
                }
                return data;
            };

            // 6. Font Vector Masking Sandboxing
            const allowedFonts = new Set(["Arial", "Times New Roman", "Segoe UI", "Courier New", "Tahoma", "Verdana"]);
            if (document.fonts && document.fonts.check) {
                const origFontCheck = document.fonts.check.bind(document.fonts);
                document.fonts.check = function(font, text) {
                    const family = font.split(' ').pop().replace(/["']/g, '');
                    if (!allowedFonts.has(family)) return false;
                    return origFontCheck(font, text);
                };
            }
        })();
    )")
    .arg(fp.cpu_cores)
    .arg(fp.memory_gb)
    .arg(fp.screen_width)
    .arg(fp.screen_height)
    .arg(QString::fromStdString(fp.webgl_vendor))
    .arg(QString::fromStdString(fp.webgl_renderer))
    .arg(canvasSeed)
    .arg(audioSeed);
}

```

---

### Module 4: Proactive Network Kill Switch Engine (`src/network/`)

#### 4.1 Network Monitor Header (`NetworkMonitor.hpp`)

```cpp
#pragma once
#include <QObject>
#include <QTimer>
#include <QTcpSocket>
#include <QNetworkProxy>
#include <QNetworkInformation>
#include <atomic>

enum class NetworkStatus {
    Healthy,
    ProxyDegraded,
    HardwareDisconnected,
    LeakedRisk
};

class NetworkMonitor : public QObject {
    Q_OBJECT
public:
    explicit NetworkMonitor(const QNetworkProxy& targetProxy, QObject* parent = nullptr);
    void startMonitoring(int heartbeatIntervalMs = 50);
    void stopMonitoring();

signals:
    void networkEmergencyTriggered(NetworkStatus status);
    void networkRestored();

private slots:
    void checkProxyHeartbeat();
    void onOsReachabilityChanged(QNetworkInformation::Reachability reachability);

private:
    QNetworkProxy m_proxy;
    QTimer* m_heartbeatTimer;
    std::atomic<bool> m_isOnline{true};
    int m_consecutiveFailures{0};
};

```

#### 4.2 Kill Switch Engine Header (`KillSwitchEngine.hpp`)

```cpp
#pragma once
#include <QObject>
#include <memory>
#include "NetworkMonitor.hpp"

class ProfileInstance;
class KillSwitchEngine : public QObject {
    Q_OBJECT
public:
    explicit KillSwitchEngine(ProfileInstance* profile, QObject* parent = nullptr);

public slots:
    void handleEmergency(NetworkStatus status);
    void handleRestoration();

private:
    bool verifyProxyIpIntegrity();

    ProfileInstance* m_profile;
    NetworkStatus m_lastStatus{NetworkStatus::Healthy};
    QString m_lastUrlBeforeDrop;
};

```

#### 4.3 Fail-Safe Action Workflow

1. **OS Kernel Callback**: Listens to Win32 `NotifyIpInterfaceChange` via Qt 6 `QNetworkInformation::reachabilityChanged`.
2. **50ms Active Ping**: Asynchronous low-level TCP heartbeat sent to the configured proxy every 50ms.
3. **Atomic Severing Protocol**:
* Halts active `QWebEngineView` page rendering.
* Replaces proxy settings with an unroutable blackhole address (`0.0.0.0:0`).
* Pauses browser navigation and blocks profile requests until proxy health is restored.


4. **Resumption Pre-Flight Check**:
* Issues an isolated HTTP request *strictly through the proxy* to an IP verification endpoint.
* If the returned IP matches the local host machine's IP, **abort resumption** and notify the UI.
* If the proxy IP matches the profile configuration, restore settings and resume browser navigation.



---

### Module 6: Qt6 Dashboard UI (`src/ui/`)

#### 6.1 Layout Specification (`MainWindow.hpp/cpp`)

* **Splitter Layout**:
1. **Sidebar Navigation**: `QListWidget` (Profiles, Proxy Pool, Network Diagnostics, Logs).
2. **Profile Grid View**: `QScrollArea` holding a dynamic `QGridLayout` of `ProfileCardWidget` items.
3. **Live Status Bar**: Real-time ping stats, active proxy IP, and a **Kill Switch Status Indicator** (Green: SECURE, Red: INTERRUPTED).



---

## 4. Full `CMakeLists.txt` Build Specification

```cmake
cmake_minimum_required(VERSION 3.20)
project(AntiDetectBrowser LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/utf-8)
endif()

find_package(Qt6 6.4 REQUIRED COMPONENTS 
    Core 
    Widgets 
    WebEngineWidgets 
    WebEngineCore 
    Network
)

find_package(OpenSSL REQUIRED)

include(FetchContent)

if(WIN32)
    FetchContent_Declare(
        minhook
        GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
        GIT_TAG        v1.3.3
    )
    FetchContent_MakeAvailable(minhook)
endif()

# Fetch nlohmann_json
FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/releases/download/v3.11.2/json.tar.xz
)
FetchContent_MakeAvailable(json)

# Fetch libmaxminddb
FetchContent_Declare(
    maxminddb
    GIT_REPOSITORY https://github.com/maxmind/libmaxminddb.git
    GIT_TAG        main
)
FetchContent_MakeAvailable(maxminddb)

add_executable(${PROJECT_NAME}
    src/main.cpp
    src/core/ProfileManager.cpp
    src/core/ProfileInstance.cpp
    src/core/CustomUrlInterceptor.cpp
    src/core/ProfileValidator.cpp
    src/crypto/ProfileSeedEngine.cpp
    src/geo/GeoSyncEngine.cpp
    src/hooks/MinHookManager.cpp
    src/hooks/FingerprintEngine.cpp
    src/network/NetworkMonitor.cpp
    src/network/KillSwitchEngine.cpp
    src/ui/MainWindow.cpp
    src/ui/ProfileCardWidget.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE src)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::WebEngineWidgets
    Qt6::WebEngineCore
    Qt6::Network
    OpenSSL::Crypto
    nlohmann_json::nlohmann_json
    maxminddb
)

if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE minhook iphlpapi)
    target_compile_definitions(${PROJECT_NAME} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES
    AUTOMOC ON
    AUTOUIC ON
    AUTORCC ON
)

```

---

## 5. Sequential Implementation and Validation Sequence

Feed these commands to Codex CLI sequentially:

1. **Phase 1 (Profile Sandbox & Pre-flight Isolation)**:
> Implement Module 1, including isolated storage paths, request interception, profile lifecycle management, and pre-flight validation. Build and run its tests before starting Module 2.


2. **Phase 2 (Seed Engine & GeoSync Module)**:
> Implement Module 2 using OpenSSL SHA-256 and libmaxminddb. Validate deterministic seed derivation and portable GeoIP error handling.


3. **Phase 3 (Fingerprint Injection & Guarded Native Hooks)**:
> Implement Module 3 JavaScript injection for navigator, canvas, WebGL, audio, and font surfaces. Compile MinHook and Win32 APIs only under `#ifdef _WIN32`; use a portable no-op native-hook implementation on Linux.


4. **Phase 4 (Proxy Manager & Network Kill Switch)**:
> Implement Module 4 proxy health monitoring, request containment, immediate profile freezing, and verified restoration. Exercise failure and recovery with local mock endpoints.


5. **Phase 5 (Qt6 Dashboard UI — Module 6)**:
> Implement the native profile manager, cookie inspector, geo-sync controls, proxy diagnostics, and kill-switch status UI.


6. **Phase 6 (Unified Build & Release Validation)**:
> Finalize the CMake build for headless Linux validation and Windows desktop deployment, then run the complete unit and mock-browser validation suite.

### Module 2.2 Extension: Proxy Network Geo-Synchronization Engine
- **GeoIP Localization:** When a profile connects through a proxy, parse the proxy exit node's country and timezone.
- **Runtime JS Overrides:** Dynamically inject runtime JavaScript overrides for `Intl.DateTimeFormat`, `Date.prototype.getTimezoneOffset`, `navigator.language`, and `navigator.languages` to maintain network-to-browser environmental consistency.
- **Header Synchronization:** Automatically set the `QWebEngineProfile` `Accept-Language` header to align with the proxy's regional language code.
- **WebRTC Enforcement:** Configure `QWebEngineSettings::WebRTCPublicInterfacesOnly` to ensure WebRTC candidate gathering is restricted strictly to proxied interfaces.

### Module 4.4: TLS & HTTP/2 Network Stack Alignment
- **TLS JA3/JA4 Normalization:** Configure `QNetworkAccessManager` and SSL sockets to negotiate TLS ciphers, extensions, and elliptic curves in the exact order emitted by modern Chromium releases.
- **HTTP/2 Frame Synchronization:** Match Chromium's native HTTP/2 SETTINGS frames (header table size, initial window size, max concurrent streams) to prevent TLS/H2 fingerprint mismatch flagging.
- **Client Hints Parity:** Enforce synchronous delivery of `Sec-CH-UA`, `Sec-CH-UA-Mobile`, and `Sec-CH-UA-Platform` headers on all initial navigation requests to Google services.
