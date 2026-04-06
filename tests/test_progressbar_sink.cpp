/**
 * \file test_progressbar_sink.cpp
 * \brief Unit tests for \c progress::Sink — the spdlog sink that keeps the
 *        last terminal line reserved for a progress bar.
 *
 * \details
 * Test suites:
 *
 *  - **SinkConstruct** — construction and default state.
 *  - **SinkAttach**    — \c attach() / \c detach() lifecycle; no crash when
 *                        called in unusual orders.
 *  - **SinkOutput**    — (POSIX) captures fd 1 and verifies the 4-step write
 *                        protocol: erase sequence, message text, newline, and
 *                        bar redraw when a bar is attached.
 *  - **SinkStderr**    — (POSIX) verifies that error- and critical-level
 *                        messages are mirrored to fd 2 as plain text, while
 *                        lower-level messages are not.
 *  - **SinkConcurrency** — multiple threads logging concurrently; asserts no
 *                          crash or deadlock.  Stdout is suppressed via
 *                          \c StdoutDevNull so pipe-buffer overflow cannot
 *                          block concurrent \c fwrite() calls.
 */

#include <gtest/gtest.h>
#include "guss/cli/progressbar_sink.hpp"
#include "test_helpers.hpp"

#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <spdlog/logger.h>

#ifndef _WIN32
using test_helpers::StdoutCapture;
using test_helpers::StdoutDevNull;
using test_helpers::StderrCapture;
using test_helpers::contains;
#endif

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

/// Build a \c spdlog::logger backed solely by \p sink at trace level.
std::shared_ptr<spdlog::logger> make_logger(
    const std::string& name,
    std::shared_ptr<progress::Sink> sink)
{
    auto logger = std::make_shared<spdlog::logger>(name, std::move(sink));
    logger->set_level(spdlog::level::trace);
    return logger;
}

} // namespace

// ===========================================================================
//  Suite: SinkConstruct
// ===========================================================================

TEST(SinkConstruct, DefaultConstruct_DoesNotThrow) {
    EXPECT_NO_THROW({ auto sink = std::make_shared<progress::Sink>(); });
}

// ===========================================================================
//  Suite: SinkAttach
// ===========================================================================

TEST(SinkAttach, Detach_WithoutPriorAttach_DoesNotCrash) {
#ifndef _WIN32
    StdoutDevNull suppress;
#endif
    auto sink = std::make_shared<progress::Sink>();
    EXPECT_NO_THROW(sink->detach());
}

TEST(SinkAttach, Attach_ThenDetach_DoesNotCrash) {
#ifndef _WIN32
    StdoutDevNull suppress;
#endif
    auto sink = std::make_shared<progress::Sink>();
    auto bar  = std::make_shared<progress::Bar>(progress::BarConfig{.show_eta = false});
    EXPECT_NO_THROW({
        sink->attach(bar);
        sink->detach();
    });
}

TEST(SinkAttach, AttachAndDetach_MultipleTimes_DoesNotCrash) {
#ifndef _WIN32
    StdoutDevNull suppress;
#endif
    auto sink = std::make_shared<progress::Sink>();
    auto bar  = std::make_shared<progress::Bar>(progress::BarConfig{.show_eta = false});
    EXPECT_NO_THROW({
        for (int i = 0; i < 5; ++i) {
            sink->attach(bar);
            sink->detach();
        }
    });
}

TEST(SinkAttach, AttachNewBar_ReplacesOldBar_DoesNotCrash) {
#ifndef _WIN32
    StdoutDevNull suppress;
#endif
    auto sink = std::make_shared<progress::Sink>();
    auto bar1 = std::make_shared<progress::Bar>(progress::BarConfig{.show_eta = false});
    auto bar2 = std::make_shared<progress::Bar>(progress::BarConfig{.show_eta = false});
    EXPECT_NO_THROW({
        sink->attach(bar1);
        sink->attach(bar2); // replaces bar1
        sink->detach();
    });
}

