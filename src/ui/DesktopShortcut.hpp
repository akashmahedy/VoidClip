#pragma once

// Desktop-aware shortcut facade. Linux Mint XFCE is handled through Xfconf;
// GNOME keeps its settings-daemon integration. Callers use one API and can
// safely rebind without knowing which desktop owns the shortcut.

#include <string>
#include <string_view>

namespace voidclip::ui {

// Pure desktop-name classifier used by environment detection and unit tests.
[[nodiscard]] bool desktop_is_xfce(std::string_view desktop);

[[nodiscard]] bool register_desktop_shortcut(const std::string& command,
                                             const std::string& accelerator);
[[nodiscard]] bool unregister_desktop_shortcut(const std::string& command,
                                               const std::string& accelerator);
[[nodiscard]] bool rebind_desktop_shortcut(const std::string& command,
                                           const std::string& old_accelerator,
                                           const std::string& new_accelerator);
[[nodiscard]] bool is_desktop_shortcut_registered(const std::string& command,
                                                  const std::string& accelerator);
[[nodiscard]] bool migrate_legacy_desktop_shortcut(const std::string& command,
                                                   const std::string& accelerator);

} // namespace voidclip::ui
