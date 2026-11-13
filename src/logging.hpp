#pragma once

#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>

#include <plog/Initializers/ConsoleInitializer.h>
#include <plog/Initializers/RollingFileInitializer.h>
#include <plog/Log.h>

#include <settings/settings.hpp>

namespace plog {
class CustomFormatter {
  public:
    static util::nstring header() { return util::nstring(); }

    static util::nstring format(const Record& record) {
        util::nostringstream ss;
        ss << record.getMessage() << PLOG_NSTR("\n");

        return ss.str();
    }
};
} // namespace plog

namespace enim {

/// @brief Counts the error (and fatal) records, from any thread: --exit-after-load turns them into the exit code.
class ErrorCounter : public plog::IAppender {
  public:
    void write(const plog::Record& record) override {
        if (record.getSeverity() <= plog::error)
            this->errors.fetch_add(1, std::memory_order_relaxed);
    }
    int count() const { return this->errors.load(std::memory_order_relaxed); }

  private:
    std::atomic<int> errors{0};
};

inline ErrorCounter& error_counter() {
    static ErrorCounter counter;
    return counter;
}

/// @brief plog instance of the profile log. It is only initialized with --profile: otherwise plog::get<kProfileLog>()
/// is null and PLOG_PROFILE statements are skipped without formatting their message.
constexpr int kProfileLog = 1;

/// @brief Reference time of the profile log, set by its first call: the first statement of main().
inline std::chrono::steady_clock::time_point profile_start() {
    static const auto start = std::chrono::steady_clock::now();
    return start;
}

/// @brief Prefix of a profile line: milliseconds since profile_start() and thread id. Evaluated where PLOG_PROFILE
/// is written, before plog takes the appender's lock, so the time is the event's.
inline std::string profile_prefix() {
    auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - profile_start());
    char buffer[48];
    std::snprintf(buffer, sizeof(buffer), "[profile %10.3f ms  tid %6u] ", elapsed.count(), plog::util::gettid());
    return buffer;
}

inline void setup_logger(const Settings& settings) {
    if (settings.debug) {
        plog::init(plog::debug, "enim.log", 100000, 0); // 100MB one file
        plog::init<plog::TxtFormatter>(plog::debug, plog::streamStdErr);
    } else {
        plog::init<plog::CustomFormatter>(plog::error, plog::streamStdErr);
    }
    plog::get()->addAppender(&error_counter());

    if (settings.profile)
        plog::init<plog::CustomFormatter, kProfileLog>(plog::info, plog::streamStdErr);
}

} // namespace enim

/// @brief Logs a profiling milestone (e.g. `PLOG_PROFILE << "first band uploaded";`) to stderr with the time since
/// startup and the thread id. Only with --profile; otherwise it costs a null check.
#define PLOG_PROFILE PLOG_INFO_(::enim::kProfileLog) << ::enim::profile_prefix()