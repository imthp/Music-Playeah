// The one place miniaudio's implementation is compiled. Built without our /W4 flags
// (third-party code). The MA_NO_* switches that leave out the parts we don't use are set
// on the mp_miniaudio target in src/core/CMakeLists.txt, so every file sees the same header.
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>
