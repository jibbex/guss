#include "guss/core/error.hpp"

#include <string>
#include <string_view>

namespace guss::error {

std::string_view Error::code_name() const noexcept {
    switch (code) {
        case ErrorCode::ConfigNotFound: return "ConfigNotFound";
        case ErrorCode::ConfigParseError: return "ConfigParseError";
        case ErrorCode::ConfigValidationError: return "ConfigValidationError";
        case ErrorCode::ConfigMissingField: return "ConfigMissingField";
        case ErrorCode::AdapterFetchFailed: return "AdapterFetchFailed";
        case ErrorCode::AdapterAuthFailed: return "AdapterAuthFailed";
        case ErrorCode::AdapterConnectionFailed: return "AdapterConnectionFailed";
        case ErrorCode::AdapterParseError: return "AdapterParseError";
        case ErrorCode::AdapterNotImplemented: return "AdapterNotImplemented";
        case ErrorCode::AdapterRateLimited: return "AdapterRateLimited";
        case ErrorCode::AdapterBadRequest: return "AdapterBadRequest";
        case ErrorCode::AdapterNotFound: return "AdapterNotFound";
        case ErrorCode::AdapterServerError: return "AdapterServerError";
        case ErrorCode::ContentNotFound: return "ContentNotFound";
        case ErrorCode::ContentParseError: return "ContentParseError";
        case ErrorCode::FrontmatterParseError: return "FrontmatterParseError";
        case ErrorCode::MarkdownRenderError: return "MarkdownRenderError";
        case ErrorCode::TemplateNotFound: return "TemplateNotFound";
        case ErrorCode::TemplateParseError: return "TemplateParseError";
        case ErrorCode::TemplateRenderError: return "TemplateRenderError";
        case ErrorCode::FileNotFound: return "FileNotFound";
        case ErrorCode::FileReadError: return "FileReadError";
        case ErrorCode::FileWriteError: return "FileWriteError";
        case ErrorCode::DirectoryCreateError: return "DirectoryCreateError";
        case ErrorCode::DirectoryNotFound: return "DirectoryNotFound";
        case ErrorCode::PipelineFetchFailed: return "PipelineFetchFailed";
        case ErrorCode::PipelinePrepareFailed: return "PipelinePrepareFailed";
        case ErrorCode::PipelineRenderFailed: return "PipelineRenderFailed";
        case ErrorCode::PipelineWriteFailed: return "PipelineWriteFailed";
        case ErrorCode::WatchInitFailed: return "WatchInitFailed";
        case ErrorCode::WebhookServerFailed: return "WebhookServerFailed";
        default: return "Unknown";
    }
}

std::string Error::format() const {
    std::string result = "[";
    result += code_name();
    result += "] ";
    result += message;
    if (!context.empty()) {
        result += " (";
        result += context;
        result += ")";
    }
    return result;
}

}