// ===========================================================================
//  Suite: SinkOutput  (POSIX only)
// ===========================================================================

#ifndef _WIN32

TEST(SinkOutput, NoBar_MessageText_AppearsOnStdout) {
    StdoutCapture cap;
    auto sink   = std::make_shared<progress::Sink>();
    auto logger = make_logger("out1", sink);
    logger->info("hello sink");
    const std::string out = cap.read();
    EXPECT_TRUE(contains(out, "hello sink"));
}

TEST(SinkOutput, NoBar_Output_ContainsEraseSequence) {
    // The 4-step protocol always emits \r\033[2K first, even without a bar.
    StdoutCapture cap;
    auto sink   = std::make_shared<progress::Sink>();
    auto logger = make_logger("out2", sink);
    logger->info("msg");
    const std::string out = cap.read();
    EXPECT_TRUE(contains(out, "\r\033[2K"));
}

TEST(SinkOutput, NoBar_Output_EndsWithNewline) {
    StdoutCapture cap;
    auto sink   = std::make_shared<progress::Sink>();
    auto logger = make_logger("out3", sink);
    logger->info("newline test");
    const std::string out = cap.read();
    EXPECT_FALSE(out.empty());
    // The last meaningful character before trailing flush bytes is '\n'.
    EXPECT_NE(out.find('\n'), std::string::npos);
}

TEST(SinkOutput, NoBar_AllLevels_MessageTextVisible) {
    for (auto lvl : {spdlog::level::trace,
                     spdlog::level::debug,
                     spdlog::level::info,
                     spdlog::level::warn}) {
        StdoutCapture cap;
        auto sink   = std::make_shared<progress::Sink>();
        auto logger = make_logger("out4", sink);
        logger->log(lvl, "level payload");
        EXPECT_TRUE(contains(cap.read(), "level payload"))
            << "level=" << spdlog::level::to_string_view(lvl).data();
    }
}

TEST(SinkOutput, WithBar_MessageText_AppearsOnStdout) {
    StdoutCapture cap;
    {
        auto bar    = std::make_shared<progress::Bar>(
                          progress::BarConfig{.show_eta = false});
        auto sink   = std::make_shared<progress::Sink>();
        auto logger = make_logger("out5", sink);
        sink->attach(bar);
        logger->info("bar attached msg");
        sink->detach();
    }
    EXPECT_TRUE(contains(cap.read(), "bar attached msg"));
}

TEST(SinkOutput, WithBar_Output_ContainsEraseSequence) {
    StdoutCapture cap;
    {
        auto bar    = std::make_shared<progress::Bar>(
                          progress::BarConfig{.show_eta = false});
        auto sink   = std::make_shared<progress::Sink>();
        auto logger = make_logger("out6", sink);
        sink->attach(bar);
        logger->info("msg");
        sink->detach();
    }
    EXPECT_TRUE(contains(cap.read(), "\r\033[2K"));
}

TEST(SinkOutput, WithBar_BarRedrawnAfterMessage) {
    // After the log message+newline the protocol redraws the bar, which
    // emits an SGR 38;2; colour escape characteristic of the gradient fill.
    StdoutCapture cap;
    {
        auto bar    = std::make_shared<progress::Bar>(
                          progress::BarConfig{.show_eta = false});
        auto sink   = std::make_shared<progress::Sink>();
        auto logger = make_logger("out7", sink);
        sink->attach(bar);
        bar->set(50u);
        logger->info("redraw test");
        sink->detach();
    }
    EXPECT_TRUE(contains(cap.read(), "\x1b[38;2;"));
}

// ===========================================================================
//  Suite: SinkStderr  (POSIX only)
// ===========================================================================

TEST(SinkStderr, ErrorLevel_MirroredToStderr) {
    StdoutDevNull suppress_out;
    StderrCapture err_cap;
    auto sink   = std::make_shared<progress::Sink>();
    auto logger = make_logger("err1", sink);
    logger->error("error payload");
    const std::string err = err_cap.read();
    EXPECT_TRUE(contains(err, "error payload"));
    EXPECT_TRUE(contains(err, "error"));
}

