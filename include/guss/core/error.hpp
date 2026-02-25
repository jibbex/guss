/**
 * \file error.hpp
 * \brief Error types and expected aliases for Guss SSG.
 *
 * \details
 * This file provides a structured error handling system using tl::expected
 * (a C++23 std::expected polyfill). It defines error codes categorized by
 * subsystem, an Error struct with code, message, and context, and convenience
 * macros for early-return error propagation.
 *
 * Example usage:
 * \code
 * #include "guss/core/error.hpp"
 *
 * guss::error::Result<int> parse_number(const std::string& str) {
 *     try {
 *         return std::stoi(str);
 *     } catch (...) {
 *         return guss::error::make_error(
 *             guss::error::ErrorCode::ContentParseError,
 *             "Invalid number format",
 *             str
 *         );
 *     }
 * }
 *
 * guss::error::Result<int> double_number(const std::string& str) {
 *     GUSS_TRY(int value, parse_number(str));
 *     return value * 2;
 * }
 * \endcode
 *
 * \author Manfred Michaelis
 * \date 2025
 */
#pragma once

#include <expected>


#include <string>
#include <string_view>
#include "tl/expected.hpp"

namespace guss::error {

/**
 * \brief Error codes categorized by subsystem.
 *
 * \details
 * - 1xx: Configuration errors
 * - 2xx: Adapter errors (CMS API issues)
 * - 3xx: Content errors (parsing, rendering)
 * - 4xx: Template errors
 * - 5xx: Filesystem errors
 * - 6xx: Pipeline errors
 * - 7xx: Watch/trigger errors
 * - 9xx: General errors
 */
enum class ErrorCode {
    ConfigNotFound = 100,
    ConfigParseError = 101,
    ConfigValidationError = 102,
    ConfigMissingField = 103,

    AdapterFetchFailed = 200,
    AdapterAuthFailed = 201,
    AdapterConnectionFailed = 202,
    AdapterParseError = 203,
    AdapterNotImplemented = 204,
    AdapterRateLimited = 205,
    AdapterBadRequest = 206,
    AdapterNotFound = 207,
    AdapterServerError = 208,

    ContentNotFound = 300,
    ContentParseError = 301,
    FrontmatterParseError = 302,
    MarkdownRenderError = 303,

    TemplateNotFound = 400,
    TemplateParseError = 401,
    TemplateRenderError = 402,

    FileNotFound = 500,
    FileReadError = 501,
    FileWriteError = 502,
    DirectoryCreateError = 503,
    DirectoryNotFound = 504,

    PipelineFetchFailed = 600,
    PipelinePrepareFailed = 601,
    PipelineRenderFailed = 602,
    PipelineWriteFailed = 603,

    WatchInitFailed = 700,
    WebhookServerFailed = 701,

    Unknown = 999
};

/**
 * \brief Structured error with code, message, and optional context.
 */
struct Error {
    ErrorCode code;
    std::string message;
    std::string context;

    Error(ErrorCode c, std::string msg, std::string ctx = "")
        : code(c), message(std::move(msg)), context(std::move(ctx)) {}

    [[nodiscard]] std::string_view code_name(ErrorCode code) const noexcept;
    [[nodiscard]] std::string format() const;
    [[nodiscard]] bool is(ErrorCode c) const noexcept { return code == c; }
};

/**
 * \brief Result type alias using tl::expected.
 * \tparam T The success value type.
 */
template<typename T>
using Result = std::expected<T, Error>;

/**
 * \brief Void result for operations that don't return a value.
 */
using VoidResult = std::expected<void, Error>;

/**
 * \brief Helper to create an unexpected error.
 * \param code The error code.
 * \param message Human-readable error message.
 * \param context Optional additional context (e.g., file path).
 * \return std::unexpected wrapping the Error.
 */
inline std::unexpected<Error> make_error(ErrorCode code, std::string message, std::string context = "") {
    return std::unexpected<Error>(Error(code, std::move(message), std::move(context)));
}

/**
 * \brief Macro for propagating errors with early return.
 * \param var Variable to assign the success value to.
 * \param expr Expression returning a Result<T>.
 */
#define GUSS_TRY(var, expr) \
    auto _guss_result_##__LINE__ = (expr); \
    if (!_guss_result_##__LINE__) return std::unexpected(_guss_result_##__LINE__.error()); \
    var = std::move(*_guss_result_##__LINE__)

/**
 * \brief Macro for propagating void results with early return.
 * \param expr Expression returning a VoidResult.
 */
#define GUSS_TRY_VOID(expr) \
    if (auto _guss_result = (expr); !_guss_result) return std::unexpected(_guss_result.error())

} // namespace guss::error
