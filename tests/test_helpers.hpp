/**
 * \file test_helpers.hpp
 * \brief Shared POSIX test utilities: stdout/stderr capture and suppression.
 *
 * \details
 * All helpers are POSIX-only (guarded by \c #ifndef _WIN32). On Windows the
 * types are simply unavailable; callers must guard their usage with the same
 * preprocessor condition.
 *
 * Three RAII types are provided:
 *
 *  - \c StdoutCapture  — redirects fd 1 to a pipe; \c read() restores stdout
 *                        and drains the pipe to a \c std::string.
 *  - \c StderrCapture  — same as above for fd 2 / stderr.
 *  - \c StdoutDevNull  — redirects fd 1 to \c /dev/null (suppresses output
 *                        without a pipe; avoids pipe-buffer overflow in tests
 *                        that do not need to inspect the output).
 *
 * \note \c StdoutCapture::read() closes the pipe write-end by restoring
 *       stdout first.  This makes the read side see EOF immediately, which
 *       prevents the blocking read loop from hanging.
 */
#pragma once

#include <cstdio>
#include <string>

#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>

namespace test_helpers {

/**
 * \brief Size in bytes of the internal pipe buffer used by \c StdoutCapture and \c StderrCapture.
 */
static constexpr size_t PIPE_BUF_SIZE = 4096;

// ---------------------------------------------------------------------------

[[nodiscard]] inline bool contains(const std::string& haystack,
                                   std::string_view needle) {
    return haystack.find(needle) != std::string::npos;
}

// ---------------------------------------------------------------------------

/**
 * \brief Redirect stdout to a pipe for the duration of its lifetime.
 *
 * \details
 * Call \c read() once to restore stdout and retrieve all captured bytes.
 * Calling \c read() closes the write-end (via \c dup2(saved_fd, 1)), which
 * signals EOF to the read-end — no blocking occurs.
 *
 * The destructor is safe to call whether or not \c read() has been called.
 */
class StdoutCapture {
public:
    StdoutCapture() {
        std::fflush(stdout);
        saved_fd_ = ::dup(1);
        int fds[2];
        ::pipe(fds);
        read_fd_ = fds[0];
        ::dup2(fds[1], 1);
        ::close(fds[1]);
    }

    ~StdoutCapture() {
        restore();
        if (read_fd_ >= 0) {
            ::close(read_fd_);
            read_fd_ = -1;
        }
    }

    /**
     * \brief Restore stdout and return all bytes written since construction.
     *
     * \details
     * Restores fd 1 to the original stdout, which closes the write-end of the
     * internal pipe.  The read loop then terminates on EOF.  Must be called at
     * most once.
     */
    [[nodiscard]] std::string read() {
        std::fflush(stdout);
        restore(); // closes the write-end → EOF on the read-end
        std::string result;
        char buf[PIPE_BUF_SIZE];
        ssize_t n;
        while ((n = ::read(read_fd_, buf, sizeof(buf))) > 0)
            result.append(buf, static_cast<size_t>(n));
        ::close(read_fd_);
        read_fd_ = -1;
        return result;
    }

private:
    void restore() {
        if (!restored_) {
            std::fflush(stdout);
            ::dup2(saved_fd_, 1);
            ::close(saved_fd_);
            saved_fd_ = -1;
            restored_ = true;
        }
    }

    int  saved_fd_{-1};
    int  read_fd_{-1};
    bool restored_{false};
};

// ---------------------------------------------------------------------------

/**
 * \brief Redirect stderr to a pipe for the duration of its lifetime.
 *
 * \details
 * Identical design to \c StdoutCapture but operates on fd 2.
 */
class StderrCapture {
public:
    StderrCapture() {
        std::fflush(stderr);
        saved_fd_ = ::dup(2);
        int fds[2];
        ::pipe(fds);
        read_fd_ = fds[0];
        ::dup2(fds[1], 2);
        ::close(fds[1]);
    }

    ~StderrCapture() {
        restore();
        if (read_fd_ >= 0) {
            ::close(read_fd_);
            read_fd_ = -1;
        }
    }

    [[nodiscard]] std::string read() {
        std::fflush(stderr);
        restore();
        std::string result;
        char buf[PIPE_BUF_SIZE];
        ssize_t n;
        while ((n = ::read(read_fd_, buf, sizeof(buf))) > 0)
            result.append(buf, static_cast<size_t>(n));
        ::close(read_fd_);
        read_fd_ = -1;
        return result;
    }

private:
    void restore() {
        if (!restored_) {
            std::fflush(stderr);
            ::dup2(saved_fd_, 2);
            ::close(saved_fd_);
            saved_fd_ = -1;
            restored_ = true;
        }
    }

    int  saved_fd_{-1};
    int  read_fd_{-1};
    bool restored_{false};
};

// ---------------------------------------------------------------------------

/**
 * \brief Redirect stdout to \c /dev/null for the duration of its lifetime.
 *
 * \details
 * Use in tests that generate large amounts of progress-bar output but do not
 * need to inspect it.  Unlike \c StdoutCapture, there is no internal pipe
 * buffer to overflow, so concurrency tests with hundreds of render calls are
 * safe.
 */
class StdoutDevNull {
public:
    StdoutDevNull() {
        std::fflush(stdout);
        saved_fd_ = ::dup(1);
        int devnull = ::open("/dev/null", O_WRONLY);
        ::dup2(devnull, 1);
        ::close(devnull);
    }

    ~StdoutDevNull() {
        std::fflush(stdout);
        ::dup2(saved_fd_, 1);
        ::close(saved_fd_);
    }

private:
    int saved_fd_{-1};
};

} // namespace test_helpers

#endif // _WIN32
