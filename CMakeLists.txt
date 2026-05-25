cmake_minimum_required(VERSION 3.20)
project(antigravity-chrome-bridge LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Fetch nlohmann/json (header-only) ────────────────────────────────
include(FetchContent)
FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
)
FetchContent_MakeAvailable(json)

# ── Source files ─────────────────────────────────────────────────────
set(SOURCES
    src/main.cpp
    src/mcp_core.cpp
    src/mcp_tools.cpp
    src/mcp_resources.cpp
    src/cdp_client.cpp
    src/ext_bridge.cpp
    src/ws_client.cpp
)

add_executable(${PROJECT_NAME} ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE src)
target_link_libraries(${PROJECT_NAME} PRIVATE nlohmann_json::nlohmann_json)

# ── Platform-specific linking ────────────────────────────────────────
if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE ws2_32)
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
