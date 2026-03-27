/**
 * \file generators.cpp
 * \brief HTML minification and feed/sitemap generation for the Guss build pipeline.
 */
#include "guss/builder/generators.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>

namespace {

/// Convert a relative output path to an absolute site URL.
/// "posts/hello/index.html" + "https://example.com" -> "https://example.com/posts/hello/"
/// "index.html"             + "https://example.com" -> "https://example.com/"
std::string path_to_url(const std::filesystem::path& rel, std::string_view base) {
    std::string url(base);
    while (!url.empty() && url.back() == '/') url.pop_back();

    std::string r = rel.generic_string();

    if (r == "index.html") return url + "/";

    constexpr std::string_view suffix = "/index.html";
    if (r.size() > suffix.size() &&
        std::string_view(r).substr(r.size() - suffix.size()) == suffix)
        r = r.substr(0, r.size() - suffix.size()) + "/";

    return url + "/" + r;
}

/// Convert ISO 8601 date-time to RFC 822 format required by RSS 2.0.
/// Accepts "2024-01-15T10:30:00Z" or "2024-01-15". Returns input unchanged on parse failure.
std::string iso_to_rfc822(std::string_view iso) {
    if (iso.size() < 10) return std::string(iso);

    int year = 0, month = 0, day = 0;
    for (int i = 0; i < 4; ++i) year  = year  * 10 + (iso[i] - '0');
    for (int i = 5; i < 7; ++i) month = month * 10 + (iso[i] - '0');
    for (int i = 8; i < 10;++i) day   = day   * 10 + (iso[i] - '0');

    int hour = 0, min = 0, sec = 0;
    if (iso.size() >= 19 && iso[10] == 'T') {
        for (int i = 11; i < 13; ++i) hour = hour * 10 + (iso[i] - '0');
        for (int i = 14; i < 16; ++i) min  = min  * 10 + (iso[i] - '0');
        for (int i = 17; i < 19; ++i) sec  = sec  * 10 + (iso[i] - '0');
    }

    static constexpr const char* months[12] = {
        "Jan","Feb","Mar","Apr","May","Jun",
        "Jul","Aug","Sep","Oct","Nov","Dec"
    };
    if (month < 1 || month > 12) return std::string(iso);

    char buf[64];
    std::snprintf(buf, sizeof(buf), "%02d %s %04d %02d:%02d:%02d GMT",
                  day, months[month - 1], year, hour, min, sec);
    return buf;
}

/// XML-escape a string (escapes &, <, >, ", ').
std::string xml_escape(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&':  out += "&amp;";  break;
            case '<':  out += "&lt;";   break;
            case '>':  out += "&gt;";   break;
            case '"':  out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default:   out += c;        break;
        }
    }
    return out;
}

} // anonymous namespace

