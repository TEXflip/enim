#include <plog/Initializers/ConsoleInitializer.h>
#include <plog/Initializers/RollingFileInitializer.h>

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

inline void setup_logger(const Settings& settings) {
    if (settings.debug) {
        plog::init(plog::debug, "enim.log", 100000, 0); // 100MB one file
        plog::init<plog::TxtFormatter>(plog::debug, plog::streamStdErr);
    } else {
        plog::init<plog::CustomFormatter>(plog::error, plog::streamStdErr);
    }
}