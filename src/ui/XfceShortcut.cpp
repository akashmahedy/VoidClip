#include "ui/XfceShortcut.hpp"

#include <glibmm/error.h>
#include <glibmm/miscutils.h>
#include <glibmm/spawn.h>

#include <spdlog/spdlog.h>

#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace voidclip::ui {

namespace {

constexpr const char* kChannel = "xfce4-keyboard-shortcuts";
constexpr const char* kCustomPrefix = "/commands/custom/";

[[nodiscard]] std::string property_for(std::string_view accelerator) {
    if (accelerator.empty() || accelerator.contains('/')) {
        return {};
    }
    std::string property{kCustomPrefix};
    property.append(accelerator);
    return property;
}

[[nodiscard]] std::string trim(std::string value) {
    while (!value.empty() &&
           (value.back() == '\n' || value.back() == '\r' || value.back() == ' ')) {
        value.pop_back();
    }
    return value;
}

bool run_xfconf(const std::vector<std::string>& args, std::string* output = nullptr) {
    std::vector<std::string> argv;
    argv.reserve(args.size() + 1);
    argv.emplace_back("xfconf-query");
    argv.insert(argv.end(), args.begin(), args.end());

    int wait_status = 0;
    try {
        Glib::spawn_sync("", argv, Glib::SpawnFlags::SEARCH_PATH, {}, output, nullptr,
                         &wait_status);
    } catch (const Glib::Error&) {
        return false;
    }
    return wait_status == 0;
}

[[nodiscard]] bool read_binding(const std::string& property, std::string& command) {
    if (!run_xfconf({"-c", kChannel, "-p", property}, &command)) {
        return false;
    }
    command = trim(std::move(command));
    return true;
}

} // namespace

bool xfce_shortcuts_available() {
    return !Glib::find_program_in_path("xfconf-query").empty();
}

bool register_xfce_shortcut(const XfceShortcutBinding& binding) {
    const std::string property = property_for(binding.accelerator);
    if (binding.command.empty() || property.empty() || !xfce_shortcuts_available()) {
        return false;
    }

    std::string current;
    if (read_binding(property, current)) {
        if (current == binding.command) {
            return true;
        }
        spdlog::warn("XFCE shortcut {} is already assigned to '{}'; leaving it unchanged",
                     binding.accelerator, current);
        return false;
    }

    return run_xfconf(
        {"-c", kChannel, "-p", property, "-n", "-t", "string", "-s", std::string{binding.command}});
}

bool unregister_xfce_shortcut(const XfceShortcutBinding& binding) {
    const std::string property = property_for(binding.accelerator);
    if (binding.command.empty() || property.empty() || !xfce_shortcuts_available()) {
        return false;
    }

    std::string current;
    if (!read_binding(property, current)) {
        return true;
    }
    if (current != binding.command) {
        return true; // not ours: never remove another application shortcut
    }
    return run_xfconf({"-c", kChannel, "-p", property, "-r"});
}

bool is_xfce_shortcut_registered(const XfceShortcutBinding& binding) {
    const std::string property = property_for(binding.accelerator);
    if (binding.command.empty() || property.empty() || !xfce_shortcuts_available()) {
        return false;
    }
    std::string current;
    return read_binding(property, current) && current == binding.command;
}

bool migrate_legacy_xfce_shortcut(const XfceShortcutBinding& binding) {
    const std::string property = property_for(binding.accelerator);
    if (binding.command.empty() || property.empty() || !xfce_shortcuts_available()) {
        return false;
    }
    std::string current;
    if (!read_binding(property, current)) {
        return false;
    }
    const std::filesystem::path executable{current};
    if (executable.filename() != "copyclip") {
        return false;
    }
    return run_xfconf({"-c", kChannel, "-p", property, "-s", std::string{binding.command}});
}

} // namespace voidclip::ui
