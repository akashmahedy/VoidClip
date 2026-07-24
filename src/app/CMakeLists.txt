# voidclip: the headless composition root. Wires every layer together and runs
# the Qt event loop. A QGuiApplication (no window) backs QClipboard.
find_package(Qt6 CONFIG REQUIRED COMPONENTS Gui)

add_executable(voidclip main.cpp)

target_include_directories(voidclip PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)

target_link_libraries(
  voidclip
  PRIVATE voidclip::core voidclip::storage voidclip::adapters voidclip::runtime
          voidclip::config Qt6::Gui voidclip::warnings voidclip::sanitizers)
