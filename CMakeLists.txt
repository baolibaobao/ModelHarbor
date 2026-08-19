cmake_minimum_required(VERSION 3.28)

project(ModelHarbor
    VERSION 0.1.0
    DESCRIPTION "Local OpenAI-compatible API aggregation and diagnostics"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(WIN32)
    add_compile_definitions(_WIN32_WINNT=0x0A00 WINVER=0x0A00)
endif()

include(CTest)
enable_testing()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.11.1 REQUIRED COMPONENTS Core Network Widgets)
find_package(Boost CONFIG REQUIRED COMPONENTS beast)
find_package(CURL CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/resources/migrations/0001_initial.sql" MIGRATION_0001_SQL)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/resources/migrations/0002_import_jobs.sql" MIGRATION_0002_SQL)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/builtin_migration_sql.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/persistence/builtin_migration_sql.h"
    @ONLY
)

add_library(modelharbor-core STATIC
    src/core/version.cpp
    src/ipc/ipc_endpoint.cpp
    src/ipc/ipc_protocol.cpp
)

target_include_directories(modelharbor-core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(modelharbor-core
    PUBLIC
        Qt6::Core
)

target_compile_features(modelharbor-core PUBLIC cxx_std_20)

if(WIN32)
    target_link_libraries(modelharbor-core PRIVATE Advapi32)
endif()

add_library(modelharbor-persistence STATIC
    src/persistence/app_settings_repository.cpp
    src/persistence/app_settings_repository.h
    src/persistence/migration.cpp
    src/persistence/migration.h
    src/persistence/site_repository.cpp
    src/persistence/site_repository.h
    src/persistence/sqlite_database.cpp
    src/persistence/sqlite_database.h
)

target_include_directories(modelharbor-persistence
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}/generated
)

target_link_libraries(modelharbor-persistence
    PUBLIC
        unofficial::sqlite3::sqlite3
)

target_compile_features(modelharbor-persistence PUBLIC cxx_std_20)

add_library(modelharbor-application STATIC
    src/application/gateway_controller.cpp
    src/application/gateway_controller.h
)

