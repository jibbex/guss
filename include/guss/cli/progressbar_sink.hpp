/**
 * \file progressbar_sink.hpp
 * \brief spdlog sink that reserves the last terminal line for a progress bar.
 *
 * \details
 * \c ProgressBarSink enforces one invariant at all times:
 *
 *   > The last terminal line always belongs to the progress bar.
 *   > Every log message lives above it, on its own line.
 *
 * It achieves this with a strict 4-step write protocol executed atomically
 * under \c bar->console_mutex():
 *
 *   1. \c \\r\\033[2K   — carriage-return + erase-line, wiping the bar from
 *                         the last terminal line.
 *   2. log message      — ANSI-colored, formatted by spdlog's pattern formatter.
 *   3. \c \\n           — advance the cursor to a fresh, empty last line.
 *   4. bar redraw       — restore the bar on the new last line via
 *                         \c progress::Bar::redraw_unlocked().
 *
 * Because all four steps run while \c bar->console_mutex() is held, no
 * concurrent call to \c Bar::set() or \c Bar::increment() (which also acquire
 * that mutex) can render a partial bar frame in between steps 1 and 4.
 *
 * \note Error- and critical-level messages are additionally mirrored to
 *       \c stderr as plain text (no ANSI) for scripted error capture.
 */
#pragma once

#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string_view>

#include <spdlog/common.h>
#include <spdlog/details/log_msg.h>
#include <spdlog/sinks/base_sink.h>

#include "guss/cli/progressbar.hpp"

namespace progress {

class Sink final : public spdlog::sinks::base_sink<std::mutex> {
public:
    /**
     * \brief Construct the sink with a sensible default pattern.
     *
     * \details
     * The pattern \c "[%^%l%$] %v" places the level name between the color
     * range markers \c %^ / \c %$ so that \c sink_it_() can inject the correct
     * ANSI escape sequence around it.  The formatter appends \c \\n
     * automatically.
     */
    Sink() {
        set_pattern("[%^%l%$] %v");
    }

    /**
     * \brief Attach a live progress bar.
     *
     * \details
     * The sink takes shared ownership of \p bar so the bar cannot be destroyed
     * while a concurrent \c sink_it_() call is mid-protocol.  Every subsequent
     * log message follows the 4-step protocol: erase → log → newline → redraw.
     * Safe to call from any thread.
     *
     * \param bar  Shared pointer to the bar.
     */
    void attach(std::shared_ptr<progress::Bar> bar) noexcept {
        bar_.store(std::move(bar), std::memory_order_release);
    }

    /**
     * \brief Detach the progress bar.
     *
     * \details
     * Releases the sink's shared ownership.  If the bar's destructor runs as a
     * result, it does so outside any sink lock.  After this call the sink
     * writes log messages as plain stdout output.  Safe to call from any thread.
     */
    void detach() noexcept {
        bar_.store({}, std::memory_order_release);
    }

protected:
    // -------------------------------------------------------------------------
    // spdlog::sinks::base_sink interface
    // -------------------------------------------------------------------------

    /**
     * \brief Format \p msg and execute the 4-step write protocol.
     *
     * \details
     * Called by \c base_sink::log() with \c mutex_ already held, so concurrent
     * \c sink_it_() calls are serialized.  The additional acquisition of
     * \c bar->console_mutex() (= \c Bar::render_mutex_) inside this function
     * is safe because:
     *
     *  - The sink always acquires its own \c mutex_ first, then the bar's
     *    \c render_mutex_.
     *  - \c Bar::set() / \c increment() only ever acquire \c render_mutex_,
     *    never the sink's \c mutex_.
     *
     * There is therefore no circular wait and no deadlock risk.
     */
    void sink_it_(const spdlog::details::log_msg& msg) override {
        // Format the message; the pattern formatter sets msg.color_range_start
        // and msg.color_range_end to delimit the region that needs an ANSI color.
        spdlog::memory_buf_t buf;
        formatter_->format(msg, buf);

        // Atomically acquire shared ownership so the bar cannot be destroyed
        // between the load and the end of write_protocol_().
        std::shared_ptr<progress::Bar> bar = bar_.load(std::memory_order_acquire);

        if (bar) {
            // Acquire bar's render mutex for the entire 4-step protocol.
            // No bar render can slip in between the steps while we hold this.
            std::lock_guard lk{bar->console_mutex()};
            write_protocol_(buf, msg, bar.get());
        } else {
            write_protocol_(buf, msg, nullptr);
        }

        // Mirror errors / critical to stderr as plain text so that
        // `2>errors.log` captures them even when stdout is a terminal.
        if (msg.level >= spdlog::level::err) {
            const auto lvl_name = spdlog::level::to_string_view(msg.level);
            std::fwrite("[",             1, 1,                stderr);
            std::fwrite(lvl_name.data(), 1, lvl_name.size(), stderr);
            std::fwrite("] ",            1, 2,                stderr);
            std::fwrite(msg.payload.data(), 1, msg.payload.size(), stderr);
            std::fwrite("\n",            1, 1,                stderr);
            std::fflush(stderr);
        }
    }

