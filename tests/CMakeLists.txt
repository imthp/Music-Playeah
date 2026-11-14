find_package(Catch2 3 CONFIG REQUIRED)
include(Catch)

add_executable(core_tests
  test_format.cpp
  test_frame_ring.cpp
  test_renderer.cpp
  test_decoder.cpp
  test_engine.cpp
  wav_writer.hpp
)
target_link_libraries(core_tests PRIVATE mp_core mp_options Catch2::Catch2WithMain)
catch_discover_tests(core_tests)
mp_copy_asan_runtime(core_tests)
