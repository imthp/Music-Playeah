// Windows implementation of platform/window_style.hpp. Only built when WIN32 (see CMakeLists.txt).
#include "platform/window_style.hpp"

#include <QWindow>

#include <windows.h>
#include <dwmapi.h>

// Newer SDK constants, defined here so older SDKs still build.
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_CAPTION_COLOR
#define DWMWA_CAPTION_COLOR 35
#endif

namespace mp::app::platform {

void applyDarkTitleBar(QWindow* window) {
    if (window == nullptr) {
        return;
    }
    const auto hwnd = reinterpret_cast<HWND>(window->winId());

    const BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));

    // Windows 11 only; older versions ignore it. COLORREF is 0x00BBGGRR. Matches Theme.bg #0E0E10.
    const COLORREF caption = RGB(0x0E, 0x0E, 0x10);
    DwmSetWindowAttribute(hwnd, DWMWA_CAPTION_COLOR, &caption, sizeof(caption));
}

}  // namespace mp::app::platform
