#pragma once

class QWindow;

namespace mp::app::platform {

// Platform interface: make the native title bar match the dark theme (D8).
// Implemented once per OS: platform/win/, platform/generic/ (no-op for now), later mac/ and linux/.
// Nothing outside src/app/platform/<os>/ may call OS APIs (D22).
void applyDarkTitleBar(QWindow* window);

}  // namespace mp::app::platform
