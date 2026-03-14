/**
 * @file test_rest_adapter.cpp
 * @brief Integration tests for RestCmsAdapter pagination strategies.
 *
 * Each test creates a local httplib::Server, configures a RestCmsAdapter
 * pointing at it, and exercises a pagination strategy end-to-end.
 */
#include <gtest/gtest.h>
#include "guss/adapters/rest/rest_adapter.hpp"
#include "guss/core/config.hpp"
#include <httplib.h>
#include <atomic>
#include <string>
#include <thread>

namespace {

/// Build a minimal RestApiConfig pointing at a local server.
/// response_key="" → bare JSON array; otherwise wrapped under that key.
guss::config::RestApiConfig make_cfg(int port,
                                     const guss::config::PaginationConfig& pag,
                                     const std::string& response_key = "items")
{
    guss::config::RestApiConfig cfg;
    cfg.base_url   = "http://127.0.0.1:" + std::to_string(port);
    cfg.timeout_ms = 5000;

    guss::config::EndpointConfig ep;
    ep.path         = "items";
    ep.response_key = response_key;
    ep.pagination   = pag;
    cfg.endpoints["items"] = ep;
    return cfg;
}

/// Build a minimal CollectionCfgMap that accepts any slug-based item.
guss::config::CollectionCfgMap make_collections()
{
    guss::config::CollectionCfgMap m;
    guss::config::CollectionConfig cc;
    cc.item_template = "item.html";
    cc.permalink     = "/{slug}/";
    cc.context_key   = "item";
    m["items"] = cc;
    return m;
}

/// RAII wrapper: starts an httplib::Server on a dynamic port, stops on destruction.
struct TestServer {
    httplib::Server svr;
    std::thread     thread;
    int             port = 0;

    void start() {
        port   = svr.bind_to_any_port("127.0.0.1");
        thread = std::thread([this]() { svr.listen_after_bind(); });
        svr.wait_until_ready();
    }

    ~TestServer() {
        svr.stop();
        if (thread.joinable()) thread.join();
    }
};

} // anonymous namespace

// ---------------------------------------------------------------------------
// total_pages_header strategy
// ---------------------------------------------------------------------------

TEST(RestAdapterPagination, TotalPagesHeader_SinglePage) {
    TestServer ts;
    ts.svr.Get("/items", [](const httplib::Request&, httplib::Response& res) {
        res.set_header("X-WP-TotalPages", "1");
        res.set_content(R"({"items":[{"slug":"a"}]})", "application/json");
    });
    ts.start();

    guss::config::PaginationConfig pag;
    pag.total_pages_header = "X-WP-TotalPages";
    pag.page_param         = "page";
    pag.limit_param        = "limit";
    pag.limit              = 10;

    auto result = guss::adapters::rest::RestCmsAdapter(
        make_cfg(ts.port, pag), {}, make_collections()).fetch_all();
    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_EQ(result->items.at("items").size(), 1u);
}

TEST(RestAdapterPagination, TotalPagesHeader_TwoPages) {
    std::atomic<int> calls{0};
    TestServer ts;
    ts.svr.Get("/items", [&](const httplib::Request& req, httplib::Response& res) {
        ++calls;
        res.set_header("X-WP-TotalPages", "2");
        res.set_content(
            req.get_param_value("page") == "2"
                ? R"({"items":[{"slug":"b"}]})"
                : R"({"items":[{"slug":"a"}]})",
            "application/json");
    });
    ts.start();

    guss::config::PaginationConfig pag;
    pag.total_pages_header = "X-WP-TotalPages";
    pag.page_param         = "page";
    pag.limit_param        = "limit";
    pag.limit              = 1;

    auto result = guss::adapters::rest::RestCmsAdapter(
        make_cfg(ts.port, pag), {}, make_collections()).fetch_all();
    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_EQ(result->items.at("items").size(), 2u);
    EXPECT_EQ(calls.load(), 2);
}
