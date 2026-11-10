cmake_minimum_required(VERSION 3.20)
project(LowLatency LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "LowLatency currently targets Windows only (Winsock, SetThreadAffinityMask).")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(lowlatency_common STATIC
    src/Common/Network.cpp
    src/Common/Thread.cpp
)
target_include_directories(lowlatency_common PUBLIC src)
target_link_libraries(lowlatency_common PUBLIC Ws2_32)
if(MSVC)
    target_compile_options(lowlatency_common PUBLIC /W4 /w14062 /permissive- /wd4324)
endif()

add_executable(LowLatencyTests
    tests/TestMain.cpp
    tests/TypesTests.cpp
    tests/MemoryPoolTests.cpp
    tests/LFQueueTests.cpp
    tests/ThreadLoggerTests.cpp
    tests/TcpTests.cpp
    tests/MulticastTests.cpp
    tests/ExchangeTests.cpp
)
target_link_libraries(LowLatencyTests PRIVATE lowlatency_common)

enable_testing()
add_test(NAME LowLatencyTests COMMAND LowLatencyTests)
