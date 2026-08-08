cmake_minimum_required(VERSION 3.20)

project(AntiDetectBrowser VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

option(ADB_BUILD_TESTS "Build the validation test suite" ON)

if(MSVC)
    add_compile_options(/utf-8 /W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(Qt6 6.4 REQUIRED COMPONENTS
    Core
    Network
    Qml
    Widgets
    WebEngineCore
    WebEngineWidgets
)
find_package(OpenSSL 3 REQUIRED COMPONENTS Crypto)
find_package(PkgConfig REQUIRED)
pkg_check_modules(MAXMINDDB REQUIRED IMPORTED_TARGET libmaxminddb)

include(FetchContent)
if(WIN32)
    FetchContent_Declare(
        minhook
        GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
        GIT_TAG v1.3.3
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(minhook)
endif()

add_library(adb_core STATIC
    src/core/CustomUrlInterceptor.cpp
    src/core/ProfileInstance.cpp
    src/core/ProfileManager.cpp
    src/core/ProfileSandbox.cpp
    src/core/ProfileValidator.cpp
    src/crypto/ProfileSeedEngine.cpp
    src/geo/GeoSyncEngine.cpp
    src/hooks/FingerprintEngine.cpp
    src/hooks/MinHookManager.cpp
    src/network/NetworkMonitor.cpp
    src/network/KillSwitchEngine.cpp
)

target_include_directories(adb_core PUBLIC src)
target_link_libraries(adb_core PUBLIC
    Qt6::Core
    Qt6::Network
    Qt6::Qml
    Qt6::Widgets
    Qt6::WebEngineCore
    Qt6::WebEngineWidgets
    OpenSSL::Crypto
    PkgConfig::MAXMINDDB
)

add_executable(AntiDetectBrowser WIN32 src/main.cpp)
target_link_libraries(AntiDetectBrowser PRIVATE adb_core)

if(WIN32)
    target_compile_definitions(adb_core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(adb_core PUBLIC minhook iphlpapi)
endif()

if(ADB_BUILD_TESTS)
    include(CTest)
    find_package(Qt6 6.4 REQUIRED COMPONENTS Test)

    add_executable(test_module1 tests/test_module1.cpp)
    target_link_libraries(test_module1 PRIVATE adb_core Qt6::Test)

    add_test(NAME module1_profile_isolation COMMAND test_module1)
    set_tests_properties(module1_profile_isolation PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QTWEBENGINE_CHROMIUM_FLAGS=--no-sandbox --disable-gpu --single-process --no-zygote;QTWEBENGINE_DISABLE_SANDBOX=1"
        TIMEOUT 30
    )

    add_executable(test_module2 tests/test_module2.cpp)
    target_link_libraries(test_module2 PRIVATE adb_core Qt6::Test)
    add_test(NAME module2_seed_and_geo COMMAND test_module2)
    set_tests_properties(module2_seed_and_geo PROPERTIES TIMEOUT 30)

    add_executable(test_module3 tests/test_module3.cpp)
    target_link_libraries(test_module3 PRIVATE adb_core Qt6::Test)
    add_test(NAME module3_fingerprint_injection COMMAND test_module3)
    set_tests_properties(module3_fingerprint_injection PROPERTIES TIMEOUT 30)

    add_executable(test_module4 tests/test_module4.cpp)
    target_link_libraries(test_module4 PRIVATE adb_core Qt6::Test)
    add_test(NAME module4_network_kill_switch COMMAND test_module4)
    set_tests_properties(module4_network_kill_switch PROPERTIES TIMEOUT 30)

    add_executable(test_live_rigorous tests/test_live_rigorous.cpp)
    target_link_libraries(test_live_rigorous PRIVATE adb_core Qt6::Test)
    add_test(NAME live_rigorous_network_privacy COMMAND test_live_rigorous)
    set_tests_properties(live_rigorous_network_privacy PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QTWEBENGINE_CHROMIUM_FLAGS=--no-sandbox --disable-gpu --no-zygote --force-webrtc-ip-handling-policy=disable_non_proxied_udp;QTWEBENGINE_DISABLE_SANDBOX=1"
        TIMEOUT 45
    )
endif()
