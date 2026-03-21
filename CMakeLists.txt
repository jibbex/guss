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

# Markdown rendering (GFM: tables, strikethrough, autolinks)
CPMAddPackage(
    NAME md4c
    GITHUB_REPOSITORY mity/md4c
    GIT_TAG release-0.5.2
    OPTIONS
        "BUILD_SHARED_LIBS OFF"
        "BUILD_MD2HTML_EXECUTABLE OFF"
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

if(TARGET md4c-html)
    target_include_directories(guss-adapters PUBLIC ${md4c_SOURCE_DIR}/src)
    target_link_libraries(guss-adapters PUBLIC md4c-html)
endif()

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
    src/render/runtime.cpp
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
    src/builder/generators.cpp
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

# -----------------------------------------------------------------------------
# Generate include/guss/cli/constants.hpp from themes/pinguin
# -----------------------------------------------------------------------------
set(_constants_out "${CMAKE_SOURCE_DIR}/include/guss/cli/constants.hpp")

file(READ "${CMAKE_SOURCE_DIR}/themes/pinguin/index.html"   _tpl_index)
file(READ "${CMAKE_SOURCE_DIR}/themes/pinguin/post.html"    _tpl_post)
file(READ "${CMAKE_SOURCE_DIR}/themes/pinguin/page.html"    _tpl_page)
file(READ "${CMAKE_SOURCE_DIR}/themes/pinguin/tag.html"     _tpl_tag)
file(READ "${CMAKE_SOURCE_DIR}/themes/pinguin/author.html"  _tpl_author)
file(READ "${CMAKE_SOURCE_DIR}/themes/pinguin/assets/style.css" _tpl_style)
file(READ "${CMAKE_SOURCE_DIR}/themes/pinguin/assets/main.js" _tpl_main_js)
file(READ "${CMAKE_SOURCE_DIR}/themes/pinguin/assets/reveal.js" _tpl_reveal_js)
file(READ "${CMAKE_SOURCE_DIR}/themes/pinguin/assets/reading-progress.js" _tpl_reading_progress_js)
file(READ "${CMAKE_SOURCE_DIR}/themes/pinguin/assets/toc.js" _tpl_toc_js)

file(WRITE "${_constants_out}" "\
#pragma once

// AUTO-GENERATED — do not edit by hand.
// Re-runs every CMake configure. Source: themes/pinguin/

#include <string_view>

namespace guss::cli {

/// \\brief Embedded content of themes/pinguin/index.html
constexpr std::string_view DEFAULT_INDEX_TEMPLATE = R\"(
${_tpl_index}
)\";

/// \\brief Embedded content of themes/pinguin/post.html
constexpr std::string_view DEFAULT_POST_TEMPLATE = R\"(
${_tpl_post}
)\";

/// \\brief Embedded content of themes/pinguin/page.html
constexpr std::string_view DEFAULT_PAGE_TEMPLATE = R\"(
${_tpl_page}
)\";

/// \\brief Embedded content of themes/pinguin/tag.html
constexpr std::string_view DEFAULT_TAG_TEMPLATE = R\"(
${_tpl_tag}
)\";

/// \\brief Embedded content of themes/pinguin/author.html
constexpr std::string_view DEFAULT_AUTHOR_TEMPLATE = R\"(
${_tpl_author}
)\";

/// \\brief Embedded content of themes/pinguin/assets/style.css
constexpr std::string_view DEFAULT_STYLE_CSS = R\"(
${_tpl_style}
)\";

/// \\brief Embedded content of themes/pinguin/assets/main.js
constexpr std::string_view DEFAULT_MAIN_JS = R\"(
${_tpl_main_js}
)\";

/// \\brief Embedded content of themes/pinguin/assets/reveal.js
constexpr std::string_view DEFAULT_REVEAL_JS = R\"(
${_tpl_reveal_js}
)\";

/// \\brief Embedded content of themes/pinguin/assets/reading-progress.js
constexpr std::string_view DEFAULT_READING_PROGRESS_JS = R\"(
${_tpl_reading_progress_js}
)\";

/// \\brief Embedded content of themes/pinguin/assets/toc.js
constexpr std::string_view DEFAULT_TOC_JS = R\"(
${_tpl_toc_js}
)\";

/// \\brief Default guss.yaml written by guss init
constexpr std::string_view DEFAULT_CONFIG = R\"(
# ─────────────────────────────────────────────────────────────
# SITE METADATA
# Populates the \"site\" variable in every template.
# ─────────────────────────────────────────────────────────────
site:
  title: \"michaelis_m\"
  description: \"A site built with Guss\"
  url: \"https://michm.de\"
  language: \"de\"
  # logo: \"/images/logo.png\"
  # twitter: \"@myhandle\"
  navigation:
    primary:
      - label: \"Home\"
        url: \"/\"
      - label: \"Blog\"
        url: \"/blog/\"
    footer:
      - label: \"Legal Notice\"
        url: \"/impressum/\"
      - label: \"Privacy\"
        url: \"/datenschutz/\"
      - label: \"GitHub\"
        url: \"https://github.com\"
        external: true


