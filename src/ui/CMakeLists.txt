# voidclip_ui: the GTK4 + libadwaita UI layer. Links only the pure engine — never
# Qt — so the GTK frontend stays toolkit-isolated.
find_package(PkgConfig REQUIRED)
find_package(spdlog CONFIG REQUIRED)
pkg_check_modules(GTKMM REQUIRED IMPORTED_TARGET gtkmm-4.0)
pkg_check_modules(ADWAITA REQUIRED IMPORTED_TARGET libadwaita-1)

# Treat the toolkit headers as SYSTEM so their own warnings never trip -Werror.
foreach(toolkit PkgConfig::GTKMM PkgConfig::ADWAITA)
  get_target_property(includes ${toolkit} INTERFACE_INCLUDE_DIRECTORIES)
  if(includes)
    set_target_properties(${toolkit} PROPERTIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${includes}")
  endif()
endforeach()

# Pure presentation logic (no GTK) — its own library so it unit-tests headless.
add_library(voidclip_ui_text STATIC ClipText.cpp ShortcutText.cpp Fuzzy.cpp)
add_library(voidclip::ui_text ALIAS voidclip_ui_text)
target_include_directories(voidclip_ui_text PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(voidclip_ui_text PUBLIC voidclip::core)
target_link_libraries(voidclip_ui_text PRIVATE voidclip::warnings voidclip::sanitizers)

add_library(
  voidclip_ui STATIC
  Theme.cpp
  MainWindow.cpp
  Application.cpp
  GdkClipboardSource.cpp
  DesktopShortcut.cpp
  GnomeShortcut.cpp
  XfceShortcut.cpp
  StatusNotifierItem.cpp
  KeystrokePaster.cpp
  CopyAction.cpp
  widgets/ClipCard.cpp
  widgets/ShortcutChooser.cpp
  dialogs/SettingsDialog.cpp
  dialogs/FirstRunDialog.cpp)
add_library(voidclip::ui ALIAS voidclip_ui)

target_include_directories(voidclip_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

target_link_libraries(voidclip_ui PUBLIC voidclip::core PkgConfig::GTKMM PkgConfig::ADWAITA)
target_link_libraries(voidclip_ui PRIVATE voidclip::ui_text spdlog::spdlog voidclip::warnings
                                          voidclip::sanitizers)

# The windowed application.
add_executable(voidclip-gtk main.cpp)
target_link_libraries(
  voidclip-gtk PRIVATE voidclip::ui voidclip::storage voidclip::runtime voidclip::config
                       voidclip::warnings voidclip::sanitizers)
