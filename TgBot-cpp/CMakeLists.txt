cmake_minimum_required(VERSION 3.20)
project(NeuroGuard_Bot)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(VCPKG_TARGET_TRIPLET "x64-windows" CACHE STRING "")

find_package(TgBot CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(cpr CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Boost REQUIRED COMPONENTS system)
find_package(CURL REQUIRED)

add_executable(NeuroGuard_Bot
    main.cpp
)

if(MSVC)
    target_compile_options(NeuroGuard_Bot PRIVATE
        /utf-8
        /W3
        /MP
    )
endif()

target_include_directories(NeuroGuard_Bot PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(NeuroGuard_Bot PRIVATE
    TgBot::TgBot
    nlohmann_json::nlohmann_json
    cpr::cpr
    OpenSSL::SSL
    OpenSSL::Crypto
    Boost::system
    CURL::libcurl
    ws2_32.lib
)

# ── Google Tests ─────────────────────────────────────────────
find_package(GTest CONFIG REQUIRED)

add_executable(NeuroGuard_Bot_Tests tests/test_main.cpp)

if(MSVC)
    target_compile_options(NeuroGuard_Bot_Tests PRIVATE /utf-8 /W3 /MP)
endif()

target_include_directories(NeuroGuard_Bot_Tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(NeuroGuard_Bot_Tests PRIVATE
    GTest::gtest
    GTest::gtest_main
    nlohmann_json::nlohmann_json
    TgBot::TgBot
    cpr::cpr
    OpenSSL::SSL
    OpenSSL::Crypto
    Boost::system
    CURL::libcurl
    ws2_32.lib
)

enable_testing()
add_test(NAME NeuroGuard_Bot_Tests COMMAND NeuroGuard_Bot_Tests)