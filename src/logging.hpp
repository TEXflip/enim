#pragma once

#include <atomic>

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

inline void setup_logger(const Settings& settings) {
    if (settings.debug) {
        plog::init(plog::debug, "enim.log", 100000, 0); // 100MB one file
        plog::init<plog::TxtFormatter>(plog::debug, plog::streamStdErr);
    } else {
        plog::init<plog::CustomFormatter>(plog::error, plog::streamStdErr);
    }
    plog::get()->addAppender(&error_counter());
}

} // namespace enim