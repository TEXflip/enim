#include "wakeup.hpp"

#include <RGFW.h>

namespace enim {

void wake_main_thread() { RGFW_stopCheckEvents(); }

} // namespace enim
