cmake_minimum_required(VERSION 3.5...3.25)

project(guss
    VERSION 0.1.0
    DESCRIPTION "Guss — a pluggable static site generator that FORGES your content into PERFECTION"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# -----------------------------------------------------------------------------
# Options
# -----------------------------------------------------------------------------
option(GUSS_BUILD_TESTS "Build tests" OFF)
option(GUSS_BUILD_SERVER "Build HTTP server component" OFF)
option(GUSS_ENABLE_SANITIZERS "Enable ASan/UBSan" OFF)

if(GUSS_ENABLE_SANITIZERS)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

# -----------------------------------------------------------------------------
# Dependencies via CPM
# -----------------------------------------------------------------------------
set(CPM_PATH "${CMAKE_BINARY_DIR}/CPM.cmake")
if(NOT EXISTS "${CPM_PATH}")
    file(DOWNLOAD
        https://github.com/cpm-cmake/CPM.cmake/releases/download/v0.42.1/CPM.cmake
        "${CPM_PATH}"
        STATUS status
    )
    list(GET status 0 status_code)
    if(NOT status_code EQUAL 0)
        message(FATAL_ERROR "Failed to download CPM.cmake")
    endif()
endif()
include("${CPM_PATH}")

# JSON (template contexts, config serialization)
CPMAddPackage("gh:nlohmann/json@3.11.3")

# Jinja2-like template engine
CPMAddPackage(
    NAME inja
    GITHUB_REPOSITORY pantor/inja
    VERSION 3.4.0
    OPTIONS "INJA_USE_EMBEDDED_JSON OFF"
)

# SIMD-accelerated JSON parsing (CMS API responses)
CPMAddPackage("gh:simdjson/simdjson@3.10.1")

# YAML config parsing
CPMAddPackage(
    NAME yaml-cpp
    GITHUB_REPOSITORY jbeder/yaml-cpp
    GIT_TAG yaml-cpp-0.9.0
)

# HTTP client (header-only)
CPMAddPackage(
    NAME httplib
    GITHUB_REPOSITORY yhirose/cpp-httplib
    VERSION 0.18.1
    OPTIONS "HTTPLIB_REQUIRE_OPENSSL ON"
)

# Markdown rendering
CPMAddPackage(
    NAME cmark
    GITHUB_REPOSITORY commonmark/cmark
    GIT_TAG 0.31.2
)

# CLI argument parsing
CPMAddPackage("gh:CLIUtils/CLI11@2.4.2")

# Logging
CPMAddPackage("gh:gabime/spdlog@1.14.1")

# Progress bars
CPMAddPackage("gh:p-ranav/indicators@2.3")

# Filesystem watching
CPMAddPackage(
    NAME efsw
    GITHUB_REPOSITORY SpartanJ/efsw
    GIT_TAG 1.5.1
)

# Formatting
CPMAddPackage(
    NAME fmt
    GITHUB_REPOSITORY fmtlib/fmt
    GIT_TAG 12.1.0
)

# Prefer OpenMP if available, fall back to BS::thread_pool
find_package(OpenMP)
if(NOT OpenMP_CXX_FOUND)
    CPMAddPackage("gh:bshoshany/thread-pool@5.1.0")
endif()

# Google Test (if building tests)
if(GUSS_BUILD_TESTS)
    CPMAddPackage("gh:google/googletest@1.15.2")
endif()

# -----------------------------------------------------------------------------
# Core library
# -----------------------------------------------------------------------------
#add_library(guss-core
    #src/core/config.cpp
    #src/core/model.cpp
    #src/core/permalink.cpp
    #src/core/markdown.cpp
#)

#target_include_directories(guss-core PUBLIC include)
#target_link_libraries(guss-core PUBLIC
#    nlohmann_json::nlohmann_json
#    simdjson
#    yaml-cpp
#    spdlog::spdlog
#    fmt::fmt
#)

if(TARGET cmark)
    #target_link_libraries(guss-core PUBLIC cmark)
elseif(TARGET libcmark)
    #target_link_libraries(guss-core PUBLIC libcmark)
endif()

# -----------------------------------------------------------------------------
# Adapters library
# -----------------------------------------------------------------------------
#add_library(guss-adapters
    #src/adapters/ghost/ghost_adapter.cpp
    #src/adapters/wordpress/wordpress_adapter.cpp
    #src/adapters/markdown/markdown_adapter.cpp
#)

target_include_directories(guss-adapters PUBLIC include)
target_link_libraries(guss-adapters PUBLIC
    guss-core
    httplib::httplib
    simdjson
)

# -----------------------------------------------------------------------------
# Render library
# -----------------------------------------------------------------------------
#add_library(guss-render
#    src/render/inja_engine.cpp
#)

target_include_directories(guss-render PUBLIC include)
target_link_libraries(guss-render PUBLIC
    guss-core
    inja
    nlohmann_json::nlohmann_json
)

# -----------------------------------------------------------------------------
# Builder library
# -----------------------------------------------------------------------------
#add_library(guss-builder
#    src/builder/pipeline.cpp
#)

target_include_directories(guss-builder PUBLIC include)
target_link_libraries(guss-builder PUBLIC
#    guss-core
    guss-adapters
    guss-render
    indicators
)

# Prefer OpenMP if available, fall back to BS::thread_pool
if(OpenMP_CXX_FOUND)
    target_link_libraries(guss-builder PUBLIC OpenMP::OpenMP_CXX)
    target_compile_definitions(guss-builder PUBLIC GUSS_USE_OPENMP)
else()
    target_link_libraries(guss-builder PUBLIC BS_thread_pool)
endif()

# -----------------------------------------------------------------------------
# Watch library
# -----------------------------------------------------------------------------
#add_library(guss-watch
#    src/watch/coordinator.cpp
#)

target_include_directories(guss-watch PUBLIC include)
target_link_libraries(guss-watch PUBLIC
    guss-core
    efsw
)

# -----------------------------------------------------------------------------
# Server (optional)
# -----------------------------------------------------------------------------
if(GUSS_BUILD_SERVER)
    add_library(guss-server
        src/server/server.cpp
    )

    target_include_directories(guss-server PUBLIC include)
    target_link_libraries(guss-server PUBLIC
        guss-core
        guss-builder
        httplib::httplib
    )
endif()

# -----------------------------------------------------------------------------
# CLI binary
# -----------------------------------------------------------------------------
add_executable(guss src/cli/main.cpp)

target_link_libraries(guss PRIVATE
    guss-core
    guss-adapters
    guss-render
    guss-builder
    guss-watch
    CLI11::CLI11
    spdlog::spdlog
    indicators
)

if(GUSS_BUILD_SERVER)
    target_link_libraries(guss PRIVATE guss-server)
    target_compile_definitions(guss PRIVATE GUSS_HAS_SERVER)
endif()

# -----------------------------------------------------------------------------
# Tests
# -----------------------------------------------------------------------------
if(GUSS_BUILD_TESTS)
    enable_testing()
    add_executable(guss-tests
        tests/test_permalink.cpp
        tests/test_markdown.cpp
        tests/test_config.cpp
    )
    target_link_libraries(guss-tests PRIVATE
        guss-core
        guss-render
        GTest::gtest_main
    )
    include(GoogleTest)
    gtest_discover_tests(guss-tests)
endif()

# -----------------------------------------------------------------------------
# Install
# -----------------------------------------------------------------------------
install(TARGETS guss RUNTIME DESTINATION bin)