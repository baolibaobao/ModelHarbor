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

find_package(Qt6 6.11.1 REQUIRED COMPONENTS Core)

add_executable(modelharbor-toolchain-smoke
    tests/toolchain_smoke.cpp
)

target_link_libraries(modelharbor-toolchain-smoke
    PRIVATE
        Qt6::Core
)

target_compile_features(modelharbor-toolchain-smoke PRIVATE cxx_std_20)

add_test(
    NAME modelharbor.toolchain.smoke
    COMMAND modelharbor-toolchain-smoke
)
