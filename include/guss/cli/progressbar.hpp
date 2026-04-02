/**
 * \file progressbar.hpp
 * \brief Lock-free, gradient-colored terminal progress bar for the Guss CLI.
 *
 * \details
 * Provides a single public class, \c progress::Bar, that renders a smooth
 * progress bar directly to stdout using ANSI/VT escape sequences.
 *
 * Design goals:
 *  - **Lock-free inner state**: progress is stored as \c std::atomic<uint8_t>
 *    in the integer range [0, 100]. The CAS-loop in \c increment() operates
 *    entirely on integers, making float accumulation error structurally
 *    impossible. Only the final stdout write is serialized.
 *  - **Sub-cell smoothness**: Unicode eighth-block glyphs (▏▎▍▌▋▊▉█) give
 *    eight distinct fill levels per terminal column, yielding effectively
 *    \c width × 8 visual positions.
 *  - **24-bit RGB gradient**: each filled cell is individually colored by
 *    linear interpolation between \c BarConfig::color_low and
 *    \c BarConfig::color_high, so the gradient always spans the full bar
 *    regardless of current progress.
 *  - **Zero external dependencies**: only the C++23 standard library is used.
 *  - **Cross-platform VT support**: on Windows, \c SetConsoleMode enables
 *    \c ENABLE_VIRTUAL_TERMINAL_PROCESSING at construction; the call is a
 *    no-op on POSIX systems.
 *
 * \note Declarations only — all method bodies live in \c progressbar.cpp.
 *       The header pulls in \c <windows.h> on Win32 targets only, guarded by
 *       \c WIN32_LEAN_AND_MEAN.
 */
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace progress {

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

/**
 * \brief Reserved capacity of the per-frame output string (in bytes).
 *
 * \details
 * Each call to \c Bar::render_impl() writes a single line to stdout.
 * The string is pre-allocated to this capacity to avoid heap reallocations
 * on every frame. The default value of 768 is sufficient for a bar width of
 * up to ~50 columns with brackets, percentage, and ETA enabled, accounting
 * for the overhead of ANSI escape sequences and multi-byte UTF-8 block glyphs.
 *
 * Increase this constant if a custom \c BarConfig produces noticeably wider
 * output (e.g. very long labels combined with a wide bar).
 */
static constexpr size_t OUTPUT_BUFFER_SIZE = 768;

// ---------------------------------------------------------------------------
// Rgb
// ---------------------------------------------------------------------------

/**
 * \struct Rgb
 * \brief 24-bit sRGB color with linear interpolation.
 *
 * \details
 * Used by \c BarConfig to specify the gradient endpoints and by
 * \c Bar::render_impl() to derive per-cell fill colors.
 *
 * All arithmetic is performed in \c float and clamped by the truncating cast
 * back to \c uint8_t; no gamma correction is applied. For typical terminal
 * palette distances the resulting gradient is visually smooth without the
 * added complexity of linearised blending.
 */
struct Rgb {
    uint8_t r{}; ///< Red channel in [0, 255].
    uint8_t g{}; ///< Green channel in [0, 255].
    uint8_t b{}; ///< Blue channel in [0, 255].

    /**
     * \brief Linearly interpolate toward \p other by \p factor.
     *
     * \param other  Target color.
     * \param factor Blend weight in [0, 1]; 0 returns \c *this, 1 returns \p other.
     * \retval Rgb   Interpolated color; channels are computed independently.
     */
    [[nodiscard]] constexpr Rgb lerp(const Rgb &other, const float factor) const noexcept {
        auto li = [](uint8_t a, uint8_t b, float t) noexcept -> uint8_t {
            return static_cast<uint8_t>(static_cast<float>(a) + (static_cast<float>(b) - static_cast<float>(a)) * t);
        };
        return {li(r, other.r, factor), li(g, other.g, factor), li(b, other.b, factor)};
    }
};

// ---------------------------------------------------------------------------
// BarConfig
// ---------------------------------------------------------------------------

/**
 * \struct BarConfig
 * \brief Aggregate configuration for \c Bar.
 *
 * \details
 * All members have sensible defaults so a zero-argument \c Bar{} produces a
 * usable emerald-to-sky-blue bar with brackets, percentage, and ETA enabled.
 * Use C++20 designated initializers to override individual fields:
 *
 * \code
 * progress::Bar bar{{
 *     .width      = 50,
 *     .label      = "Rendering",
 *     .color_low  = { 255, 140, 0  },
 *     .color_high = { 220, 30,  255 },
 *     .show_eta   = false,
 * }};
 * \endcode
 *
 * \note \c label is printed in bold before the left bracket. Leave it empty
 *       to omit the label entirely and save the column space.
 */
struct BarConfig {
    /** \brief Inner width of the bar in terminal columns (excludes brackets). */
    uint16_t width{40};

    /** \brief Label width of the prepended text. */
    uint16_t label_width{10};

    /** \brief Optional prefix label printed in bold before the bar. Empty = omitted. */
    std::string label;

    /** \brief Gradient color at 0 % (left edge of the filled region). */
    Rgb color_low{30, 215, 96};

    /** \brief Gradient color at 100 % (right edge of the filled region). */
    Rgb color_high{80, 180, 255};

    /** \brief UTF-8 glyph used for unfilled columns. Defaults to \c ░. */
    std::string_view empty_char{"░"};

    /** \brief Render dim \c [ \c ] delimiters around the bar. */
    bool show_brackets{true};

    /** \brief Render the current percentage to the right of the bar. */
    bool show_pct{true};

    /**
     * \brief Render an estimated time remaining to the right of the bar.
     *
     * \details
     * ETA is derived from the simple rate estimate \c (100-pct)/(pct/elapsed).
     * It is suppressed until at least 0.2 s have elapsed and at least 1 %
     * progress has been recorded, so the display does not flicker with
     * nonsensical values at the very start of a task.
     */
    bool show_eta{true};
};

// ---------------------------------------------------------------------------
// Bar
// ---------------------------------------------------------------------------

/**
 * \class Bar
 * \brief Lock-free terminal progress bar with 24-bit gradient coloring.
 *
 * \details
 * Progress is represented as an integer percentage [0, 100] stored in a
 * \c std::atomic<uint8_t>. The public API operates in the same unit, so
 * there is no float-to-integer conversion in the hot path and no accumulation
 * error is possible.
 *
 * \c Bar manages two separate concurrency concerns:
 *
 *  1. **State updates** — \c progress_ is a \c std::atomic<uint8_t> that is
 *     always lock-free (enforced by \c static_assert). \c set() performs a
 *     relaxed store; \c increment() uses a CAS-loop so concurrent callers
 *     never contend on a mutex.
 *
 *  2. **Rendering** — \c render_mutex_ serializes calls to \c render_impl()
 *     so that multiple threads advancing the bar simultaneously cannot
 *     interleave ANSI escape sequences on stdout.
 *
 * The only floating-point arithmetic occurs inside \c render_impl(), where
 * \c cells_per_pct_ — precomputed once at construction — converts the integer
 * percentage to a fractional fill position for the sub-cell eighth-block glyph.
 *
 * Each rendered frame overwrites the current terminal line using a carriage
 * return (\c \\r) without consuming an additional line until \c finish() is
 * called. \c finish() is idempotent and is also called by the destructor, so
 * explicit invocation is optional but recommended when the bar completes
 * before the owning scope exits.
 *
 * \par Thread safety
 * \c set() and \c increment() are safe to call from any thread. All other
 * public methods (\c finish(), \c value()) are also thread-safe.
 *
 * \par Non-copyability
 * \c Bar owns the terminal cursor state for a specific line and therefore
 * must not be copied or moved. Copy and move constructors are explicitly
 * deleted.
 */
class Bar final {
public:
    // -- Compile-time assertions ---------------------------------------------

    /**
     * \brief Guarantee lock-free atomic progress on the target platform.
     *
     * \details
     * The entire lock-free design depends on \c std::atomic<uint8_t> not
     * falling back to a mutex internally. This assertion fires at compile
     * time on any platform where that guarantee does not hold.
     */
    static_assert(std::atomic<uint8_t>::is_always_lock_free,
                  "std::atomic<uint8_t> must be always lock-free on this platform");

    // -- Type aliases --------------------------------------------------------

    using Clock = std::chrono::steady_clock;       ///< Monotonic clock used for ETA.
    using TimePoint = Clock::time_point;           ///< Absolute time point.
    using Seconds = std::chrono::duration<double>; ///< Floating-point seconds.

    // -- Construction / destruction ------------------------------------------

    /**
     * \brief Construct a \c Bar and render the initial 0 % frame.
     *
     * \details
     * Precomputes \c cells_per_pct_ from \c BarConfig::width, records the
     * construction time as the ETA origin, enables VT on Win32, and renders
     * the first frame at 0 % before returning.
     *
     * \param config  Display configuration; all fields have defaults.
     */
    explicit Bar(BarConfig config = {});

    /**
     * \brief Destructor — calls \c finish() if not already finished.
     *
     * \details
     * Ensures the bar is always closed cleanly (cursor on a new line, bar at
     * 100 %) even if an exception unwinds the owning scope.
     */
    ~Bar();

    // -- Non-copyable, non-movable -------------------------------------------

    Bar(const Bar &) = delete;            ///< Deleted — owns terminal cursor state.
    Bar &operator=(const Bar &) = delete; ///< Deleted — owns terminal cursor state.
    Bar(Bar &&) = delete;                 ///< Deleted — address-stable atomic members.
    Bar &operator=(Bar &&) = delete;      ///< Deleted — address-stable atomic members.

    // -- Public interface ----------------------------------------------------

    /**
     * \brief Set absolute progress to \p pct ∈ [0, 100].
     *
     * \details
     * Values above 100 are clamped to 100. Performs a single relaxed atomic
     * store followed by a locked render; safe to call from any thread.
     *
     * \param pct Target progress in [0, 100].
     */
    void set(uint8_t pct);

    /**
     * \brief Advance progress by \p step percent using a lock-free CAS-loop.
     *
     * \details
     * Repeatedly attempts a compare-and-swap until the updated value is
     * committed to \c progress_. Saturates at 100 rather than wrapping.
     *
     * \param step  Percentage points to add; defaults to 1.
     */
    void increment(uint8_t step = 1u);

    /**
     * \brief Seal the bar at 100 % and advance the cursor to the next line.
     *
     * \details
     * Stores 100, renders the final frame, and emits a newline. Subsequent
     * calls are no-ops thanks to the \c finished_ atomic flag. The destructor
     * calls this automatically, so explicit invocation is optional.
     */
    void finish();

    /**
     * \brief Return the current progress as an integer percentage [0, 100].
     *
     * \details
     * Uses a relaxed load; the returned value may lag slightly behind a
     * concurrent \c increment() but is always a valid percentage in [0, 100].
     *
     * \retval uint8_t Current progress in [0, 100].
     */
    [[nodiscard]] uint8_t value() const;

    /**
     * \brief Replace the label text displayed to the left of the bar.
     *
     * \details
     * The new label takes effect on the next rendered frame. Pass an empty
     * \c string_view to remove the label entirely.
     *
     * \note The string is copied into \c cfg_.label; the caller's buffer need
     *       not outlive this call.
     *
     * \param text  New label text. May be empty.
     */
    void set_label(std::string_view text);

    /**
     * \brief Return the current label text.
     *
     * \retval std::string_view  View into the label stored in \c cfg_.label.
     *                           The view is valid until the next call to \c set_label().
     */
    [[nodiscard]] std::string_view label() const;

    /**
     * \brief Return a reference to the internal render mutex.
     *
     * \details
     * Exposed so that \c guss::cli::ProgressBarSink can acquire this mutex
     * for the full duration of its 4-step write protocol, preventing any
     * concurrent bar render from interleaving with a log message write.
     *
     * \note The caller must never hold this lock while calling any public
     *       \c Bar method that itself calls \c render_locked() (i.e. \c set(),
     *       \c increment(), \c finish()) — use \c redraw_unlocked() instead.
     *
     * \retval std::mutex&  Reference to the render / console mutex.
     */
    [[nodiscard]] std::mutex& console_mutex();

    /**
     * \brief Render the current progress state without acquiring the mutex.
     *
     * \details
     * Calls \c render_impl() directly on the current \c progress_ value.
     * Must only be called while the caller already holds \c console_mutex()
     * (i.e. from inside \c guss::cli::ProgressBarSink::sink_it_()).
     */
    void redraw_unlocked();

private:
    // -- Rendering -----------------------------------------------------------

    /**
     * \brief Acquire \c render_mutex_ and delegate to \c render_impl().
     *
     * \details
     * serializes all stdout writes so concurrent threads never produce
     * torn ANSI escape sequences. Called after every successful state update.
     *
     * \param pct Current progress as integer percent [0, 100].
     */
    void render_locked(uint8_t pct) const;

    /**
     * \brief Build and write a single progress-bar line to stdout.
     *
     * \details
     * Constructs the full escape-sequence string in a local \c std::string
     * (pre-allocated to \c OUTPUT_BUFFER_SIZE), then flushes it in a single
     * \c fwrite call. The line begins with \c \\r to overwrite the previous
     * frame in place without advancing the terminal cursor.
     *
     * Rendering pipeline per frame:
     *  1. Carriage return.
     *  2. Bold label (if \c BarConfig::label is non-empty).
     *  3. Dim \c [ bracket (if \c BarConfig::show_brackets).
     *  4. Filled cells — gradient color via \c Rgb::lerp(), full-block glyph.
     *  5. Partial cell — gradient color, eighth-block glyph from \c kEighths.
     *  6. Empty cells — dim grey, \c BarConfig::empty_char glyph.
     *  7. Dim \c ] bracket (if \c BarConfig::show_brackets).
     *  8. Percentage (if \c BarConfig::show_pct).
     *  9. ETA (if \c BarConfig::show_eta and sufficient data exists).
     * 10. Trailing spaces to erase any wider previous frame.
     *
     * \param pct Current progress as integer percent [0, 100].
     */
    void render_impl(uint8_t pct) const;

    // -- Static helpers ------------------------------------------------------

    /**
     * \brief Format an \c SGR 38;2 foreground color escape sequence.
     *
     * \param color  24-bit RGB color to encode.
     * \retval std::string  ANSI escape string of the form \c \\x1b[38;2;R;G;Bm.
     */
    [[nodiscard]] static std::string fg(Rgb color);

    /**
     * \brief Format a duration in seconds as \c MM:SS or \c HhMMm.
     *
     * \details
     * Durations under one hour are formatted as \c MM:SS (e.g. \c 01:42).
     * Durations of one hour or more are formatted as \c HhMMm (e.g. \c 1h03m).
     *
     * \param seconds  Non-negative duration in seconds.
     * \retval std::string  Human-readable duration string.
     */
    [[nodiscard]] static std::string fmt_duration(double seconds);

    /**
     * \brief Write \p text to stdout in a single \c fwrite call and flush.
     *
     * \details
     * A single \c fwrite is effectively atomic with respect to the kernel's
     * write buffer, which avoids partial line corruption when multiple
     * threads write to the same terminal. Callers are responsible for
     * acquiring \c render_mutex_ before calling this function.
     *
     * \param text  Data to write; may contain arbitrary binary / escape bytes.
     */
    static void write_raw(std::string_view text);

    /**
     * \brief Enable VT processing on the Windows console handle.
     *
     * \details
     * On Win32, retrieves the \c HANDLE for \c STD_OUTPUT_HANDLE and
     * ORs in \c ENABLE_VIRTUAL_TERMINAL_PROCESSING via \c SetConsoleMode
     * so that ANSI escape sequences are interpreted rather than printed
     * literally. On POSIX systems this function is a no-op.
     */
    static void enable_vt_on_windows();

    // -- Data ----------------------------------------------------------------

    /**
     * \brief Unicode LEFT n/8 BLOCK glyphs, indexed by eighths filled.
     *
     * \details
     * \c kEighths[0] is a plain space (unused in practice; the empty-cell
     * path renders \c BarConfig::empty_char instead). \c kEighths[1] through
     * \c kEighths[8] are U+258F … U+2588, allowing the partial cell at the
     * leading edge of the filled region to be drawn with sub-character
     * precision.
     */
    static constexpr std::string_view kEighths[9] = {
        " ", "▏", "▎", "▍", "▌", "▋", "▊", "▉", "█"
    };

    BarConfig cfg_;                     ///< Display configuration (immutable after construction).
    float cells_per_pct_;               ///< Precomputed cfg_.width / 100.0f; avoids repeated division in render_impl().
    TimePoint start_;                   ///< Construction time; used as the origin for ETA calculation.
    std::atomic<uint8_t> progress_{0u}; ///< Current progress as integer percent [0, 100]; always lock-free.
    std::atomic<bool> finished_{false}; ///< Set by finish(); prevents duplicate newlines.
    mutable std::mutex render_mutex_;   ///< serializes stdout writes; not held during state updates.
};

} // namespace progress
