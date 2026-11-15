find_package(Catch2 3 CONFIG REQUIRED)
include(Catch)

add_executable(core_tests
  test_format.cpp
  test_frame_ring.cpp
  test_renderer.cpp
  test_decoder.cpp
  test_engine.cpp
  wav_writer.hpp
  library_fixtures.hpp
  test_text.cpp
  test_bounded_queue.cpp
  test_database.cpp
  test_tag_reader.cpp
  test_scanner.cpp
)
# mp_taglib: the library tests write tags into generated WAV files with TagLib itself.
target_link_libraries(core_tests PRIVATE mp_core mp_options mp_taglib Catch2::Catch2WithMain)
catch_discover_tests(core_tests)
mp_copy_asan_runtime(core_tests)