    void flush_() override {
        std::fflush(stdout);
        std::fflush(stderr);
    }

private:
    // -------------------------------------------------------------------------
    // ANSI level-color table (index == spdlog::level::level_enum value)
    // -------------------------------------------------------------------------

    static constexpr std::array<const char*, 7> LEVEL_COLORS{{
        "\033[37m",        // trace    — white
        "\033[36m",        // debug    — cyan
        "\033[32m",        // info     — green
        "\033[33;1m",      // warn     — yellow bold
        "\033[31;1m",      // error    — red bold
        "\033[97;41;1m",   // critical — bright-white on red background
        "\033[0m",         // off      — reset
    }};

    static constexpr const char* RESET = "\033[0m";

    // -------------------------------------------------------------------------

    /**
     * \brief Execute the 4-step write protocol to stdout.
     *
     * \details
     * When \p bar is non-null this must be called while the caller holds
     * \c bar->console_mutex(), guaranteeing atomicity of all four steps.
     *
     * \param buf   Formatted message from the pattern formatter (includes \\n).
     * \param msg   Original log message; provides level and color range offsets.
     * \param bar   Attached bar (may be \c nullptr).
     */
    void write_protocol_(const spdlog::memory_buf_t& buf,
                         const spdlog::details::log_msg& msg,
                         progress::Bar* bar) const
    {
        const std::string_view text{buf.data(), buf.size()};
        const auto level_idx = static_cast<std::size_t>(msg.level);
        const char* color    = (level_idx < LEVEL_COLORS.size())
                                   ? LEVEL_COLORS[level_idx]
                                   : RESET;

        // Step 1 — erase the bar from the last terminal line.
        std::fwrite("\r\033[2K", 1, 5, stdout);

        // Step 2 — write the formatted, ANSI-colored log message.
        //
        // The pattern formatter sets color_range_start / color_range_end to the
        // byte offsets of the %^ / %$ region so we inject the ANSI code there.
        if (msg.color_range_end > msg.color_range_start) {
            // text before the color range
            std::fwrite(text.data(),
                        1, msg.color_range_start, stdout);
            // level color
            std::fwrite(color, 1, std::strlen(color), stdout);
            // colored region (level name)
            std::fwrite(text.data() + msg.color_range_start,
                        1, msg.color_range_end - msg.color_range_start, stdout);
            // reset
            std::fwrite(RESET, 1, std::strlen(RESET), stdout);
            // remainder of the message
            std::fwrite(text.data() + msg.color_range_end,
                        1, text.size() - msg.color_range_end, stdout);
        } else {
            std::fwrite(text.data(), 1, text.size(), stdout);
        }

        // Step 3 — ensure the message ends with \n so the cursor lands on a
        // fresh, empty last line.  The pattern formatter always appends \n, but
        // guard against a custom formatter that omits it.
        if (text.empty() || text.back() != '\n')
            std::fputc('\n', stdout);

        // Step 4 — redraw the bar on the new last line.
        //          bar->console_mutex() is already held by our caller.
        if (bar)
            bar->redraw_unlocked();

        std::fflush(stdout);
    }

    // Shared ownership of the active bar; null when no bar is attached.
    // std::atomic<shared_ptr<T>> is the C++20 non-deprecated alternative to
    // the std::atomic_load / std::atomic_store free-function overloads.
    std::atomic<std::shared_ptr<progress::Bar>> bar_;
};

} // namespace progress

