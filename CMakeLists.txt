cmake_minimum_required(VERSION 3.5...3.25)

project(guss
    VERSION 0.1.0
    DESCRIPTION "Guss — a pluggable static site generator that FORGES your content into PERFECTION"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Ensure pthread is properly linked (fixes GCC 14 mutex init issues)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
link_libraries(Threads::Threads)

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

if(NOT DEFINED MAX_RECURSION)
    set(MAX_RECURSION 100000)
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

# SIMD-accelerated JSON parsing (CMS API responses)
CPMAddPackage(
    NAME simdjson
    GITHUB_REPOSITORY simdjson/simdjson
    GIT_TAG v4.3.1
)


# YAML config parsing
CPMAddPackage(
    NAME yaml-cpp
    GITHUB_REPOSITORY jbeder/yaml-cpp
    GIT_TAG yaml-cpp-0.9.0
    OPTIONS
        "YAML_CPP_BUILD_TESTS OFF"
        "YAML_CPP_BUILD_TOOLS OFF"
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
    OPTIONS
        "CMARK_TESTS OFF"
        "BUILD_SHARED_LIBS OFF"
)

# CLI argument parsing
CPMAddPackage("gh:CLIUtils/CLI11@2.4.2")

# Progress bars
CPMAddPackage("gh:p-ranav/indicators@2.3")

# Filesystem watching
CPMAddPackage(
    NAME efsw
    GITHUB_REPOSITORY SpartanJ/efsw
    GIT_TAG 1.5.1
    OPTIONS
        "BUILD_SHARED_LIBS OFF"
)

# Logging
CPMAddPackage(
    NAME spdlog
    GITHUB_REPOSITORY gabime/spdlog
    VERSION 1.14.1
    OPTIONS
    "SPDLOG_FMT_EXTERNAL OFF"
    "SPDLOG_BUILD_SHARED OFF"
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
add_library(guss-core
    src/core/config.cpp
    src/core/error.cpp
    src/core/permalink.cpp
    src/core/value.cpp
)

target_include_directories(guss-core PUBLIC include)
target_link_libraries(guss-core PUBLIC
    yaml-cpp
    spdlog::spdlog
)

if(TARGET cmark)
    target_link_libraries(guss-core PUBLIC cmark)
elseif(TARGET libcmark)
    target_link_libraries(guss-core PUBLIC libcmark)
endif()

# -----------------------------------------------------------------------------
# Adapters library
# -----------------------------------------------------------------------------
add_library(guss-adapters
    src/adapters/adapter.cpp
    src/adapters/markdown/markdown_adapter.cpp
    src/adapters/rest/rest_adapter.cpp
)

target_include_directories(guss-adapters PUBLIC include)
target_link_libraries(guss-adapters PUBLIC
    guss-core
    httplib::httplib
    simdjson
)

if(OpenMP_CXX_FOUND)
    target_link_libraries(guss-adapters PUBLIC OpenMP::OpenMP_CXX)
    target_compile_definitions(guss-adapters PUBLIC GUSS_USE_OPENMP)
endif()

# -----------------------------------------------------------------------------
# Render library
# -----------------------------------------------------------------------------
add_library(guss-render
    src/render/lexer.cpp
    src/render/parser.cpp
    src/render/context.cpp
    src/render/compiler.cpp
    src/render/filters.cpp
    src/render/engine.cpp
)

target_include_directories(guss-render PUBLIC include)
target_link_libraries(guss-render PUBLIC
    guss-core
)

# -----------------------------------------------------------------------------
# Builder library
# -----------------------------------------------------------------------------
add_library(guss-builder
    src/builder/pipeline.cpp
)

target_include_directories(guss-builder PUBLIC include)
target_link_libraries(guss-builder PUBLIC
    guss-core
    guss-adapters
    guss-render
    indicators
)

# -----------------------------------------------------------------------------
# Watch library (stub for now)
# -----------------------------------------------------------------------------
add_library(guss-watch INTERFACE)

target_include_directories(guss-watch INTERFACE include)
if(TARGET efsw-static)
    target_link_libraries(guss-watch INTERFACE guss-core efsw-static)
elseif(TARGET efsw)
    target_link_libraries(guss-watch INTERFACE guss-core efsw)
endif()

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
    CLI11::CLI11
    spdlog::spdlog
    indicators
)

target_compile_definitions(guss PRIVATE MAX_RECURSION=${MAX_RECURSION})

if(GUSS_BUILD_SERVER)
    target_link_libraries(guss PRIVATE guss-server)
    add_executable(new_target)
    target_compile_definitions(guss PRIVATE GUSS_HAS_SERVER)
endif()

# Prefer OpenMP if available, fall back to BS::thread_pool
if(OpenMP_CXX_FOUND)
    target_link_libraries(guss PUBLIC OpenMP::OpenMP_CXX)
    target_compile_definitions(guss PUBLIC GUSS_USE_OPENMP)
else()
    if(TARGET BS_thread_pool)
        target_link_libraries(guss PUBLIC BS_thread_pool)
    endif()
endif()

# -----------------------------------------------------------------------------
# Tests
# -----------------------------------------------------------------------------
if(GUSS_BUILD_TESTS)
    enable_testing()
    add_executable(guss-tests
        tests/test_value.cpp
        tests/test_context.cpp
        tests/test_config.cpp
        tests/test_permalink.cpp
        tests/test_lexer.cpp
        tests/test_parser.cpp
        tests/test_compiler.cpp
        tests/test_engine.cpp
        tests/test_inheritance.cpp
        tests/test_filters.cpp
        tests/test_render_item.cpp
        tests/test_pipeline_prepare.cpp
    )
    target_link_libraries(guss-tests PRIVATE
        guss-core
        guss-adapters
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
