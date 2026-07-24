#pragma once

// Application-wide constants. Paths are computed from the environment at call
// time (not static-init) so tests and sandboxed runs can redirect them via XDG
// variables. Mirrors voidclip/config/constants.py.

#include <filesystem>
#include <string_view>

namespace voidclip::config {

inline constexpr std::string_view kAppName = "VoidClip";
inline constexpr std::string_view kAppId = "voidclip";
inline constexpr std::string_view kLegacyAppId = "copyclip";

// Version when running from a source tree without installed package metadata.
inline constexpr std::string_view kAppVersion = "0.3.3";

inline constexpr int kDefaultMaxHistoryItems = 70;

// Default open shortcut in GTK accelerator syntax (matches core::kDefaultPreset,
// Super+V). GNOME and XFCE both accept this representation.
inline constexpr std::string_view kDefaultHotkeyAccelerator = "<Super>v";
inline constexpr std::string_view kHistoryDbName = "history.db";
inline constexpr std::string_view kSettingsFileName = "settings.json";
inline constexpr std::string_view kInstanceSocketName = "voidclip.sock";

// One-byte "show the window" command sent over the single-instance socket.
inline constexpr char kInstanceShowCommand = 'S';
inline constexpr int kInstanceSocketBacklog = 1;

// Temp-file suffix for atomic settings writes (write temp, then rename).
inline constexpr std::string_view kSettingsTempSuffix = ".json.tmp";

inline constexpr std::string_view kXdgDataHomeEnv = "XDG_DATA_HOME";
inline constexpr std::string_view kXdgConfigHomeEnv = "XDG_CONFIG_HOME";
inline constexpr std::string_view kHomeEnv = "HOME";
inline constexpr std::string_view kXdgRuntimeDirEnv = "XDG_RUNTIME_DIR";

inline constexpr std::string_view kLocalShareSubdir = ".local/share";
inline constexpr std::string_view kConfigSubdir = ".config";
inline constexpr std::string_view kAutostartSubdir = "autostart";
inline constexpr std::string_view kAutostartFileName = "io.github.akashmahedy.VoidClip.desktop";
inline constexpr std::string_view kRuntimeDirFallback = "/tmp";

// $XDG_DATA_HOME/voidclip, falling back to $HOME/.local/share/voidclip when
// XDG_DATA_HOME is unset or empty. Mirrors the Python reference when HOME is set.
[[nodiscard]] std::filesystem::path data_dir();

// Import a pre-rebrand CopyClip data directory when VoidClip has no state yet.
// Returns true when no migration is needed or the directory was moved; false
// only when migration was required but could not be completed.
[[nodiscard]] bool migrate_legacy_data();

[[nodiscard]] std::filesystem::path history_db();

[[nodiscard]] std::filesystem::path settings_file();

// Per-user autostart override. A user entry with this name takes precedence over
// the system package's /etc/xdg/autostart entry.
[[nodiscard]] std::filesystem::path autostart_file();

// Falls back to /tmp when $XDG_RUNTIME_DIR is unset or empty.
[[nodiscard]] std::filesystem::path runtime_dir();

[[nodiscard]] std::filesystem::path instance_socket();

} // namespace voidclip::config
