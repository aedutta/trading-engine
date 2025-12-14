cmake_minimum_required(VERSION 3.20)

project(hft-engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler optimizations for performance
if(MSVC)
    add_compile_options(/O2 /Oi /Ot)
else()
    add_compile_options(-O3 -march=native -mtune=native -Wall -Wextra -pthread)
endif()

include_directories(include)

# Define the executable
add_executable(hft_engine
    src/main.cpp
    src/feed_handler/FeedHandler.cpp
    src/strategy/StrategyEngine.cpp
    src/execution/ExecutionGateway.cpp
)

# Add threading support
find_package(Threads REQUIRED)
target_link_libraries(hft_engine PRIVATE Threads::Threads)