#include "guss/core/permalink.hpp"

namespace guss::core {

std::string PermalinkGenerator::expand(std::string_view pattern,
                                        const Value& data) {
    std::string result;
    result.reserve(pattern.size() + 32);

    size_t i = 0;
    while (i < pattern.size()) {
        if (pattern[i] == '{') {
            size_t end = pattern.find('}', i + 1);
            if (end == std::string_view::npos) {
                result += pattern[i++];
                continue;
            }
            std::string_view token = pattern.substr(i + 1, end - i - 1);
            const auto field = data[token];
            if (!field.is_null()) result += field.to_string();
            i = end + 1;
        } else {
            result += pattern[i++];
        }
    }
    return result;
}

std::filesystem::path PermalinkGenerator::permalink_to_path(std::string_view permalink) {
    std::string p(permalink);
    while (!p.empty() && p.front() == '/') p = p.substr(1);
    if (p.empty() || p.back() == '/') p += "index.html";
    return std::filesystem::path(p);
}

} // namespace guss::core
