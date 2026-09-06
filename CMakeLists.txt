cmake_minimum_required(VERSION 3.20)
project(antigravity-chrome-bridge LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Fetch nlohmann/json (header-only) ────────────────────────────────
include(FetchContent)
FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(json)

foreach(feature OPENSSL WOLFSSL MBEDTLS ZLIB BROTLI ZSTD)
    set(HTTPLIB_USE_${feature}_IF_AVAILABLE OFF CACHE BOOL "Loopback transport only" FORCE)
endforeach()
set(HTTPLIB_INSTALL OFF CACHE BOOL "" FORCE)
# All connections use literal loopback addresses; no asynchronous DNS is needed.
set(HTTPLIB_USE_NON_BLOCKING_GETADDRINFO OFF CACHE BOOL "" FORCE)
FetchContent_Declare(httplib
    URL https://codeload.github.com/yhirose/cpp-httplib/tar.gz/refs/tags/v0.54.1
    URL_HASH SHA256=7310f5312e1423830d649b38ed028e9db86303a979ccbfdbd1c4b1574f422dfb
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(httplib)
find_package(Threads REQUIRED)

# ── Source files ─────────────────────────────────────────────────────
set(SOURCES
    src/main.cpp
    src/mcp_core.cpp
    src/mcp_tools.cpp
    src/mcp_resources.cpp
    src/cdp_client.cpp
    src/ext_bridge.cpp
    src/ws_client.cpp
    src/security.cpp
)

add_executable(${PROJECT_NAME} ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE src)
target_link_libraries(${PROJECT_NAME} PRIVATE nlohmann_json::nlohmann_json httplib::httplib Threads::Threads)
target_compile_definitions(${PROJECT_NAME} PRIVATE NOMINMAX CPPHTTPLIB_WEBSOCKET_MAX_PAYLOAD_LENGTH=10485760)

# ── Platform-specific linking ────────────────────────────────────────
if(WIN32)
    target_compile_definitions(${PROJECT_NAME} PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00)
    target_link_libraries(${PROJECT_NAME} PRIVATE ws2_32 bcrypt advapi32)
endif()

include(CTest)
if(BUILD_TESTING)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_test(NAME bridge_protocol COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_bridge.py $<TARGET_FILE:${PROJECT_NAME}>)
    set_tests_properties(bridge_protocol PROPERTIES TIMEOUT 120)
endif()

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /utf-8 /W4)
endif()

# ── MinGW cross-compilation tweaks ───────────────────────────────────
if(MINGW)
    target_link_libraries(${PROJECT_NAME} PRIVATE ws2_32)
    # Static linking for portable .exe
    target_link_options(${PROJECT_NAME} PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# ── Release optimizations ───────────────────────────────────────────
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    if(MSVC)
        target_compile_options(${PROJECT_NAME} PRIVATE /O2 /GL)
        target_link_options(${PROJECT_NAME} PRIVATE /LTCG)
    else()
        target_compile_options(${PROJECT_NAME} PRIVATE -O2 -flto)
        target_link_options(${PROJECT_NAME} PRIVATE -flto -s)
    endif()
endif()

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
