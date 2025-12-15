cmake_minimum_required(VERSION 3.20)

project(hft-engine LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler optimizations
if(MSVC)
    add_compile_options(/O2 /Oi /Ot)
else()
    add_compile_options(-O3 -march=native -mtune=native -Wall -Wextra -pthread)
endif()

include_directories(include)

# Dependencies
find_package(Threads REQUIRED)
find_package(fmt REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)

include(FetchContent)

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
)

# Link libraries
target_link_libraries(hft_engine PRIVATE 
    Threads::Threads
    fmt::fmt
    simdjson
    ixwebsocket::ixwebsocket
    OpenSSL::SSL OpenSSL::Crypto
    ZLIB::ZLIB
)

# Replay Engine
add_executable(replay_engine
    src/replay_engine.cpp
    src/feed_handler/FeedHandler.cpp
    src/strategy/StrategyEngine.cpp
    src/execution/ExecutionGateway.cpp
)

target_link_libraries(replay_engine PRIVATE 
    Threads::Threads
    fmt::fmt
    simdjson
    ixwebsocket::ixwebsocket
    OpenSSL::SSL OpenSSL::Crypto
    ZLIB::ZLIB
)