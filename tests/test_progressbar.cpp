/**
 * \file test_progressbar.cpp
 * \brief Unit tests for the progress namespace — Rgb, Bar state machine,
 *        clamping invariants, finish() idempotency, concurrent safety,
 *        and stdout output structure.
 *
 * \details
 * The test surface is divided into five suites:
 *
 *  - **RgbLerp**      — pure arithmetic on \c Rgb::lerp(); no I/O involved.
 *  - **BarState**     — \c Bar::set(), \c Bar::increment(), and \c Bar::value()
 *                       in isolation; clamping and accumulation invariants.
 *  - **BarLifecycle** — construction, RAII destruction, and the idempotency
 *                       guarantee of \c Bar::finish().
 *  - **BarConcurrency** — multiple threads calling \c increment()
 *                         simultaneously; asserts the final value is exactly
 *                         100 and that no data race occurs.
 *  - **BarSetLabel**  — label mutation via \c set_label(); state orthogonality
 *                       and output verification.
 *  - **BarOutput**    — captures fd 1 into a POSIX pipe and asserts that each
 *                       rendered frame contains the expected structural tokens:
 *                       carriage return, SGR colour escape, block glyph, and the
 *                       terminating newline emitted by \c finish().
 *
 * \note The output capture tests use POSIX \c pipe(2) / \c dup2(2) and are
 *       therefore skipped automatically on non-POSIX platforms via a
 *       compile-time guard.
 */

#include <gtest/gtest.h>
#include "guss/cli/progressbar.hpp"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <unistd.h>
#endif

using progress::Bar;
using progress::BarConfig;
using progress::Rgb;

// ===========================================================================
//  Helpers
// ===========================================================================

namespace {

#ifndef _WIN32
class StdoutCapture {
public:
    StdoutCapture() {
        std::fflush(stdout);
        saved_fd_ = ::dup(1);
        EXPECT_NE(saved_fd_, -1);
        EXPECT_EQ(::pipe(fds_), 0);
        EXPECT_EQ(::dup2(fds_[1], 1), 1);
        ::close(fds_[1]);
    }

    ~StdoutCapture() {
        std::fflush(stdout);
        ::dup2(saved_fd_, 1);
        ::close(saved_fd_);
    }

    [[nodiscard]] std::string read() const {
        std::fflush(stdout);
        std::string result;
        char buf[256];
        ssize_t n;
        while ((n = ::read(fds_[0], buf, sizeof(buf))) > 0)
            result.append(buf, static_cast<size_t>(n));
        ::close(fds_[0]);
        return result;
    }

private:
    int saved_fd_{};
    int fds_[2]{};
};
#endif

bool contains(const std::string &haystack, std::string_view needle) {
    return haystack.find(needle) != std::string::npos;
}

} // namespace

// ===========================================================================
//  Suite: RgbLerp
// ===========================================================================

TEST(RgbLerp, Factor0_ReturnsSelf) {
    constexpr Rgb a{10, 20, 30};
    constexpr Rgb b{200, 210, 220};
    const Rgb result = a.lerp(b, 0.0f);
    EXPECT_EQ(result.r, a.r);
    EXPECT_EQ(result.g, a.g);
    EXPECT_EQ(result.b, a.b);
}

TEST(RgbLerp, Factor1_ReturnsOther) {
    constexpr Rgb a{10, 20, 30};
    constexpr Rgb b{200, 210, 220};
    const Rgb result = a.lerp(b, 1.0f);
    EXPECT_EQ(result.r, b.r);
    EXPECT_EQ(result.g, b.g);
    EXPECT_EQ(result.b, b.b);
}

TEST(RgbLerp, Factor0_5_ApproximatelyMidpoint) {
    constexpr Rgb a{0, 0, 0};
    constexpr Rgb b{200, 100, 50};
    const Rgb result = a.lerp(b, 0.5f);
    EXPECT_NEAR(static_cast<int>(result.r), 100, 1);
    EXPECT_NEAR(static_cast<int>(result.g), 50, 1);
    EXPECT_NEAR(static_cast<int>(result.b), 25, 1);
}

