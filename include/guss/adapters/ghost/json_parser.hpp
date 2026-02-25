/**
 * @file json_parser.hpp
 * @brief simdjson parsing helpers for Ghost CMS API responses.
 */
#pragma once

#include "guss/core/model/post.hpp"
#include "guss/core/model/page.hpp"
#include "guss/core/model/author.hpp"
#include "guss/core/model/taxonomy.hpp"
#include "guss/core/error.hpp"
#include <simdjson.h>
#include <string>
#include <string_view>
#include <vector>
#include <chrono>
#include <optional>

namespace guss::adapters::ghost {

/**
 * @brief Helper to safely get optional string from simdjson object.
 */
std::optional<std::string> get_optional_string(simdjson::ondemand::object& obj, std::string_view key);

/**
 * @brief Helper to safely get string with default from simdjson object.
 */
std::string get_string(simdjson::ondemand::object& obj, std::string_view key, std::string_view default_value = "");

/**
 * @brief Parse ISO 8601 timestamp to system_clock time_point.
 */
std::optional<std::chrono::system_clock::time_point> parse_timestamp(std::string_view ts);

/**
 * @brief Parse a Ghost author object from simdjson.
 */
model::Author parse_author(simdjson::ondemand::object author_obj);

/**
 * @brief Parse a Ghost tag object from simdjson.
 */
model::Tag parse_tag(simdjson::ondemand::object tag_obj);

/**
 * @brief Parse a Ghost post object from simdjson.
 */
model::Post parse_post(simdjson::ondemand::object post_obj);

/**
 * @brief Parse a Ghost page object from simdjson.
 */
model::Page parse_page(simdjson::ondemand::object page_obj);

/**
 * @brief Parse an array of authors from Ghost API response.
 */
error::Result<std::vector<model::Author>> parse_authors_response(std::string_view json);

/**
 * @brief Parse an array of tags from Ghost API response.
 */
error::Result<std::vector<model::Tag>> parse_tags_response(std::string_view json);

/**
 * @brief Parse an array of posts from Ghost API response.
 * @return Tuple of (posts, has_more_pages)
 */
error::Result<std::pair<std::vector<model::Post>, bool>> parse_posts_response(std::string_view json);

/**
 * @brief Parse an array of pages from Ghost API response.
 * @return Tuple of (pages, has_more_pages)
 */
error::Result<std::pair<std::vector<model::Page>, bool>> parse_pages_response(std::string_view json);

} // namespace guss::adapters::ghost
