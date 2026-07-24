#pragma once

// GitHub-release update support for the packaged GTK application. Network and
// package-manager work is synchronous here so callers can run it on a worker
// thread; the Settings dialog owns the asynchronous UI handoff.

#include <cstdint>
#include <string>
#include <string_view>

namespace voidclip::ui {

struct UpdateRelease {
    std::string version;
    std::string tag;
    std::string package_name;
    std::string package_url;
    std::string checksum_url;
};

enum class UpdateCheckState : std::uint8_t {
    UpToDate,
    Available,
    Error,
};

struct UpdateCheckResult {
    UpdateCheckState state = UpdateCheckState::Error;
    UpdateRelease release;
    bool automatic_install_supported = false;
    std::string message;
};

struct UpdateInstallResult {
    bool success = false;
    std::string message;
};

// Numeric stable-release comparison (for example 0.3.10 > 0.3.9). Malformed
// versions are never considered newer.
[[nodiscard]] bool is_newer_version(std::string_view candidate, std::string_view current);

// Parse GitHub's latest-release JSON for one exact architecture. Kept public so
// malformed/missing-asset behavior can be unit-tested without network access.
[[nodiscard]] UpdateCheckResult check_release_json(std::string_view release_json,
                                                   std::string_view current_version,
                                                   std::string_view release_arch,
                                                   std::string_view deb_arch);

// Return the exact asset's published SHA-256, or an empty string for a malformed
// manifest/missing entry. Partial filename matches are deliberately rejected.
[[nodiscard]] std::string checksum_for_asset(std::string_view manifest,
                                             std::string_view asset_name);

// Fetch the latest stable GitHub release and decide whether this machine can
// install it automatically as an upgrade of an existing Debian package.
[[nodiscard]] UpdateCheckResult check_for_update(std::string_view current_version);

// Download and verify the selected .deb, then install it through one polkit
// authentication prompt. Only existing VoidClip Debian installs are accepted.
[[nodiscard]] UpdateInstallResult install_deb_update(const UpdateRelease& release);

} // namespace voidclip::ui
