#pragma once

namespace enim {

/// @brief Wakes the main thread from waiting for window events, so it redraws. Callable from any thread.
void wake_main_thread();

} // namespace enim