target_include_directories(modelharbor-application
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(modelharbor-application
    PUBLIC
        modelharbor-core
        Qt6::Core
        Qt6::Network
)

target_compile_features(modelharbor-application PUBLIC cxx_std_20)

add_executable(modelharbor-gateway
    src/gateway/main.cpp
)

target_link_libraries(modelharbor-gateway
    PRIVATE
        modelharbor-core
        modelharbor-persistence
        Qt6::Core
        Qt6::Network
)

add_executable(modelharbor-desktop
    src/desktop/main.cpp
    src/desktop/main_window.cpp
    src/desktop/main_window.h
    src/desktop/theme.cpp
    src/desktop/theme.h
    resources/lucide.qrc
)

target_link_libraries(modelharbor-desktop
    PRIVATE
        modelharbor-application
        modelharbor-core
        Qt6::Core
        Qt6::Network
        Qt6::Widgets
)

set_target_properties(
    modelharbor-gateway
    modelharbor-desktop
    PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_executable(modelharbor-toolchain-smoke
    tests/toolchain_smoke.cpp
)

target_link_libraries(modelharbor-toolchain-smoke
    PRIVATE
        Qt6::Core
)

target_compile_features(modelharbor-toolchain-smoke PRIVATE cxx_std_20)

set_target_properties(
    modelharbor-toolchain-smoke
    PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_test(
    NAME modelharbor.toolchain.smoke
    COMMAND modelharbor-toolchain-smoke
)

add_executable(modelharbor-ipc-protocol-test
    tests/ipc_protocol_test.cpp
)

target_link_libraries(modelharbor-ipc-protocol-test
    PRIVATE
        modelharbor-core
        Qt6::Core
)

set_target_properties(modelharbor-ipc-protocol-test
    PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_test(
    NAME modelharbor.ipc.protocol
    COMMAND modelharbor-ipc-protocol-test
)

add_executable(modelharbor-ipc-integration
    tests/ipc_integration.cpp
)

target_link_libraries(modelharbor-ipc-integration
    PRIVATE
        modelharbor-core
        Qt6::Core
        Qt6::Network
)

target_compile_definitions(modelharbor-ipc-integration
    PRIVATE
        MODELHARBOR_GATEWAY_EXECUTABLE="$<TARGET_FILE:modelharbor-gateway>"
)

set_target_properties(modelharbor-ipc-integration
    PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_test(
    NAME modelharbor.ipc.integration
    COMMAND modelharbor-ipc-integration
)

add_executable(modelharbor-gateway-controller-integration
    tests/gateway_controller_integration.cpp
)

target_link_libraries(modelharbor-gateway-controller-integration
    PRIVATE
        modelharbor-application
        Qt6::Core
)

target_compile_definitions(modelharbor-gateway-controller-integration
    PRIVATE
        MODELHARBOR_GATEWAY_EXECUTABLE="$<TARGET_FILE:modelharbor-gateway>"
)

set_target_properties(modelharbor-gateway-controller-integration
    PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_test(
    NAME modelharbor.gateway.controller.integration
    COMMAND modelharbor-gateway-controller-integration
)

add_executable(modelharbor-http-stream-integration
    tests/http_stream_integration.cpp
)

target_link_libraries(modelharbor-http-stream-integration
    PRIVATE
        Boost::beast
        CURL::libcurl
)

set_target_properties(modelharbor-http-stream-integration
    PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_test(
    NAME modelharbor.http.stream.integration
    COMMAND modelharbor-http-stream-integration
)

set_tests_properties(modelharbor.http.stream.integration PROPERTIES TIMEOUT 15)

add_library(modelharbor-test-support STATIC
    tests/support/fake_upstream.cpp
    tests/support/fake_upstream.h
    tests/support/test_data_dir.cpp
    tests/support/test_data_dir.h
    tests/support/virtual_clock.h
    tests/support/deterministic_random.h
)

target_include_directories(modelharbor-test-support
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
)

target_link_libraries(modelharbor-test-support
    PUBLIC
        Boost::beast
)

target_compile_features(modelharbor-test-support PUBLIC cxx_std_20)

add_executable(modelharbor-test-support-test
    tests/test_support_test.cpp
)

target_link_libraries(modelharbor-test-support-test
    PRIVATE
        modelharbor-test-support
        Boost::beast
)

set_target_properties(
    modelharbor-test-support-test
    PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_test(
    NAME modelharbor.test.support
    COMMAND modelharbor-test-support-test
)

add_executable(modelharbor-persistence-test
    tests/persistence_test.cpp
)

add_executable(modelharbor-persistence-crash-writer
    tests/persistence_crash_writer.cpp
)

target_link_libraries(modelharbor-persistence-crash-writer
    PRIVATE
        modelharbor-persistence
)

set_target_properties(
    modelharbor-persistence-crash-writer
    PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

target_link_libraries(modelharbor-persistence-test
    PRIVATE
        modelharbor-persistence
        modelharbor-test-support
)

target_compile_definitions(modelharbor-persistence-test
    PRIVATE
        MODELHARBOR_MIGRATION_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/resources/migrations"
        MODELHARBOR_CRASH_WRITER="$<TARGET_FILE:modelharbor-persistence-crash-writer>"
)

set_target_properties(
    modelharbor-persistence-test
    PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_test(
    NAME modelharbor.persistence
    COMMAND modelharbor-persistence-test
)

add_executable(modelharbor-desktop-smoke
    tests/desktop_smoke.cpp
    src/desktop/main_window.cpp
    src/desktop/main_window.h
    src/desktop/theme.cpp
    src/desktop/theme.h
    resources/lucide.qrc
)

target_link_libraries(modelharbor-desktop-smoke
    PRIVATE
        modelharbor-application
        modelharbor-core
        Qt6::Core
        Qt6::Network
        Qt6::Widgets
)

set_target_properties(
    modelharbor-desktop-smoke
    PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_test(
    NAME modelharbor.desktop.smoke
    COMMAND modelharbor-desktop-smoke
)

set_tests_properties(modelharbor.desktop.smoke PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
