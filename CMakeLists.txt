cmake_minimum_required(VERSION 3.28)

project(ModelHarbor
    VERSION 0.1.0
    DESCRIPTION "Local OpenAI-compatible API aggregation and diagnostics"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)
enable_testing()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.11.1 REQUIRED COMPONENTS Core Network Widgets)

add_library(modelharbor-core STATIC
    src/core/version.cpp
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

add_executable(modelharbor-gateway
    src/gateway/main.cpp
)

target_link_libraries(modelharbor-gateway
    PRIVATE
        modelharbor-core
        Qt6::Core
        Qt6::Network
)

add_executable(modelharbor-desktop
    src/desktop/main.cpp
)

target_link_libraries(modelharbor-desktop
    PRIVATE
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
