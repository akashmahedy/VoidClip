#include "ui/Autostart.hpp"

#include "config/Constants.hpp"
#include "ui/Constants.hpp"

#include <glib.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace voidclip::ui {

bool set_start_at_login(bool enabled, const std::string& executable) {
    const std::filesystem::path path = config::autostart_file();
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        return false;
    }

    std::ofstream stream{path, std::ios::binary | std::ios::trunc};
    if (!stream) {
        return false;
    }
    stream << "[Desktop Entry]\n"
              "Type=Application\n"
              "Name=VoidClip\n";
    if (enabled) {
        char* quoted = g_shell_quote(executable.c_str());
        stream << "Comment=Keep and reuse your clipboard history\n"
                  "Exec="
               << (quoted != nullptr ? quoted : executable) << ' ' << kBackgroundFlag
               << "\nIcon=io.github.akashmahedy.VoidClip\n"
                  "Terminal=false\n"
                  "X-GNOME-Autostart-enabled=true\n";
        g_free(quoted);
    } else {
        stream << "Hidden=true\n"
                  "X-GNOME-Autostart-enabled=false\n";
    }
    stream.flush();
    return stream.good();
}

} // namespace voidclip::ui
