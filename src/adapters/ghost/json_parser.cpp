/**
 * @file json_parser.cpp
 * @brief simdjson parsing implementation for Ghost CMS API responses.
 */
#include "guss/adapters/ghost/json_parser.hpp"
#include <ctime>
#include <iomanip>
#include <sstream>

namespace guss::adapters::ghost {

std::optional<std::string> get_optional_string(simdjson::ondemand::object& obj, std::string_view key) {
    auto result = obj.find_field_unordered(key);
    if (result.error()) {
        return std::nullopt;
    }

    auto value = result.value();
    if (value.is_null()) {
        return std::nullopt;
    }

    auto str = value.get_string();
    if (str.error()) {
        return std::nullopt;
    }

    return std::string(str.value());
}

std::string get_string(simdjson::ondemand::object& obj, std::string_view key, std::string_view default_value) {
    auto opt = get_optional_string(obj, key);
    return opt.value_or(std::string(default_value));
}

std::optional<std::chrono::system_clock::time_point> parse_timestamp(std::string_view ts) {
    if (ts.empty()) {
        return std::nullopt;
    }

    std::tm tm = {};
    std::istringstream ss{std::string(ts)};

    // Ghost uses ISO 8601 format: 2024-01-15T10:30:00.000Z
    ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
    if (ss.fail()) {
        return std::nullopt;
    }

    // Convert to time_t (UTC)
    #ifdef _WIN32
    std::time_t time = _mkgmtime(&tm);
    #else
    std::time_t time = timegm(&tm);
    #endif

    if (time == -1) {
        return std::nullopt;
    }

    return std::chrono::system_clock::from_time_t(time);
}

model::Author parse_author(simdjson::ondemand::object author_obj) {
    model::Author author;

    author.id = get_string(author_obj, "id");
    author.name = get_string(author_obj, "name");
    author.slug = get_string(author_obj, "slug");
    author.email = get_optional_string(author_obj, "email");
    author.bio = get_optional_string(author_obj, "bio");
    author.profile_image = get_optional_string(author_obj, "profile_image");
    author.website = get_optional_string(author_obj, "website");
    author.twitter = get_optional_string(author_obj, "twitter");
    author.facebook = get_optional_string(author_obj, "facebook");

    return author;
}

model::Tag parse_tag(simdjson::ondemand::object tag_obj) {
    model::Tag tag;

    tag.id = get_string(tag_obj, "id");
    tag.name = get_string(tag_obj, "name");
    tag.slug = get_string(tag_obj, "slug");
    tag.description = get_optional_string(tag_obj, "description");
    tag.feature_image = get_optional_string(tag_obj, "feature_image");

    // Ghost doesn't return post_count in tag objects by default
    auto count_result = tag_obj.find_field_unordered("count");
    if (!count_result.error()) {
        auto posts_result = count_result.value().find_field_unordered("posts");
        if (!posts_result.error()) {
            auto val = posts_result.value().get_uint64();
            if (!val.error()) {
                tag.post_count = static_cast<size_t>(val.value());
            }
        }
    }

    return tag;
}

model::Post parse_post(simdjson::ondemand::object post_obj) {
    model::Post post;

    post.id = get_string(post_obj, "id");
    post.title = get_string(post_obj, "title");
    post.slug = get_string(post_obj, "slug");
    post.content_html = get_string(post_obj, "html");
    post.excerpt = get_string(post_obj, "excerpt");

    // Ghost provides plaintext for excerpt if custom_excerpt not set
    auto custom_excerpt = get_optional_string(post_obj, "custom_excerpt");
    if (custom_excerpt && !custom_excerpt->empty()) {
        post.excerpt = *custom_excerpt;
    }

    post.feature_image = get_optional_string(post_obj, "feature_image");
    post.feature_image_alt = get_optional_string(post_obj, "feature_image_alt");
    post.meta_title = get_optional_string(post_obj, "meta_title");
    post.meta_description = get_optional_string(post_obj, "meta_description");
    post.canonical_url = get_optional_string(post_obj, "canonical_url");
    post.custom_template = get_optional_string(post_obj, "custom_template");

    // Parse status
    std::string status_str = get_string(post_obj, "status", "published");
    post.status = model::post_status_from_string(status_str);

    // Parse timestamps
    auto published_at_str = get_optional_string(post_obj, "published_at");
    if (published_at_str) {
        post.published_at = parse_timestamp(*published_at_str);
    }

    auto updated_at_str = get_optional_string(post_obj, "updated_at");
    if (updated_at_str) {
        post.updated_at = parse_timestamp(*updated_at_str);
    }

    auto created_at_str = get_optional_string(post_obj, "created_at");
    if (created_at_str) {
        auto tp = parse_timestamp(*created_at_str);
        if (tp) {
            post.created_at = *tp;
        }
    }

    // Parse authors array
    auto authors_result = post_obj.find_field_unordered("authors");
    if (!authors_result.error()) {
        auto authors_array = authors_result.value().get_array();
        if (!authors_array.error()) {
            for (auto author_val : authors_array.value()) {
                auto author_obj = author_val.get_object();
                if (!author_obj.error()) {
                    post.authors.push_back(parse_author(author_obj.value()));
                }
            }
        }
    }

    // Parse tags array
    auto tags_result = post_obj.find_field_unordered("tags");
    if (!tags_result.error()) {
        auto tags_array = tags_result.value().get_array();
        if (!tags_array.error()) {
            for (auto tag_val : tags_array.value()) {
                auto tag_obj = tag_val.get_object();
                if (!tag_obj.error()) {
                    post.tags.push_back(parse_tag(tag_obj.value()));
                }
            }
        }
    }

    return post;
}

model::Page parse_page(simdjson::ondemand::object page_obj) {
    model::Page page;

    page.id = get_string(page_obj, "id");
    page.title = get_string(page_obj, "title");
    page.slug = get_string(page_obj, "slug");
    page.content_html = get_string(page_obj, "html");

    page.feature_image = get_optional_string(page_obj, "feature_image");
    page.feature_image_alt = get_optional_string(page_obj, "feature_image_alt");
    page.meta_title = get_optional_string(page_obj, "meta_title");
    page.meta_description = get_optional_string(page_obj, "meta_description");
    page.canonical_url = get_optional_string(page_obj, "canonical_url");
    page.custom_template = get_optional_string(page_obj, "custom_template");

    // Parse status
    std::string status_str = get_string(page_obj, "status", "published");
    page.status = model::page_status_from_string(status_str);

    // Parse timestamps
    auto published_at_str = get_optional_string(page_obj, "published_at");
    if (published_at_str) {
        page.published_at = parse_timestamp(*published_at_str);
    }

    auto updated_at_str = get_optional_string(page_obj, "updated_at");
    if (updated_at_str) {
        page.updated_at = parse_timestamp(*updated_at_str);
    }

    auto created_at_str = get_optional_string(page_obj, "created_at");
    if (created_at_str) {
        auto tp = parse_timestamp(*created_at_str);
        if (tp) {
            page.created_at = *tp;
        }
    }

    // Parse authors array
    auto authors_result = page_obj.find_field_unordered("authors");
    if (!authors_result.error()) {
        auto authors_array = authors_result.value().get_array();
        if (!authors_array.error()) {
            for (auto author_val : authors_array.value()) {
                auto author_obj = author_val.get_object();
                if (!author_obj.error()) {
                    page.authors.push_back(parse_author(author_obj.value()));
                }
            }
        }
    }

    return page;
}

error::Result<std::vector<model::Author>> parse_authors_response(std::string_view json) {
    simdjson::ondemand::parser parser;
    simdjson::padded_string padded(json);

    auto doc = parser.iterate(padded);
    if (doc.error()) {
        return error::make_error(
            error::ErrorCode::AdapterParseError,
            "Failed to parse JSON response",
            std::string(simdjson::error_message(doc.error()))
        );
    }

    auto root = doc.get_object();
    if (root.error()) {
        return error::make_error(
            error::ErrorCode::AdapterParseError,
            "Expected JSON object",
            ""
        );
    }

    auto authors_field = root.value().find_field_unordered("authors");
    if (authors_field.error()) {
        return error::make_error(
            error::ErrorCode::AdapterParseError,
            "Missing 'authors' field in response",
            ""
        );
    }

    auto authors_array = authors_field.value().get_array();
    if (authors_array.error()) {
        return error::make_error(
            error::ErrorCode::AdapterParseError,
            "'authors' field is not an array",
            ""
        );
    }

    std::vector<model::Author> authors;
    for (auto author_val : authors_array.value()) {
        auto author_obj = author_val.get_object();
        if (!author_obj.error()) {
            authors.push_back(parse_author(author_obj.value()));
        }
    }

    return authors;
}

error::Result<std::vector<model::Tag>> parse_tags_response(std::string_view json) {
    simdjson::ondemand::parser parser;
    simdjson::padded_string padded(json);

    auto doc = parser.iterate(padded);
    if (doc.error()) {
        return error::make_error(
            error::ErrorCode::AdapterParseError,
            "Failed to parse JSON response",
            std::string(simdjson::error_message(doc.error()))
        );
    }

    auto root = doc.get_object();
    if (root.error()) {
        return error::make_error(
            error::ErrorCode::AdapterParseError,
            "Expected JSON object",
            ""
        );
    }

    auto tags_field = root.value().find_field_unordered("tags");
    if (tags_field.error()) {
        return error::make_error(
            error::ErrorCode::AdapterParseError,
            "Missing 'tags' field in response",
            ""
        );
    }

    auto tags_array = tags_field.value().get_array();
    if (tags_array.error()) {
        return error::make_error(
            error::ErrorCode::AdapterParseError,
            "'tags' field is not an array",
            ""
        );
    }

    std::vector<model::Tag> tags;
    for (auto tag_val : tags_array.value()) {
        auto tag_obj = tag_val.get_object();
        if (!tag_obj.error()) {
            tags.push_back(parse_tag(tag_obj.value()));
        }
    }

    return tags;
}

error::Result<std::pair<std::vector<model::Post>, bool>> parse_posts_response(std::string_view json) {
    simdjson::ondemand::parser parser;
    simdjson::padded_string padded(json);

    auto doc = parser.iterate(padded);
    if (doc.error()) {
        return error::make_error(
            error::ErrorCode::AdapterParseError,
            "Failed to parse JSON response",
            std::string(simdjson::error_message(doc.error()))
        );
    }

    auto root = doc.get_object();
    if (root.error()) {
        return error::make_error(
            error::ErrorCode::AdapterParseError,
            "Expected JSON object",
            ""
        );
    }

    auto posts_field = root.value().find_field_unordered("posts");
    if (posts_field.error()) {
        return error::make_error(
            error::ErrorCode::AdapterParseError,
            "Missing 'posts' field in response",
            ""
        );
    }

    auto posts_array = posts_field.value().get_array();
    if (posts_array.error()) {
        return error::make_error(
            error::ErrorCode::AdapterParseError,
            "'posts' field is not an array",
            ""
        );
    }

    std::vector<model::Post> posts;
    for (auto post_val : posts_array.value()) {
        auto post_obj = post_val.get_object();
        if (!post_obj.error()) {
            posts.push_back(parse_post(post_obj.value()));
        }
    }

    // Check for pagination
    bool has_more = false;
    auto meta_field = root.value().find_field_unordered("meta");
    if (!meta_field.error()) {
        auto meta_obj = meta_field.value().get_object();
        if (!meta_obj.error()) {
            auto pagination = meta_obj.value().find_field_unordered("pagination");
            if (!pagination.error()) {
                auto pag_obj = pagination.value().get_object();
                if (!pag_obj.error()) {
                    auto next = pag_obj.value().find_field_unordered("next");
                    if (!next.error() && !next.value().is_null()) {
                        has_more = true;
                    }
                }
            }
        }
    }

    return std::make_pair(std::move(posts), has_more);
}

error::Result<std::pair<std::vector<model::Page>, bool>> parse_pages_response(std::string_view json) {
    simdjson::ondemand::parser parser;
    simdjson::padded_string padded(json);

    auto doc = parser.iterate(padded);
    if (doc.error()) {
        return error::make_error(
            error::ErrorCode::AdapterParseError,
            "Failed to parse JSON response",
            std::string(simdjson::error_message(doc.error()))
        );
    }

    auto root = doc.get_object();
    if (root.error()) {
        return error::make_error(
            error::ErrorCode::AdapterParseError,
            "Expected JSON object",
            ""
        );
    }

    auto pages_field = root.value().find_field_unordered("pages");
    if (pages_field.error()) {
        return error::make_error(
            error::ErrorCode::AdapterParseError,
            "Missing 'pages' field in response",
            ""
        );
    }

    auto pages_array = pages_field.value().get_array();
    if (pages_array.error()) {
        return error::make_error(
            error::ErrorCode::AdapterParseError,
            "'pages' field is not an array",
            ""
        );
    }

    std::vector<model::Page> pages;
    for (auto page_val : pages_array.value()) {
        auto page_obj = page_val.get_object();
        if (!page_obj.error()) {
            pages.push_back(parse_page(page_obj.value()));
        }
    }

    // Check for pagination
    bool has_more = false;
    auto meta_field = root.value().find_field_unordered("meta");
    if (!meta_field.error()) {
        auto meta_obj = meta_field.value().get_object();
        if (!meta_obj.error()) {
            auto pagination = meta_obj.value().find_field_unordered("pagination");
            if (!pagination.error()) {
                auto pag_obj = pagination.value().get_object();
                if (!pag_obj.error()) {
                    auto next = pag_obj.value().find_field_unordered("next");
                    if (!next.error() && !next.value().is_null()) {
                        has_more = true;
                    }
                }
            }
        }
    }

    return std::make_pair(std::move(pages), has_more);
}

} // namespace guss::adapters::ghost
