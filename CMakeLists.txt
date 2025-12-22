cmake_minimum_required(VERSION 3.20)

project(hft-engine LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler optimizations
if(MSVC)
    add_compile_options(/O2 /Oi /Ot)
else()
    # Production build for AWS c7i.large (Sapphire Rapids)
    if(CMAKE_BUILD_TYPE STREQUAL "Production")
        add_compile_options(-O3 -march=sapphirerapids -mtune=sapphirerapids -Wall -Wextra -pthread -Wno-volatile)
    else()
        add_compile_options(-O3 -march=native -mtune=native -Wall -Wextra -pthread -Wno-volatile)
    endif()
endif()

include_directories(include)
include_directories(${Boost_INCLUDE_DIRS})

# Dependencies
find_package(Threads REQUIRED)
# find_package(fmt REQUIRED) - Replaced with FetchContent
find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(CURL REQUIRED)
find_package(Boost REQUIRED)

# DPDK Support
option(ENABLE_DPDK "Enable DPDK support" ON)

if(ENABLE_DPDK)
    pkg_check_modules(DPDK libdpdk)
    if(DPDK_FOUND)
        message(STATUS "DPDK found. Enabling kernel bypass features.")
        add_definitions(-DUSE_DPDK)
        include_directories(${DPDK_INCLUDE_DIRS})
        link_directories(${DPDK_LIBRARY_DIRS})
    else()
        message(WARNING "DPDK not found. Falling back to standard networking.")
    endif()
else()
    message(STATUS "DPDK disabled by user.")
endif()

include(FetchContent)

# fmt
FetchContent_Declare(
  fmt
  GIT_REPOSITORY https://github.com/fmtlib/fmt.git
  GIT_TAG        10.2.1
)
FetchContent_MakeAvailable(fmt)

# simdjson
FetchContent_Declare(
  simdjson
  GIT_REPOSITORY https://github.com/simdjson/simdjson.git
  GIT_TAG        v3.6.0
)
FetchContent_MakeAvailable(simdjson)

# IXWebSocket
set(USE_TLS ON CACHE BOOL "" FORCE)
set(USE_OPEN_SSL ON CACHE BOOL "" FORCE)
FetchContent_Declare(
  ixwebsocket
  GIT_REPOSITORY https://github.com/machinezone/IXWebSocket.git
  GIT_TAG        v11.4.4
)
FetchContent_MakeAvailable(ixwebsocket)

# Define the executable
add_executable(hft_engine
    src/main.cpp
    src/feed_handler/FeedHandler.cpp
    src/strategy/StrategyEngine.cpp
    src/execution/ExecutionGateway.cpp
    src/execution/CoinbaseAuth.cpp
    src/network/DPDKPoller.cpp
)

if(DPDK_FOUND)
    target_link_libraries(hft_engine PRIVATE ${DPDK_LIBRARIES})
    # Explicitly link ENA driver for AWS
    target_link_libraries(hft_engine PRIVATE rte_net_ena)
endif()

# Link libraries
target_link_libraries(hft_engine PRIVATE 
    Threads::Threads
    fmt::fmt
    simdjson
    ixwebsocket::ixwebsocket
    OpenSSL::SSL OpenSSL::Crypto
    ZLIB::ZLIB
    CURL::libcurl
)

# Replay Engine
add_executable(replay_engine
    src/replay_engine.cpp
    src/feed_handler/FeedHandler.cpp
    src/strategy/StrategyEngine.cpp
    src/execution/ExecutionGateway.cpp
    src/execution/CoinbaseAuth.cpp
    src/network/DPDKPoller.cpp
)

if(DPDK_FOUND)
    target_link_libraries(replay_engine PRIVATE ${DPDK_LIBRARIES})
    target_link_libraries(replay_engine PRIVATE rte_net_ena)
endif()

target_link_libraries(replay_engine PRIVATE 
    Threads::Threads
    fmt::fmt
    simdjson
    ixwebsocket::ixwebsocket
    OpenSSL::SSL OpenSSL::Crypto
    ZLIB::ZLIB
    CURL::libcurl
)

# Integration Tests
add_executable(integration_feed
    tests/integration_feed.cpp
    src/feed_handler/FeedHandler.cpp
)

target_link_libraries(integration_feed PRIVATE 
    Threads::Threads
    fmt::fmt
    simdjson
    ixwebsocket::ixwebsocket
    OpenSSL::SSL OpenSSL::Crypto
    ZLIB::ZLIB
)
# Auth Test
add_executable(test_auth
    tests/test_auth.cpp
    src/execution/CoinbaseAuth.cpp
)

target_link_libraries(test_auth PRIVATE 
    Threads::Threads
    fmt::fmt
    simdjson
    OpenSSL::SSL OpenSSL::Crypto
    CURL::libcurl
)
