cmake_minimum_required(VERSION 3.25)

project(MusicPlayeah VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

option(MP_ASAN "Build with MSVC AddressSanitizer (win-asan preset)" OFF)
option(MP_ASAN_NO_ANNOTATIONS
       "Turn off STL container annotations so ASan code links with non-ASan libraries such as vcpkg's Catch2 (D7)" OFF)

# One interface target carries the warning/sanitizer flags for all our targets.
add_library(mp_options INTERFACE)
if(MSVC)
  # C4324 (struct padded because of alignas) is the point of alignas in FrameRing (false-sharing guard).
  # /external: headers included with <...> (Qt, vcpkg) are not ours: no warnings from them (Qt 6.12's qjsengine.h and
  # qvariant.h raise C4702 in the generated QML cache code).
  target_compile_options(mp_options INTERFACE /W4 /wd4324 /permissive- /utf-8 /Zc:__cplusplus /external:anglebrackets /external:W0)
  if(MP_ASAN)
    target_compile_options(mp_options INTERFACE /fsanitize=address)
    # ASan is incompatible with incremental linking and edit-and-continue.
    target_link_options(mp_options INTERFACE /INCREMENTAL:NO)
    if(MP_ASAN_NO_ANNOTATIONS)
      target_compile_definitions(mp_options INTERFACE
        _DISABLE_VECTOR_ANNOTATION _DISABLE_STRING_ANNOTATION _DISABLE_OPTIONAL_ANNOTATION)
    endif()
  endif()
endif()

if(WIN32)
  target_compile_definitions(mp_options INTERFACE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# The ASan runtime DLL lives next to cl.exe. Copy it beside our executables so they
# start from Explorer, Test Explorer or a plain terminal, not only from a VS developer prompt.
function(mp_copy_asan_runtime target)
  if(MSVC AND MP_ASAN)
    get_filename_component(_mp_cl_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
    set(_mp_asan_dll "${_mp_cl_dir}/clang_rt.asan_dynamic-x86_64.dll")
    if(EXISTS "${_mp_asan_dll}")
      add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_mp_asan_dll}" "$<TARGET_FILE_DIR:${target}>"
        COMMENT "Copying ASan runtime for ${target}")
    else()
      message(WARNING "ASan runtime not found at ${_mp_asan_dll}")
    endif()
  endif()
endfunction()

enable_testing()

add_subdirectory(src/core)
add_subdirectory(src/app)
add_subdirectory(tests)
