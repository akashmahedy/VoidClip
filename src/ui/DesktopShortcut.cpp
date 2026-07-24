#include "ui/DesktopShortcut.hpp"

#include "ui/GnomeShortcut.hpp"
#include "ui/XfceShortcut.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>
#include <string_view>

namespace copyclip::ui {

namespace {

[[nodiscard]] bool is_xfce_session() {
    for (const char* variable : {"XDG_CURRENT_DESKTOP", "XDG_SESSION_DESKTOP", "DESKTOP_SESSION"}) {
        if (const char* value = std::getenv(variable); value != nullptr && desktop_is_xfce(value)) {
            return true;
        }
    }
    return false;
}

} // namespace

bool desktop_is_xfce(std::string_view desktop) {
    std::string normalized{desktop};
    std::ranges::transform(normalized, normalized.begin(), [](unsigned char glyph) {
        return static_cast<char>(std::toupper(glyph));
    });
    return normalized.contains("XFCE");
}

bool register_desktop_shortcut(const std::string& command, const std::string& accelerator) {
    return is_xfce_session() ? register_xfce_shortcut({command, accelerator})
                             : register_gnome_shortcut(command, accelerator);
}

bool unregister_desktop_shortcut(const std::string& command, const std::string& accelerator) {
    return is_xfce_session() ? unregister_xfce_shortcut({command, accelerator})
                             : unregister_gnome_shortcut();
}

bool rebind_desktop_shortcut(const std::string& command, const std::string& old_accelerator,
                             const std::string& new_accelerator) {
    if (!is_xfce_session()) {
        return register_gnome_shortcut(command, new_accelerator);
    }
    if (!register_xfce_shortcut({command, new_accelerator})) {
        return false;
    }
    if (old_accelerator != new_accelerator &&
        !unregister_xfce_shortcut({command, old_accelerator})) {
        // Keep the stored setting and XFCE state aligned when removing the old
        // binding fails. Best-effort rollback avoids leaving two live shortcuts.
        static_cast<void>(unregister_xfce_shortcut({command, new_accelerator}));
        return false;
    }
    return true;
}

bool is_desktop_shortcut_registered(const std::string& command, const std::string& accelerator) {
    return is_xfce_session() ? is_xfce_shortcut_registered({command, accelerator})
                             : is_gnome_shortcut_registered();
}

} // namespace copyclip::ui