# ─────────────────────────────────────────────────────────────
# CONTENT SOURCE
# type: rest_api  →  generic HTTP CMS (Ghost, WordPress, …)
# type: markdown  →  local filesystem
# ─────────────────────────────────────────────────────────────
source:
  type: rest_api
  base_url: \"https://ghost.michm.de/\"
  timeout_ms: 30000

  # ── Authentication ──────────────────────────────────────────
  auth:
    type: api_key          # api_key | basic | bearer | none
    param: key             # appended as ?key=<value>
    value: \"your_api_key\"
    # WordPress: type: basic, username: \"user\", password: \"app-password\"
    # Bearer:    type: bearer, token: \"mytoken\"

  # ── Pagination defaults (overrideable per endpoint) ─────────
  pagination:
    page_param:  page      # ?page=N
    limit_param: limit     # ?limit=N
    limit: 15
    # Ghost: non-null JSON value at this path = there is a next page
    json_next: \"meta.pagination.next\"
    # WordPress alternative:
    # total_pages_header: \"X-WP-TotalPages\"

  # ── Per-collection API endpoints ────────────────────────────
  # endpoint keys must match the top-level collections: keys
  endpoints:
    posts:
      path: \"ghost/api/content/posts/\"
      response_key: \"posts\"    # JSON key holding the items array
      params:                  # fixed extra query parameters
        include: \"authors,tags\"
      # WordPress: path: \"wp-json/wp/v2/posts\", response_key: \"\", params: {_embed: 1}

    pages:
      path: \"ghost/api/content/pages/\"
      response_key: \"pages\"
      params:
        include: \"authors\"

    authors:
      path: \"ghost/api/content/authors/\"
      response_key: \"authors\"

    tags:
      path: \"ghost/api/content/tags/\"
      response_key: \"tags\"

  # ── Field mappings ──────────────────────────────────────────
  # target_field: \"source.path\"
  # Dot-path:  \"authors.0\"        → authors[0]  (first array element)
  #            \"content.rendered\" → nested field (WordPress)
  # Applied before anything else; templates always see target names.
  field_maps:
    posts:
      content: \"html\"          # Ghost returns \"html\"; templates expect \"content\"
      author:  \"authors.0\"     # promote first element → singular \"author\"
      # WordPress:
      # published_at: \"date\"
      # content:      \"content.rendered\"
      # excerpt:      \"excerpt.rendered\"
      # author:       \"_embedded.author.0\"
    pages:
      content: \"html\"
      author:  \"authors.0\"

  # ── Cross-references ────────────────────────────────────────
  # Adds a \"posts\" array to each item in taxonomy collections.
  # For each tag/author item, finds all posts where the given
  # dot-path in the post data matches the item's slug.
  cross_references:
    tags:
      from: posts
      via: \"tags.slug\"         # post.tags[*].slug == tag.slug
    authors:
      from: posts
      via: \"author.slug\"       # post.author.slug == author.slug (after field_map)


# ─────────────────────────────────────────────────────────────
# MARKDOWN ADAPTER (alternative to rest_api above)
# ─────────────────────────────────────────────────────────────
# source:
#   type: markdown          # only this line differs from type: rest_api
#   recursive: true
#   collection_paths:
#     posts: ./content/posts
#     pages: ./content/pages
#   field_maps:
#     posts:
#       content: \"html\"         # cmark stores HTML as \"html\"; templates expect \"content\"
#     pages:
#       content: \"html\"
#   # ── Taxonomy synthesis ──────────────────────────────────
#   # Tags and authors have no collection_paths entry — they are
#   # synthesized from post frontmatter. Frontmatter must use
#   # explicit {name, slug} objects so match_key is transparent:
#   #   tags:
#   #     - name: C++
#   #       slug: cpp
#   #   author:
#   #     name: Alice
#   #     slug: alice
#   cross_references:
#     tags:
#       from: posts
#       via: \"tags.slug\"         # post.tags[*].slug == tag.slug
#       match_key: slug
#     authors:
#       from: posts
#       via: \"author.slug\"       # post.author.slug == author.slug
#       match_key: slug


# ─────────────────────────────────────────────────────────────
# COLLECTIONS
# Drives page generation. Keys must match endpoints: keys.
# context_key: variable name exposed to the template for this item.
# ─────────────────────────────────────────────────────────────
collections:
  posts:
    item_template:    \"post.html\"
    archive_template: \"index.html\"
    permalink:        \"/{year}/{month}/{slug}/\"
    paginate:         10
    context_key:      \"post\"

  pages:
    item_template: \"page.html\"
    permalink:     \"/{slug}/\"
    context_key:   \"page\"

  tags:
    archive_template: \"tag.html\"
    permalink:        \"/tag/{slug}/\"
    context_key:      \"tag\"

  authors:
    archive_template: \"author.html\"
    permalink:        \"/author/{slug}/\"
    context_key:      \"author\"


# ─────────────────────────────────────────────────────────────
# OUTPUT
# ─────────────────────────────────────────────────────────────
output:
  output_dir:       \"./dist\"
  generate_sitemap: true
  generate_rss:     true
  minify_html:      false
  copy_assets:      true


# ─────────────────────────────────────────────────────────────
# BUILD
# ─────────────────────────────────────────────────────────────
parallel_workers: 0      # 0 = auto-detect
log_level: \"info\"        # debug | info | warn | error)\";

} // namespace guss::cli
")

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
        tests/test_rest_adapter.cpp
        tests/test_markdown_adapter.cpp
        tests/test_generators.cpp
    )
    target_link_libraries(guss-tests PRIVATE
        guss-core
        guss-adapters
        guss-render
        guss-builder
        GTest::gtest_main
    )
    include(GoogleTest)
    gtest_discover_tests(guss-tests)
endif()

# -----------------------------------------------------------------------------
# Install
# -----------------------------------------------------------------------------
install(TARGETS guss RUNTIME DESTINATION bin)
