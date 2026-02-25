/**
 * @file inja_engine.cpp
 * @brief Inja template engine implementation for Guss SSG.
 */
#include "guss/render/inja_engine.hpp"
#include <spdlog/spdlog.h>
#include <iomanip>
#include <sstream>
#include <chrono>
#include <ctime>
#include <utility>

namespace guss::render {

InjaEngine::InjaEngine(config::SiteConfig  site_config, const config::TemplateConfig& template_config)
    : site_config_(std::move(site_config))
    , template_config_(template_config)
    , env_(template_config.templates_dir.string() + "/")
{
    // Configure Inja environment
    env_.set_trim_blocks(true);
    env_.set_lstrip_blocks(true);

    // Register custom callbacks
    register_callbacks();

    spdlog::debug(fmt::format("InjaEngine initialized with templates from: {}", template_config.templates_dir.string()));
}

void InjaEngine::register_callbacks() {
    // date_format(timestamp, format) - Format a timestamp string
    env_.add_callback("date_format", 2, [](const inja::Arguments& args) -> nlohmann::json {
        std::string timestamp = args.at(0)->get<std::string>();
        std::string format = args.at(1)->get<std::string>();

        // Parse ISO 8601 timestamp, handle fractional seconds and timezone
        std::tm tm = {};
        std::string ts = timestamp;
        // Remove fractional seconds if present
        size_t dot_pos = ts.find('.');
        if (dot_pos != std::string::npos) {
            ts = ts.substr(0, dot_pos);
        }
        // Remove timezone info if present
        size_t tz_pos = ts.find('Z');
        if (tz_pos == std::string::npos) tz_pos = ts.find('+');
        if (tz_pos == std::string::npos) tz_pos = ts.find('-');
        if (tz_pos != std::string::npos && tz_pos > 0) {
            ts = ts.substr(0, tz_pos);
        }
        if (sscanf(ts.c_str(), "%d-%d-%dT%d:%d:%d", // NOLINT(*-err34-c)
                   &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
                   &tm.tm_hour, &tm.tm_min, &tm.tm_sec) == 6) {
            tm.tm_year -= 1900;
            tm.tm_mon -= 1;
        }

        return timestamp; // Return original if parse fails
    });

    // truncate(text, length, suffix) - Truncate text with ellipsis
    env_.add_callback("truncate", 2, [](const inja::Arguments& args) -> nlohmann::json {
        std::string text = args.at(0)->get<std::string>();
        size_t length = args.at(1)->get<size_t>();

        if (text.length() <= length) {
            return text;
        }

        // Find word boundary
        size_t pos = length;
        while (pos > 0 && text[pos] != ' ') {
            pos--;
        }
        if (pos == 0) pos = length;

        return text.substr(0, pos) + "...";
    });

    // truncate with custom suffix
    env_.add_callback("truncate", 3, [](const inja::Arguments& args) -> nlohmann::json {
        std::string text = args.at(0)->get<std::string>();
        size_t length = args.at(1)->get<size_t>();
        std::string suffix = args.at(2)->get<std::string>();

        if (text.length() <= length) {
            return text;
        }

        size_t pos = length;
        while (pos > 0 && text[pos] != ' ') {
            pos--;
        }
        if (pos == 0) pos = length;

        return text.substr(0, pos) + suffix;
    });

    // urlencode(text) - URL encode a string
    env_.add_callback("urlencode", 1, [](const inja::Arguments& args) -> nlohmann::json {
        std::string text = args.at(0)->get<std::string>();
        std::ostringstream escaped;
        escaped.fill('0');
        escaped << std::hex;

        for (char c : text) {
            if (std::isalnum(static_cast<unsigned char>(c)) ||
                c == '-' || c == '_' || c == '.' || c == '~') {
                escaped << c;
            } else if (c == ' ') {
                escaped << '+';
            } else {
                escaped << '%' << std::setw(2) << static_cast<int>(static_cast<unsigned char>(c));
            }
        }
        return escaped.str();
    });

    // strip_html(text) - Remove HTML tags
    env_.add_callback("strip_html", 1, [](const inja::Arguments& args) -> nlohmann::json {
        std::string text = args.at(0)->get<std::string>();
        std::string result;
        result.reserve(text.size());

        bool in_tag = false;
        for (char c : text) {
            if (c == '<') {
                in_tag = true;
            } else if (c == '>') {
                in_tag = false;
            } else if (!in_tag) {
                result += c;
            }
        }
        return result;
    });

    // reading_time(content) - Estimate reading time in minutes
    env_.add_callback("reading_time", 1, [](const inja::Arguments& args) -> nlohmann::json {
        std::string content = args.at(0)->get<std::string>();

        // Strip HTML first
        std::string text;
        bool in_tag = false;
        for (char c : content) {
            if (c == '<') in_tag = true;
            else if (c == '>') in_tag = false;
            else if (!in_tag) text += c;
        }

        // Count words (roughly)
        size_t words = 0;
        bool in_word = false;
        for (char c : text) {
            if (std::isspace(static_cast<unsigned char>(c))) {
                in_word = false;
            } else if (!in_word) {
                in_word = true;
                words++;
            }
        }

        // Average reading speed: 200 words per minute
        int minutes = static_cast<int>((words + 199) / 200);
        return std::max(1, minutes);
    });

    // json_stringify(value) - Convert to JSON string
    env_.add_callback("json", 1, [](const inja::Arguments& args) -> nlohmann::json {
        return args.at(0)->dump();
    });

    // now() - Current timestamp
    env_.add_callback("now", 0, [](inja::Arguments&) -> nlohmann::json {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        std::tm tm = *std::gmtime(&time_t);
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
        return std::string(buf);
    });
}

nlohmann::json InjaEngine::build_base_context() const {
    nlohmann::json ctx;

    // Site metadata
    ctx["site"]["title"] = site_config_.title;
    ctx["site"]["description"] = site_config_.description;
    ctx["site"]["url"] = site_config_.url;
    ctx["site"]["language"] = site_config_.language;

    if (site_config_.logo) ctx["site"]["logo"] = *site_config_.logo;
    if (site_config_.icon) ctx["site"]["icon"] = *site_config_.icon;
    if (site_config_.cover_image) ctx["site"]["cover_image"] = *site_config_.cover_image;
    if (site_config_.twitter) ctx["site"]["twitter"] = *site_config_.twitter;
    if (site_config_.facebook) ctx["site"]["facebook"] = *site_config_.facebook;

    return ctx;
}

nlohmann::json InjaEngine::build_pagination(int page_num, int total_pages, const std::string& base_path) {
    nlohmann::json pagination;
    pagination["current"] = page_num;
    pagination["total"] = total_pages;
    pagination["has_prev"] = page_num > 1;
    pagination["has_next"] = page_num < total_pages;

    if (page_num > 1) {
        if (page_num == 2) {
            pagination["prev_url"] = base_path;
        } else {
            pagination["prev_url"] = base_path + "page/" + std::to_string(page_num - 1) + "/";
        }
    }

    if (page_num < total_pages) {
        pagination["next_url"] = base_path + "page/" + std::to_string(page_num + 1) + "/";
    }

    return pagination;
}

std::string InjaEngine::get_post_template(const model::Post& post) const {
    if (post.custom_template && !post.custom_template->empty()) {
        return *post.custom_template;
    }
    return template_config_.default_post_template;
}

std::string InjaEngine::get_page_template(const model::Page& page) const {
    if (page.custom_template && !page.custom_template->empty()) {
        return *page.custom_template;
    }
    return template_config_.default_page_template;
}

error::Result<std::string> InjaEngine::render_post(
    const model::Post& post,
    const std::vector<model::Post>& all_posts,
    const std::vector<model::Tag>& all_tags
) {
    try {
        nlohmann::json ctx = build_base_context();

        // Add post data
        ctx["post"] = post.to_json();

        // Add tags
        nlohmann::json tags_json = nlohmann::json::array();
        for (const auto& tag : all_tags) {
            tags_json.push_back(tag.to_json());
        }
        ctx["tags"] = tags_json;

        // Find prev/next posts
        for (size_t i = 0; i < all_posts.size(); ++i) {
            if (all_posts[i].id == post.id) {
                if (i > 0) {
                    ctx["prev_post"] = all_posts[i - 1].to_json();
                }
                if (i + 1 < all_posts.size()) {
                    ctx["next_post"] = all_posts[i + 1].to_json();
                }
                break;
            }
        }

        std::string template_name = get_post_template(post);
        return env_.render_file(template_name, ctx);

    } catch (const inja::InjaError& e) {
        return error::make_error(
            error::ErrorCode::TemplateRenderError,
            std::string("Template render error: ") + e.what(),
            post.slug
        );
    } catch (const std::exception& e) {
        return error::make_error(
            error::ErrorCode::TemplateRenderError,
            std::string("Unexpected error: ") + e.what(),
            post.slug
        );
    }
}

error::Result<std::string> InjaEngine::render_page(
    const model::Page& page,
    const std::vector<model::Page>& all_pages
) {
    try {
        nlohmann::json ctx = build_base_context();

        // Add page data
        ctx["page"] = page.to_json();

        // Add navigation pages
        nlohmann::json nav_pages = nlohmann::json::array();
        for (const auto& p : all_pages) {
            if (p.show_in_menu) {
                nav_pages.push_back(p.to_json());
            }
        }
        ctx["navigation"] = nav_pages;

        std::string template_name = get_page_template(page);
        return env_.render_file(template_name, ctx);

    } catch (const inja::InjaError& e) {
        return error::make_error(
            error::ErrorCode::TemplateRenderError,
            std::string("Template render error: ") + e.what(),
            page.slug
        );
    } catch (const std::exception& e) {
        return error::make_error(
            error::ErrorCode::TemplateRenderError,
            std::string("Unexpected error: ") + e.what(),
            page.slug
        );
    }
}

error::Result<std::string> InjaEngine::render_index(
    const std::vector<model::Post>& posts,
    const std::vector<model::Tag>& all_tags,
    int page_num,
    int total_pages
) {
    try {
        nlohmann::json ctx = build_base_context();

        // Add posts
        nlohmann::json posts_json = nlohmann::json::array();
        for (const auto& post : posts) {
            posts_json.push_back(post.to_json());
        }
        ctx["posts"] = posts_json;

        // Add tags
        nlohmann::json tags_json = nlohmann::json::array();
        for (const auto& tag : all_tags) {
            tags_json.push_back(tag.to_json());
        }
        ctx["tags"] = tags_json;

        // Add pagination
        ctx["pagination"] = build_pagination(page_num, total_pages, "/");

        return env_.render_file(template_config_.index_template, ctx);

    } catch (const inja::InjaError& e) {
        return error::make_error(
            error::ErrorCode::TemplateRenderError,
            std::string("Template render error: ") + e.what(),
            "index"
        );
    } catch (const std::exception& e) {
        return error::make_error(
            error::ErrorCode::TemplateRenderError,
            std::string("Unexpected error: ") + e.what(),
            "index"
        );
    }
}

error::Result<std::string> InjaEngine::render_tag(
    const model::Tag& tag,
    const std::vector<model::Post>& posts,
    int page_num,
    int total_pages
) {
    try {
        nlohmann::json ctx = build_base_context();

        // Add tag data
        ctx["tag"] = tag.to_json();

        // Add posts
        nlohmann::json posts_json = nlohmann::json::array();
        for (const auto& post : posts) {
            posts_json.push_back(post.to_json());
        }
        ctx["posts"] = posts_json;

        // Add pagination
        std::string base_path = "/tag/" + tag.slug + "/";
        ctx["pagination"] = build_pagination(page_num, total_pages, base_path);

        return env_.render_file(template_config_.tag_template, ctx);

    } catch (const inja::InjaError& e) {
        return error::make_error(
            error::ErrorCode::TemplateRenderError,
            std::string("Template render error: ") + e.what(),
            "tag:" + tag.slug
        );
    } catch (const std::exception& e) {
        return error::make_error(
            error::ErrorCode::TemplateRenderError,
            std::string("Unexpected error: ") + e.what(),
            "tag:" + tag.slug
        );
    }
}

error::Result<std::string> InjaEngine::render_author(
    const model::Author& author,
    const std::vector<model::Post>& posts,
    int page_num,
    int total_pages
) {
    try {
        nlohmann::json ctx = build_base_context();

        // Add author data
        ctx["author"] = author.to_json();

        // Add posts
        nlohmann::json posts_json = nlohmann::json::array();
        for (const auto& post : posts) {
            posts_json.push_back(post.to_json());
        }
        ctx["posts"] = posts_json;

        // Add pagination
        std::string base_path = "/author/" + author.slug + "/";
        ctx["pagination"] = build_pagination(page_num, total_pages, base_path);

        return env_.render_file(template_config_.author_template, ctx);

    } catch (const inja::InjaError& e) {
        return error::make_error(
            error::ErrorCode::TemplateRenderError,
            std::string("Template render error: ") + e.what(),
            "author:" + author.slug
        );
    } catch (const std::exception& e) {
        return error::make_error(
            error::ErrorCode::TemplateRenderError,
            std::string("Unexpected error: ") + e.what(),
            "author:" + author.slug
        );
    }
}

error::Result<std::string> InjaEngine::render_template(
    const std::string& template_name,
    const nlohmann::json& data
) {
    try {
        nlohmann::json ctx = build_base_context();

        // Merge provided data
        for (auto& [key, value] : data.items()) {
            ctx[key] = value;
        }

        return env_.render_file(template_name, ctx);

    } catch (const inja::InjaError& e) {
        return error::make_error(
            error::ErrorCode::TemplateRenderError,
            std::string("Template render error: ") + e.what(),
            template_name
        );
    } catch (const std::exception& e) {
        return error::make_error(
            error::ErrorCode::TemplateRenderError,
            std::string("Unexpected error: ") + e.what(),
            template_name
        );
    }
}

} // namespace guss::render
