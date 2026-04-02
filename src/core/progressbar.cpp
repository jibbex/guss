/**
 * \file progressbar.cpp
 * \brief Implementations for progress::Bar.
 */
#include "guss/cli/progressbar.hpp"

#include <cstdio>
#include <format>
#include <mutex>

namespace progress {

// ---------------------------------------------------------------------------
// Bar — construction / destruction
// ---------------------------------------------------------------------------

Bar::Bar(BarConfig config) :
    cfg_{std::move(config)},
    cells_per_pct_{cfg_.width / 100.0f},
    start_{Clock::now()}
{
    enable_vt_on_windows();
    render_locked(0u);
}

Bar::~Bar() {
    finish();
}

// ---------------------------------------------------------------------------
// Bar — public interface
// ---------------------------------------------------------------------------

void Bar::set(const uint8_t pct) {
    const uint8_t clamped = pct > 100u ? 100u : pct;
    progress_.store(clamped, std::memory_order_relaxed);
    render_locked(clamped);
}

void Bar::increment(const uint8_t step) {
    uint8_t current = progress_.load(std::memory_order_relaxed);
    uint8_t next{};
    do {
        const auto sum = current + step;
        next = (sum >= 100u) ? 100u : sum;
    } while (!progress_.compare_exchange_weak(current, next,
        std::memory_order_release, std::memory_order_relaxed));
    render_locked(next);
}

void Bar::finish() {
    if (finished_.exchange(true, std::memory_order_acq_rel)) return;
    progress_.store(100u, std::memory_order_relaxed);
    render_locked(100u);
    write_raw("\n");
}

uint8_t Bar::value() const {
    return progress_.load(std::memory_order_relaxed);
}

void Bar::set_label(const std::string_view text) {
    cfg_.label = text;
}

std::string_view Bar::label() const {
    return cfg_.label;
}

std::mutex& Bar::console_mutex() {
    return render_mutex_;
}

void Bar::redraw_unlocked() {
    render_impl(progress_.load(std::memory_order_relaxed));
}

// ---------------------------------------------------------------------------
// Bar — private: rendering
// ---------------------------------------------------------------------------

void Bar::render_locked(const uint8_t pct) const {
    std::lock_guard lk{render_mutex_};
    render_impl(pct);
}

void Bar::render_impl(const uint8_t pct) const {
    const double elapsed = Seconds{Clock::now() - start_}.count();

    std::string out;
    out.reserve(OUTPUT_BUFFER_SIZE);

    out += '\r';

    if (cfg_.show_brackets)
        out += "\x1b[2m[\x1b[0m";

    const float filled_f = cells_per_pct_ * static_cast<float>(pct);
    const int full = static_cast<int>(filled_f);
    const int frac = static_cast<int>((filled_f - static_cast<float>(full)) * 8.0f);

    for (int i = 0; i < cfg_.width; ++i) {
        const float gradient_pos = (cfg_.width > 1) ? static_cast<float>(i) / static_cast<float>(cfg_.width - 1) : 0.0f;
        const Rgb col = cfg_.color_low.lerp(cfg_.color_high, gradient_pos);

        if (i < full) {
            out += fg(col);
            out += "█";
        } else if (i == full && frac > 0) {
            out += fg(col);
            out += kEighths[frac];
        } else {
            out += "\x1b[38;2;60;60;80m\x1b[2m";
            out += cfg_.empty_char;
            out += "\x1b[0m";
        }
    }
    out += "\x1b[0m";

    if (cfg_.show_brackets)
        out += "\x1b[2m]\x1b[0m";

    if (cfg_.show_pct)
        out += std::format(" \x1b[1m{:3d}%\x1b[0m", pct);

    if (cfg_.show_eta) {
        if (pct > 0u && elapsed > 0.2) {
            const double rate = static_cast<double>(pct) / elapsed;
            const double eta = static_cast<double>(100u - pct) / rate;
            out += std::format(" \x1b[2m{}\x1b[0m", fmt_duration(eta));
        } else {
            out += " \x1b[2m--:--\x1b[0m";
        }
    }

    if (!cfg_.label.empty()) {
        out += "\x1b[1m";
        out += std::format("    {:<{}}", cfg_.label, cfg_.label_width);
        out += "\x1b[0m ";
    }

    out += "   ";
    write_raw(out);
}

// ---------------------------------------------------------------------------
// Bar — private: static helpers
// ---------------------------------------------------------------------------

std::string Bar::fg(const Rgb color) {
    return std::format("\x1b[38;2;{};{};{}m", color.r, color.g, color.b);
}

std::string Bar::fmt_duration(const double seconds) {
    const int sec = static_cast<int>(seconds);
    const int min = sec / 60;
    if (min >= 60) return std::format("{:d}h{:02d}m", min / 60, min % 60);
    return std::format("{:02d}:{:02d}", min, sec % 60);
}

void Bar::write_raw(const std::string_view text) {
    std::fwrite(text.data(), 1, text.size(), stdout);
    std::fflush(stdout);
}

void Bar::enable_vt_on_windows() {
#ifdef _WIN32
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD mode{};
    if (!GetConsoleMode(h, &mode)) return;
    SetConsoleMode(h, mode
        | ENABLE_PROCESSED_OUTPUT
        | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
#endif
}

} // namespace progress
