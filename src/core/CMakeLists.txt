# Qt-free core (D3). Nothing in here may include a Qt header.
add_library(mp_core STATIC
  format.cpp
  format.hpp
  version.hpp
)
target_include_directories(mp_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(mp_core PUBLIC cxx_std_20)
target_link_libraries(mp_core PRIVATE mp_options)
