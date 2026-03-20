/**
 * \file test_generators.cpp
 * \brief Unit tests for HTML minifier, sitemap, and RSS generators.
 */
#include <gtest/gtest.h>
#include "guss/builder/generators.hpp"
#include "guss/core/value.hpp"
#include <filesystem>
#include <unordered_map>

using namespace guss::builder;
using namespace guss::core;

// ---------------------------------------------------------------------------
// minify_html
// ---------------------------------------------------------------------------

TEST(MinifyHtml, CollapsesWhitespace) {
    EXPECT_EQ(minify_html("<p>  hello   world  </p>"), "<p> hello world </p>");
}

TEST(MinifyHtml, StripsHtmlComments) {
    EXPECT_EQ(minify_html("<p><!-- comment -->text</p>"), "<p>text</p>");
}

TEST(MinifyHtml, PreservesPreContent) {
    const std::string html = "<pre>  spaces\n\ttabs  </pre>";
    const auto result = minify_html(html);
    EXPECT_NE(result.find("  spaces"), std::string::npos) << "pre content must not be collapsed";
}

TEST(MinifyHtml, PreservesScriptContent) {
    const std::string html = "<script>var x =   1;\nvar y = 2;</script>";
    const auto result = minify_html(html);
    EXPECT_NE(result.find("var x =   1;"), std::string::npos);
}

TEST(MinifyHtml, EmptyInputReturnsEmpty) {
    EXPECT_EQ(minify_html(""), "");
}

TEST(MinifyHtml, PlainTextPassthrough) {
    EXPECT_EQ(minify_html("hello"), "hello");
}

// ---------------------------------------------------------------------------
// generate_sitemap_xml
// ---------------------------------------------------------------------------

TEST(GenerateSitemapXml, ContainsSitemapNs) {
    std::vector<std::pair<std::filesystem::path, std::string>> files;
    const auto xml = generate_sitemap_xml(files, "https://example.com");
    EXPECT_NE(xml.find("sitemaps.org/schemas/sitemap/0.9"), std::string::npos);
}

TEST(GenerateSitemapXml, RootIndexMapsToBaseUrl) {
    std::vector<std::pair<std::filesystem::path, std::string>> files;
    files.push_back({"index.html", ""});
    const auto xml = generate_sitemap_xml(files, "https://example.com");
    EXPECT_NE(xml.find("<loc>https://example.com/</loc>"), std::string::npos);
}

TEST(GenerateSitemapXml, SubpathIndexMapsToDirectory) {
    std::vector<std::pair<std::filesystem::path, std::string>> files;
    files.push_back({"posts/hello/index.html", ""});
    const auto xml = generate_sitemap_xml(files, "https://example.com");
    EXPECT_NE(xml.find("<loc>https://example.com/posts/hello/</loc>"), std::string::npos);
}

TEST(GenerateSitemapXml, EmptyFilesProducesValidXml) {
    const auto xml = generate_sitemap_xml({}, "https://example.com");
    EXPECT_NE(xml.find("<urlset"), std::string::npos);
    EXPECT_NE(xml.find("</urlset>"), std::string::npos);
}

// ---------------------------------------------------------------------------
// generate_rss_xml
// ---------------------------------------------------------------------------

static Value make_site(std::string title = "Test Site",
                       std::string desc  = "A test",
                       std::string lang  = "en") {
    std::unordered_map<std::string, Value> m;
    m["title"]       = Value(std::move(title));
    m["description"] = Value(std::move(desc));
    m["language"]    = Value(std::move(lang));
    return Value(std::move(m));
}

TEST(GenerateRssXml, ContainsRss2Header) {
    const auto xml = generate_rss_xml({}, "https://example.com", make_site());
    EXPECT_NE(xml.find("rss version=\"2.0\""), std::string::npos);
}

TEST(GenerateRssXml, ContainsSiteTitle) {
    const auto xml = generate_rss_xml({}, "https://example.com", make_site("My Blog"));
    EXPECT_NE(xml.find("<title>My Blog</title>"), std::string::npos);
}

TEST(GenerateRssXml, ArchivePagesExcluded) {
    RenderItem archive;
    archive.output_path   = std::filesystem::path("index.html");
    archive.template_name = "index.html";
    archive.context_key   = ""; // archive -- must be excluded

    std::unordered_map<std::string, Value> d;
    d["title"]        = Value(std::string("Archive"));
    d["published_at"] = Value(std::string("2024-01-01T00:00:00Z"));
    archive.data = Value(std::move(d));

    const auto xml = generate_rss_xml({archive}, "https://example.com", make_site());
    EXPECT_EQ(xml.find("<item>"), std::string::npos) << "archive page must not appear in RSS";
}

TEST(GenerateRssXml, ItemPageIncluded) {
    RenderItem item;
    item.output_path   = std::filesystem::path("posts/hello/index.html");
    item.template_name = "post.html";
    item.context_key   = "post";

    std::unordered_map<std::string, Value> d;
    d["title"]        = Value(std::string("Hello World"));
    d["published_at"] = Value(std::string("2024-06-15T10:00:00Z"));
    d["excerpt"]      = Value(std::string("A short excerpt."));
    item.data = Value(std::move(d));

    const auto xml = generate_rss_xml({item}, "https://example.com", make_site());
    EXPECT_NE(xml.find("<title>Hello World</title>"), std::string::npos);
    EXPECT_NE(xml.find("https://example.com/posts/hello/"), std::string::npos);
    EXPECT_NE(xml.find("15 Jun 2024"), std::string::npos);
}

TEST(GenerateRssXml, SortedDescendingByPublishedAt) {
    auto make_item = [](std::string date, std::string title) {
        RenderItem ri;
        ri.output_path   = std::filesystem::path(title + "/index.html");
        ri.template_name = "post.html";
        ri.context_key   = "post";
        std::unordered_map<std::string, Value> d;
        d["title"]        = Value(std::move(title));
        d["published_at"] = Value(std::move(date));
        ri.data = Value(std::move(d));
        return ri;
    };

    std::vector<RenderItem> items = {
        make_item("2024-01-01T00:00:00Z", "Old"),
        make_item("2024-06-01T00:00:00Z", "New"),
    };

    const auto xml = generate_rss_xml(items, "https://example.com", make_site());
    const auto pos_new = xml.find("New");
    const auto pos_old = xml.find("Old");
    EXPECT_LT(pos_new, pos_old) << "newer item must appear first in feed";
}