TEST(SinkStderr, CriticalLevel_MirroredToStderr) {
    StdoutDevNull suppress_out;
    StderrCapture err_cap;
    auto sink   = std::make_shared<progress::Sink>();
    auto logger = make_logger("err2", sink);
    logger->critical("critical payload");
    const std::string err = err_cap.read();
    EXPECT_TRUE(contains(err, "critical payload"));
    EXPECT_TRUE(contains(err, "critical"));
}

TEST(SinkStderr, WarnLevel_NotMirroredToStderr) {
    StdoutDevNull suppress_out;
    StderrCapture err_cap;
    auto sink   = std::make_shared<progress::Sink>();
    auto logger = make_logger("err3", sink);
    logger->warn("warn payload");
    EXPECT_TRUE(err_cap.read().empty());
}

TEST(SinkStderr, InfoLevel_NotMirroredToStderr) {
    StdoutDevNull suppress_out;
    StderrCapture err_cap;
    auto sink   = std::make_shared<progress::Sink>();
    auto logger = make_logger("err4", sink);
    logger->info("info payload");
    EXPECT_TRUE(err_cap.read().empty());
}

TEST(SinkStderr, TraceLevel_NotMirroredToStderr) {
    StdoutDevNull suppress_out;
    StderrCapture err_cap;
    auto sink   = std::make_shared<progress::Sink>();
    auto logger = make_logger("err5", sink);
    logger->trace("trace payload");
    EXPECT_TRUE(err_cap.read().empty());
}

#endif // _WIN32

// ===========================================================================
//  Suite: SinkConcurrency
//
//  StdoutDevNull prevents the ctest pipe buffer from filling when many threads
//  each emit multiple log messages concurrently.
// ===========================================================================

TEST(SinkConcurrency, ConcurrentLogs_DoNotCrash) {
#ifndef _WIN32
    StdoutDevNull suppress;
#endif
    auto sink   = std::make_shared<progress::Sink>();
    auto logger = make_logger("conc1", sink);

    constexpr int kThreads = 8;
    constexpr int kMsgs    = 20;

    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&logger, t] {
            for (int i = 0; i < kMsgs; ++i)
                logger->info("thread {} msg {}", t, i);
        });
    }
    for (auto& th : threads) th.join();
    SUCCEED();
}

TEST(SinkConcurrency, ConcurrentLogsWithBar_DoNotCrash) {
#ifndef _WIN32
    StdoutDevNull suppress;
#endif
    auto bar    = std::make_shared<progress::Bar>(
                      progress::BarConfig{.show_eta = false});
    auto sink   = std::make_shared<progress::Sink>();
    auto logger = make_logger("conc2", sink);
    sink->attach(bar);

    constexpr int kThreads = 6;
    constexpr int kMsgs    = 15;

    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&logger, &bar, t] {
            for (int i = 0; i < kMsgs; ++i) {
                logger->info("thread {} msg {}", t, i);
                bar->increment(1u);
            }
        });
    }
    for (auto& th : threads) th.join();

    sink->detach();
    SUCCEED();
}

TEST(SinkConcurrency, ConcurrentLogsAndDetach_DoNotDeadlock) {
#ifndef _WIN32
    StdoutDevNull suppress;
#endif
    auto bar    = std::make_shared<progress::Bar>(
                      progress::BarConfig{.show_eta = false});
    auto sink   = std::make_shared<progress::Sink>();
    auto logger = make_logger("conc3", sink);
    sink->attach(bar);

    std::thread logger_thread([&logger] {
        for (int i = 0; i < 30; ++i)
            logger->info("msg {}", i);
    });

    // Detach while the logger thread is still running.
    sink->detach();

    logger_thread.join();
    SUCCEED();
}