namespace guss::builder {

std::string minify_html(std::string_view in) {
    std::string out;
    out.reserve(in.size());

    size_t i = 0;
    // When non-empty, we are inside a verbatim block; value is the lowercase closing tag prefix.
    std::string verbatim_close; // e.g. "</pre>"

    static constexpr std::array<const char*, 4> verbatim_tags = {
        "pre", "script", "style", "textarea"
    };

    // Case-insensitive check: does in[pos..] start with tag followed by >, space, or /?
    auto tag_matches = [&](size_t pos, const char* tag) -> bool {
        const size_t len = std::strlen(tag);
        if (pos + len > in.size()) return false;
        for (size_t k = 0; k < len; ++k)
            if (std::tolower(static_cast<unsigned char>(in[pos + k])) != tag[k])
                return false;
        if (pos + len == in.size()) return true;
        const char next = in[pos + len];
        return next == '>' || next == ' ' || next == '\t' ||
               next == '\n' || next == '\r' || next == '/';
    };

    while (i < in.size()) {
        // Inside verbatim block -- output verbatim until closing tag
        if (!verbatim_close.empty()) {
            if (in[i] == '<') {
                size_t j = i + 1;
                if (j < in.size() && in[j] == '/') {
                    ++j;
                    while (j < in.size() && std::isspace(static_cast<unsigned char>(in[j]))) ++j;
                    for (const char* vtag : verbatim_tags) {
                        if (verbatim_close == (std::string("</") + vtag) && tag_matches(j, vtag)) {
                            verbatim_close.clear();
                            break;
                        }
                    }
                }
            }
            out += in[i++];
            continue;
        }

        // HTML comment: <!-- ... -->
        if (i + 3 < in.size() &&
            in[i] == '<' && in[i+1] == '!' && in[i+2] == '-' && in[i+3] == '-') {
            const auto end = in.find("-->", i + 4);
            i = (end == std::string_view::npos) ? in.size() : end + 3;
            continue;
        }

        // Opening tag -- detect verbatim-content tags
        if (in[i] == '<' && i + 1 < in.size() && in[i+1] != '/') {
            size_t j = i + 1;
            while (j < in.size() && std::isspace(static_cast<unsigned char>(in[j]))) ++j;
            for (const char* vtag : verbatim_tags) {
                if (tag_matches(j, vtag)) {
                    // Don't activate verbatim mode for self-closing tags (e.g. <script/>)
                    auto close_pos = in.find('>', j);
                    bool self_closing = (close_pos != std::string_view::npos
                                        && close_pos > 0
                                        && in[close_pos - 1] == '/');
                    if (!self_closing)
                        verbatim_close = std::string("</") + vtag;
                    break;
                }
            }
        }

        // Whitespace collapse
        if (std::isspace(static_cast<unsigned char>(in[i]))) {
            if (!out.empty() && out.back() != ' ')
                out += ' ';
            while (i < in.size() && std::isspace(static_cast<unsigned char>(in[i]))) ++i;
            continue;
        }

        out += in[i++];
    }

    return out;
}

std::string generate_sitemap_xml(
    const std::vector<std::pair<std::filesystem::path, std::string>>& files,
    std::string_view base_url)
{
    std::string xml;
    xml.reserve(files.size() * 80 + 256);
    xml += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
           "<urlset xmlns=\"http://www.sitemaps.org/schemas/sitemap/0.9\">\n";

    for (const auto& [path, _] : files) {
        if (path.empty()) continue;
        xml += "  <url>\n    <loc>";
        xml += xml_escape(path_to_url(path, base_url));
        xml += "</loc>\n    <changefreq>monthly</changefreq>\n  </url>\n";
    }

    xml += "</urlset>\n";
    return xml;
}

std::string generate_rss_xml(
    const std::vector<core::RenderItem>& items,
    std::string_view base_url,
    const core::Value& site)
{
    // Collect item pages only (archives have empty context_key)
    std::vector<const core::RenderItem*> posts;
    for (const auto& ri : items)
        if (!ri.context_key.empty() && !ri.data.is_null())
            posts.push_back(&ri);

    // Sort descending by published_at
    std::sort(posts.begin(), posts.end(), [](const auto* a, const auto* b) {
        return a->data["published_at"].to_string() > b->data["published_at"].to_string();
    });

    std::string url = std::string(base_url);
    while (!url.empty() && url.back() == '/') url.pop_back();
    const std::string title = xml_escape(site["title"].to_string());
    const std::string desc  = xml_escape(site["description"].to_string());
    const std::string lang  = site["language"].to_string();

    std::string xml;
    xml.reserve(posts.size() * 300 + 512);
    xml += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
           "<rss version=\"2.0\" xmlns:atom=\"http://www.w3.org/2005/Atom\">\n"
           "<channel>\n";
    xml += "  <title>" + title + "</title>\n";
    xml += "  <link>" + xml_escape(url) + "/</link>\n";
    xml += "  <description>" + desc + "</description>\n";
    xml += "  <atom:link href=\"" + xml_escape(url) + "/feed.xml\""
           " rel=\"self\" type=\"application/rss+xml\"/>\n";
    if (!lang.empty())
        xml += "  <language>" + xml_escape(lang) + "</language>\n";

    for (const auto* ri : posts) {
        const auto& d        = ri->data;
        const std::string item_url   = xml_escape(path_to_url(ri->output_path, base_url));
        const std::string item_title = d["title"].to_string();
        std::string item_desc = d["excerpt"].to_string();
        if (item_desc.empty() || item_desc == "null")
            item_desc = d["description"].to_string();
        if (item_desc == "null")
            item_desc.clear();
        const std::string pub = iso_to_rfc822(d["published_at"].to_string());

        xml += "  <item>\n";
        xml += "    <title>" + xml_escape(item_title) + "</title>\n";
        xml += "    <link>" + item_url + "</link>\n";
        xml += "    <guid isPermaLink=\"true\">" + item_url + "</guid>\n";
        xml += "    <description>" + xml_escape(item_desc) + "</description>\n";
        if (!pub.empty()) xml += "    <pubDate>" + pub + "</pubDate>\n";
        xml += "  </item>\n";
    }

    xml += "</channel>\n</rss>\n";
    return xml;
}

std::string generate_robots_txt(const core::config::RobotsTxtConfig& cfg) {
    std::string txt;

    if (cfg.agents.empty()) {
        txt += "User-agent: *\n";
    } else {
        for (const auto& ua : cfg.agents) {
            txt += "User-agent: " + ua.name + "\n";
            if (!ua.disallow_paths.empty())
                for (const auto& path : ua.disallow_paths)
                    txt += "Disallow: " + path + "\n";
            if (!ua.allow_paths.empty())
                for (const auto& path : ua.allow_paths)
                    txt += "Allow: " + path + "\n";
            if (ua.crawl_delay_sec.has_value())
                txt += "Crawl-delay: " + std::to_string(ua.crawl_delay_sec.value()) + "\n";
        }
    }

    if (cfg.sitemap_url.has_value()) {
        txt += "Sitemap: " + cfg.sitemap_url.value() + "\n";
    }

    return txt;
}

} // namespace guss::builder
