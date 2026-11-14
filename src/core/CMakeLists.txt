# Qt-free core (D3). Nothing in here may include a Qt header.

find_package(Threads REQUIRED)

# ---- FFmpeg (decoding, resampling) ----
# vcpkg's FindFFMPEG wrapper sets FFMPEG_INCLUDE_DIRS / FFMPEG_LIBRARY_DIRS / FFMPEG_LIBRARIES.
# Without vcpkg (Linux later, D22) fall back to the system's pkg-config files.
find_package(FFMPEG QUIET)
add_library(mp_ffmpeg INTERFACE)
if(FFMPEG_FOUND)
  target_include_directories(mp_ffmpeg INTERFACE ${FFMPEG_INCLUDE_DIRS})
  target_link_directories(mp_ffmpeg INTERFACE ${FFMPEG_LIBRARY_DIRS})
  target_link_libraries(mp_ffmpeg INTERFACE ${FFMPEG_LIBRARIES})
else()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(MP_FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libswresample libavutil)
  target_link_libraries(mp_ffmpeg INTERFACE PkgConfig::MP_FFMPEG)
endif()

# ---- miniaudio (audio output) ----
# vcpkg ships only the single header; its implementation is compiled once, here,
# without our warning flags since it's third-party code.
find_path(MINIAUDIO_INCLUDE_DIR miniaudio.h REQUIRED)
add_library(mp_miniaudio STATIC audio/miniaudio_impl.cpp)
target_include_directories(mp_miniaudio SYSTEM PUBLIC ${MINIAUDIO_INCLUDE_DIR})
# Only the low-level device API is used. Same switches for every file that includes the header.
target_compile_definitions(mp_miniaudio PUBLIC
  MA_NO_DECODING MA_NO_ENCODING MA_NO_GENERATION MA_NO_RESOURCE_MANAGER MA_NO_NODE_GRAPH MA_NO_ENGINE)
if(MSVC)
  target_compile_options(mp_miniaudio PRIVATE /W0 /utf-8)
  if(MP_ASAN)
    target_compile_options(mp_miniaudio PRIVATE /fsanitize=address)
  endif()
endif()
target_link_libraries(mp_miniaudio PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
if(UNIX)
  target_link_libraries(mp_miniaudio PUBLIC m)
endif()

# ---- core library ----
add_library(mp_core STATIC
  format.cpp
  format.hpp
  version.hpp
  audio/track_info.hpp
  audio/frame_ring.cpp
  audio/frame_ring.hpp
  audio/renderer.cpp
  audio/renderer.hpp
  audio/decoder.cpp
  audio/decoder.hpp
  audio/output.cpp
  audio/output.hpp
  audio/engine.cpp
  audio/engine.hpp
)
target_include_directories(mp_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(mp_core PUBLIC cxx_std_20)
target_link_libraries(mp_core
  PUBLIC Threads::Threads
  PRIVATE mp_options mp_ffmpeg mp_miniaudio)
