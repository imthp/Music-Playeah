find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick)
qt_standard_project_setup(REQUIRES 6.5)

qt_add_executable(music_playeah
  main.cpp
  appinfo.cpp
  appinfo.hpp
  art_provider.cpp
  art_provider.hpp
  player.cpp
  player.hpp
  library_controller.cpp
  library_controller.hpp
)

# One implementation of platform/window_style.hpp per OS (D22). Windows API calls live only in platform/win/.
if(WIN32)
  target_sources(music_playeah PRIVATE platform/win/window_style_win.cpp)
else()
  target_sources(music_playeah PRIVATE platform/generic/window_style_generic.cpp)
endif()
target_sources(music_playeah PRIVATE platform/window_style.hpp)
target_include_directories(music_playeah PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Theme.qml is a singleton: this property must be set in the same directory
# that calls qt_add_qml_module, before that call.
set(MP_QML_DIR ${CMAKE_SOURCE_DIR}/qml)
set_source_files_properties(${MP_QML_DIR}/Theme.qml PROPERTIES
  QT_QML_SINGLETON_TYPE TRUE
  QT_RESOURCE_ALIAS Theme.qml
)
set_source_files_properties(${MP_QML_DIR}/Main.qml PROPERTIES
  QT_RESOURCE_ALIAS Main.qml
)
set_source_files_properties(${MP_QML_DIR}/LibraryTest.qml PROPERTIES
  QT_RESOURCE_ALIAS LibraryTest.qml
)
set_source_files_properties(${MP_QML_DIR}/VScrollBar.qml PROPERTIES
  QT_RESOURCE_ALIAS VScrollBar.qml
)
set_source_files_properties(${MP_QML_DIR}/BrowseTest.qml PROPERTIES
  QT_RESOURCE_ALIAS BrowseTest.qml
)

set_source_files_properties(${MP_QML_DIR}/RoundedImage.qml PROPERTIES
  QT_RESOURCE_ALIAS RoundedImage.qml
)
set_source_files_properties(${MP_QML_DIR}/Icon.qml PROPERTIES
  QT_RESOURCE_ALIAS Icon.qml
)
set_source_files_properties(${MP_QML_DIR}/LineSlider.qml PROPERTIES
  QT_RESOURCE_ALIAS LineSlider.qml
)
set_source_files_properties(${MP_QML_DIR}/Chip.qml PROPERTIES
  QT_RESOURCE_ALIAS Chip.qml
)
set_source_files_properties(${MP_QML_DIR}/SearchField.qml PROPERTIES
  QT_RESOURCE_ALIAS SearchField.qml
)
set_source_files_properties(${MP_QML_DIR}/AlbumCard.qml PROPERTIES
  QT_RESOURCE_ALIAS AlbumCard.qml
)
set_source_files_properties(${MP_QML_DIR}/AlbumsPage.qml PROPERTIES
  QT_RESOURCE_ALIAS AlbumsPage.qml
)
set_source_files_properties(${MP_QML_DIR}/Sidebar.qml PROPERTIES
  QT_RESOURCE_ALIAS Sidebar.qml
)
set_source_files_properties(${MP_QML_DIR}/TransportBar.qml PROPERTIES
  QT_RESOURCE_ALIAS TransportBar.qml
)

set_source_files_properties(${MP_QML_DIR}/RoundedImage.qml PROPERTIES
  QT_RESOURCE_ALIAS RoundedImage.qml
)

set_source_files_properties(${MP_QML_DIR}/PillButton.qml PROPERTIES
  QT_RESOURCE_ALIAS PillButton.qml
)
set_source_files_properties(${MP_QML_DIR}/TrackRow.qml PROPERTIES
  QT_RESOURCE_ALIAS TrackRow.qml
)
set_source_files_properties(${MP_QML_DIR}/WorkHeader.qml PROPERTIES
  QT_RESOURCE_ALIAS WorkHeader.qml
)
set_source_files_properties(${MP_QML_DIR}/AlbumPage.qml PROPERTIES
  QT_RESOURCE_ALIAS AlbumPage.qml
)
set_source_files_properties(${MP_QML_DIR}/LinkText.qml PROPERTIES
  QT_RESOURCE_ALIAS LinkText.qml
)

set_source_files_properties(${MP_QML_DIR}/NowPlaying.qml PROPERTIES
  QT_RESOURCE_ALIAS NowPlaying.qml
)

set_source_files_properties(${MP_QML_DIR}/WorkTitles.js PROPERTIES
  QT_RESOURCE_ALIAS WorkTitles.js
)

set_source_files_properties(${MP_QML_DIR}/PlayingBars.qml PROPERTIES
  QT_RESOURCE_ALIAS PlayingBars.qml
)

set_source_files_properties(${MP_QML_DIR}/PersonPage.qml PROPERTIES
  QT_RESOURCE_ALIAS PersonPage.qml
)

qt_add_qml_module(music_playeah
  URI MusicPlayeah
  VERSION 1.0
  QML_FILES
    ${MP_QML_DIR}/Main.qml
    ${MP_QML_DIR}/LibraryTest.qml
    ${MP_QML_DIR}/BrowseTest.qml
    ${MP_QML_DIR}/VScrollBar.qml
    ${MP_QML_DIR}/Theme.qml
    ${MP_QML_DIR}/Icon.qml
    ${MP_QML_DIR}/RoundedImage.qml
    ${MP_QML_DIR}/RoundedImage.qml
    ${MP_QML_DIR}/LineSlider.qml
    ${MP_QML_DIR}/Chip.qml
    ${MP_QML_DIR}/SearchField.qml
    ${MP_QML_DIR}/AlbumCard.qml
    ${MP_QML_DIR}/AlbumsPage.qml
    ${MP_QML_DIR}/Sidebar.qml
    ${MP_QML_DIR}/TransportBar.qml
    ${MP_QML_DIR}/PillButton.qml
    ${MP_QML_DIR}/TrackRow.qml
    ${MP_QML_DIR}/WorkHeader.qml
    ${MP_QML_DIR}/AlbumPage.qml
    ${MP_QML_DIR}/LinkText.qml
    ${MP_QML_DIR}/NowPlaying.qml
    ${MP_QML_DIR}/WorkTitles.js
    ${MP_QML_DIR}/PlayingBars.qml
    ${MP_QML_DIR}/PersonPage.qml
)

qt_add_resources(music_playeah "fonts"
  PREFIX "/"
  BASE ${CMAKE_SOURCE_DIR}/assets
  FILES
    ${CMAKE_SOURCE_DIR}/assets/fonts/Inter-Regular.ttf
    ${CMAKE_SOURCE_DIR}/assets/fonts/Inter-Medium.ttf
    ${CMAKE_SOURCE_DIR}/assets/fonts/Inter-SemiBold.ttf
    ${CMAKE_SOURCE_DIR}/assets/fonts/Inter-Bold.ttf
)

target_link_libraries(music_playeah PRIVATE
  mp_core
  mp_options
  Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick
)

if(WIN32)
  target_link_libraries(music_playeah PRIVATE dwmapi)

  # Copy Qt's DLLs and plugins next to the exe so it runs straight from the build folder.
  find_program(MP_WINDEPLOYQT windeployqt
    HINTS "${Qt6_DIR}/../../../bin" NO_DEFAULT_PATH)
  if(MP_WINDEPLOYQT)
    add_custom_command(TARGET music_playeah POST_BUILD
      COMMAND "${MP_WINDEPLOYQT}"
              --no-translations --no-system-d3d-compiler --no-opengl-sw
              --qmldir "${MP_QML_DIR}"
              "$<TARGET_FILE:music_playeah>"
      COMMENT "windeployqt")
  else()
    message(WARNING "windeployqt not found; run it by hand to run the app outside Qt's bin folder")
  endif()
endif()

mp_copy_asan_runtime(music_playeah)
