#pragma once

// Xfce application-shortcut integration through xfconf-query. Xfce stores each
// accelerator as a property below /commands/custom and maps it to the command it
// should launch. Operations only remove or replace properties owned by CopyClip,
// so enabling the app never steals an existing user shortcut.

#include <string>

namespace copyclip::ui {

[[nodiscard]] bool xfce_shortcuts_available();
[[nodiscard]] bool register_xfce_shortcut(const std::string& command,
                                          const std::string& accelerator);
[[nodiscard]] bool unregister_xfce_shortcut(const std::string& command,
                                            const std::string& accelerator);
[[nodiscard]] bool is_xfce_shortcut_registered(const std::string& command,
                                               const std::string& accelerator);

} // namespace copyclip::ui