TEST(RgbLerp, IdenticalColours_AllFactors_ReturnSameColour) {
    constexpr Rgb a{128, 64, 32};
    for (float f: {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
        const Rgb result = a.lerp(a, f);
        EXPECT_EQ(result.r, a.r) << "factor=" << f;
        EXPECT_EQ(result.g, a.g) << "factor=" << f;
        EXPECT_EQ(result.b, a.b) << "factor=" << f;
    }
}

TEST(RgbLerp, Monotonic_RedChannelIncreases) {
    constexpr Rgb black{0, 0, 0};
    constexpr Rgb white{255, 255, 255};
    int prev = 0;
    for (int i = 0; i <= 10; ++i) {
        const Rgb r = black.lerp(white, static_cast<float>(i) / 10.0f);
        EXPECT_GE(static_cast<int>(r.r), prev) << "factor=" << i;
        prev = static_cast<int>(r.r);
    }
}

// ===========================================================================
//  Suite: BarState
// ===========================================================================

TEST(BarState, DefaultConstruct_ValueIsZero) {
    Bar bar{{.label = ""}};
    EXPECT_EQ(bar.value(), 0u);
}

TEST(BarState, Set_StoresExactValue) {
    Bar bar{{.label = ""}};
    bar.set(50u);
    EXPECT_EQ(bar.value(), 50u);
}

TEST(BarState, Set_ClampsAbove100) {
    Bar bar{{.label = ""}};
    bar.set(200u);
    EXPECT_EQ(bar.value(), 100u);
}

TEST(BarState, Set_BoundaryValues) {
    Bar bar{{.label = ""}};
    bar.set(0u);
    EXPECT_EQ(bar.value(), 0u);
    bar.set(100u);
    EXPECT_EQ(bar.value(), 100u);
}

TEST(BarState, Increment_AccumulatesCorrectly) {
    Bar bar{{.label = ""}};
    bar.increment(10u);
    bar.increment(10u);
    bar.increment(10u);
    EXPECT_EQ(bar.value(), 30u);
}

TEST(BarState, Increment_SaturatesAt100) {
    Bar bar{{.label = ""}};
    bar.set(90u);
    bar.increment(20u);
    EXPECT_EQ(bar.value(), 100u);
}

TEST(BarState, Increment_DefaultStep_IsOnePercent) {
    Bar bar{{.label = ""}};
    bar.increment();
    EXPECT_EQ(bar.value(), 1u);
}

// ===========================================================================
//  Suite: BarLifecycle
// ===========================================================================

TEST(BarLifecycle, ConstructAndDestroy_DoesNotThrow) {
    EXPECT_NO_THROW({ Bar bar{{.label = ""}}; });
}

TEST(BarLifecycle, Finish_SetsValueTo100) {
    Bar bar{{.label = ""}};
    bar.set(40u);
    bar.finish();
    EXPECT_EQ(bar.value(), 100u);
}

TEST(BarLifecycle, Finish_IsIdempotent_NoThrow) {
    Bar bar{{.label = ""}};
    EXPECT_NO_THROW({
        bar.finish();
        bar.finish();
        bar.finish();
    });
}

TEST(BarLifecycle, Finish_ThenDestructor_DoesNotThrow) {
    EXPECT_NO_THROW({
        Bar bar{{.label = ""}};
        bar.finish();
    });
}

TEST(BarLifecycle, SetAfterFinish_DoesNotCrash) {
    Bar bar{{.label = ""}};
    bar.finish();
    EXPECT_NO_THROW(bar.set(50u));
}

// ===========================================================================
//  Suite: BarConcurrency
// ===========================================================================

TEST(BarConcurrency, ConcurrentIncrement_FinalValueClamped) {
    Bar bar{{.label = ""}};

    constexpr int kThreads = 10;
    constexpr int kIter = 10;
    constexpr uint8_t kStep = 1u; // 10 threads × 10 × 1 % = 100 % exactly

    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&bar] {
            for (int i = 0; i < kIter; ++i)
                bar.increment(kStep);
        });
    }
    for (auto &th: threads)
        th.join();

    EXPECT_EQ(bar.value(), 100u);
}

TEST(BarConcurrency, ConcurrentIncrement_NeverExceeds100) {
    Bar bar{{.label = ""}};
    std::atomic<bool> violation{false};

    constexpr int kThreads = 8;
    constexpr int kIter = 50;

    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&bar, &violation] {
            for (int i = 0; i < kIter; ++i) {
                bar.increment(1u);
                if (bar.value() > 100u)
                    violation.store(true, std::memory_order_relaxed);
            }
        });
    }
    for (auto &th: threads)
        th.join();

    EXPECT_FALSE(violation.load());
}

TEST(BarConcurrency, ConcurrentSetAndIncrement_DoesNotCrash) {
    Bar bar{{.label = ""}};

    std::thread setter([&bar] {
        for (uint8_t i = 0; i <= 100u; ++i)
            bar.set(i);
    });
    std::thread incrementer([&bar] {
        for (int i = 0; i < 50; ++i)
            bar.increment(1u);
    });

    setter.join();
    incrementer.join();
    EXPECT_NO_FATAL_FAILURE(bar.finish());
}

// ===========================================================================
//  Suite: BarSetLabel
// ===========================================================================

