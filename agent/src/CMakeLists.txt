cmake_minimum_required(VERSION 3.20)
project(NeuroGuard_Agent)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(VCPKG_TARGET_TRIPLET "x64-windows-static" CACHE STRING "")
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

find_package(nlohmann_json CONFIG REQUIRED)
find_package(cpr          CONFIG REQUIRED)
find_package(GTest        CONFIG REQUIRED)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
add_definitions(-DCPR_STATIC -DNOMINMAX -DUNICODE -D_UNICODE)

add_executable(NeuroGuard_Agent WIN32
    main.cpp
)

if(MSVC)
    target_compile_options(NeuroGuard_Agent PRIVATE
        /utf-8
        /W3
        /MP
        /FI"${CMAKE_CURRENT_SOURCE_DIR}/pch.hpp"
    )
    set_target_properties(NeuroGuard_Agent PROPERTIES
        LINK_FLAGS "/ENTRY:mainCRTStartup /LTCG"
    )
endif()

target_link_libraries(NeuroGuard_Agent PRIVATE
    nlohmann_json::nlohmann_json
    cpr::cpr
    ws2_32.lib
    winhttp.lib
    iphlpapi.lib
    ole32.lib
    oleaut32.lib
    wbemuuid.lib
    advapi32.lib
    psapi.lib
    shell32.lib
    powrprof.lib
    pdh.lib
    crypt32.lib
    wintrust.lib
)

# ── Google Tests ─────────────────────────────────────────
add_executable(NeuroGuard_Tests
    tests/test_main.cpp
)

if(MSVC)
    target_compile_options(NeuroGuard_Tests PRIVATE
        /utf-8
        /W3
        /MP
        /FI"${CMAKE_CURRENT_SOURCE_DIR}/pch.hpp"
    )
endif()

target_link_libraries(NeuroGuard_Tests PRIVATE
    GTest::gtest
    GTest::gtest_main
    nlohmann_json::nlohmann_json
    cpr::cpr
    ws2_32.lib
    winhttp.lib
    iphlpapi.lib
    ole32.lib
    oleaut32.lib
    wbemuuid.lib
    advapi32.lib
    psapi.lib
    shell32.lib
    gdiplus.lib
    powrprof.lib
    pdh.lib
    crypt32.lib
    wintrust.lib
)

enable_testing()
add_test(NAME NeuroGuard_Tests COMMAND NeuroGuard_Tests)