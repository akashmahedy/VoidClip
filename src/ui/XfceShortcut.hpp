#pragma once

// Xfce application-shortcut integration through xfconf-query. Xfce stores each
// accelerator as a property below /commands/custom and maps it to the command it
// should launch. Operations only remove or replace properties owned by CopyClip,
// so enabling the app never steals an existing user shortcut.

#include <string_view>

namespace copyclip::ui {

struct XfceShortcutBinding {
    std::string_view command;
    std::string_view accelerator;
};

[[nodiscard]] bool xfce_shortcuts_available();
[[nodiscard]] bool register_xfce_shortcut(const XfceShortcutBinding& binding);
[[nodiscard]] bool unregister_xfce_shortcut(const XfceShortcutBinding& binding);
[[nodiscard]] bool is_xfce_shortcut_registered(const XfceShortcutBinding& binding);

} // namespace copyclip::ui