TEST(BarSetLabel, SetLabel_DoesNotAffectProgress) {
    Bar bar{{.label = "Before", .show_eta = false}};
    bar.set(60u);
    bar.set_label("After");
    EXPECT_EQ(bar.value(), 60u);
}

TEST(BarSetLabel, SetLabel_LabelReflectsNewValue) {
    Bar bar{{.label = "Before", .show_eta = false}};
    bar.set_label("After");
    EXPECT_EQ(bar.label(), "After");
}

TEST(BarSetLabel, SetLabel_EmptyString_ClearsLabel) {
    Bar bar{{.label = "Remove me", .show_eta = false}};
    bar.set_label("");
    EXPECT_EQ(bar.label(), "");
}

TEST(BarSetLabel, SetLabel_CalledMultipleTimes_ReflectsLastValue) {
    Bar bar{{.label = "", .show_eta = false}};
    bar.set_label("Step 1");
    bar.increment(25u);
    bar.set_label("Step 2");
    bar.increment(25u);
    bar.set_label("Step 3");
    EXPECT_EQ(bar.label(), "Step 3");
}

#ifndef _WIN32

TEST(BarSetLabel, SetLabel_NewLabelAppearsInOutput) {
    StdoutCapture cap;
    {
        Bar bar{{.width = 10, .label = "Old", .show_eta = false}};
        bar.set_label("New");
        bar.set(50u);
    }
    EXPECT_TRUE(contains(cap.read(), "New"));
}

TEST(BarSetLabel, SetLabel_OldLabelReplacedInOutput) {
    StdoutCapture cap;
    {
        Bar bar{{.width = 10, .label = "Old", .show_eta = false}};
        bar.set(30u);
        bar.set_label("New");
        bar.set(60u);
        bar.finish();
    }
    const std::string out = cap.read();
    const auto old_pos = out.rfind("Old");
    const auto new_pos = out.rfind("New");
    EXPECT_NE(new_pos, std::string::npos);
    if (old_pos != std::string::npos)
        EXPECT_GT(new_pos, old_pos);
}

#endif // _WIN32

// ===========================================================================
//  Suite: BarOutput  (POSIX only)
// ===========================================================================

#ifndef _WIN32

TEST(BarOutput, EachFrame_BeginsWithCarriageReturn) {
    StdoutCapture cap;
    {
        Bar bar{{.width = 20, .label = "", .show_eta = false}};
        bar.set(50u);
    }
    EXPECT_TRUE(contains(cap.read(), "\r"));
}

TEST(BarOutput, Output_ContainsSgrColourEscape) {
    StdoutCapture cap;
    {
        Bar bar{{.width = 10, .label = "", .show_eta = false}};
        bar.set(50u);
    }
    EXPECT_TRUE(contains(cap.read(), "\x1b[38;2;"));
}

TEST(BarOutput, Output_ContainsFullBlockGlyph_WhenNonZeroProgress) {
    StdoutCapture cap;
    {
        Bar bar{{.width = 20, .label = "", .show_eta = false}};
        bar.set(50u);
    }
    EXPECT_TRUE(contains(cap.read(), "█"));
}

TEST(BarOutput, Finish_EmitsNewline) {
    StdoutCapture cap;
    {
        Bar bar{{.width = 10, .label = "", .show_eta = false}};
        bar.finish();
    }
    EXPECT_TRUE(contains(cap.read(), "\n"));
}

TEST(BarOutput, Finish_EmitsNewlineExactlyOnce) {
    StdoutCapture cap;
    {
        Bar bar{{.width = 10, .label = "", .show_eta = false}};
        bar.finish();
        bar.finish();
    }
    const std::string out = cap.read();
    EXPECT_EQ(std::count(out.begin(), out.end(), '\n'), 1L);
}

TEST(BarOutput, Label_AppearsInOutput) {
    StdoutCapture cap;
    {
        Bar bar{{.width = 10, .label = "Compiling", .show_eta = false}};
        bar.set(10u);
    }
    EXPECT_TRUE(contains(cap.read(), "Compiling"));
}

TEST(BarOutput, Brackets_PresentWhenEnabled) {
    StdoutCapture cap;
    {
        Bar bar{{.width = 10, .label = "", .show_brackets = true, .show_eta = false}};
        bar.set(50u);
    }
    const std::string out = cap.read();
    EXPECT_TRUE(contains(out, "["));
    EXPECT_TRUE(contains(out, "]"));
}

TEST(BarOutput, ZeroProgress_NoFullBlockGlyph) {
    StdoutCapture cap;
    {
        Bar bar{{.width = 10, .label = "", .show_eta = false}};
        bar.finish();
    }
    EXPECT_FALSE(contains(cap.read(), "█"));
}

#endif // _WIN32
