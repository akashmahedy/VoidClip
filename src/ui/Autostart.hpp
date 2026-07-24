#pragma once

// Per-user XDG autostart control. Writing an entry enables VoidClip for both
// package and AppImage installs; writing Hidden=true disables any system-wide
// package entry with the same desktop-file id.

#include <string>

namespace voidclip::ui {

[[nodiscard]] bool set_start_at_login(bool enabled, const std::string& executable);

} // namespace voidclip::ui
