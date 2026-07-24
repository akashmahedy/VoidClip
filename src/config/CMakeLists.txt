# voidclip_config: application-wide constants and environment-derived paths.
#
# Pure layer: depends on the standard library only (no Qt/Xlib/D-Bus). The path
# helpers read XDG environment variables at call time, so they are compiled
# out-of-line here rather than living header-only.
add_library(voidclip_config STATIC Constants.cpp)
add_library(voidclip::config ALIAS voidclip_config)

target_include_directories(voidclip_config PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

target_link_libraries(voidclip_config PRIVATE voidclip::warnings
                                              voidclip::sanitizers)
