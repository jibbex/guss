#include "guss/adapters/adapter.hpp"
#include "guss/core/error.hpp"

#include "httplib.h"
#include <string>


namespace guss::adapters {
std::optional<std::unexpected<error::Error>> get_error(const httplib::Response& res) {
    if (!res) {
        return error::make_error(
            error::ErrorCode::AdapterConnectionFailed,
            "HTTP request failed",
            httplib::to_string(res.error())
        );
    }

    switch (res.status) {
        case 400: {
            return error::make_error(
                error::ErrorCode::AdapterBadRequest,
                "Bad request to Ghost API",
                "HTTP 400"
            );
        }
        case 401: [[fallthrough]]
        case 403: {
            return error::make_error(
                error::ErrorCode::AdapterAuthFailed,
                "Authentication failed",
                "HTTP " + std::to_string(res.status)
            );
        }
        case 404: {
            return error::make_error(
                error::ErrorCode::AdapterNotFound,
                "Endpoint not found in Ghost API",
                "HTTP 404"
            );
        }
        case 429: {
            return error::make_error(
                error::ErrorCode::AdapterRateLimited,
                "Rate limited by Ghost API",
                "HTTP 429"
            );
        }
        case 500: {
            return error::make_error(
                error::ErrorCode::AdapterServerError,
                "Ghost API server error",
                "HTTP 500"
            );
        }
        default: {
            if (res->status != 200) {
                return error::make_error(
                    error::ErrorCode::AdapterFetchFailed,
                    "Ghost API returned error",
                    "HTTP " + std::to_string(res->status)
                );
            }
        }
    }

    return std::nullopt;
}

}