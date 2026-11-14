// Fallback for platforms without an implementation yet (macOS and Linux get their own folders later).
#include "platform/window_style.hpp"

namespace mp::app::platform {

void applyDarkTitleBar(QWindow*) {}

}  // namespace mp::app::platform
