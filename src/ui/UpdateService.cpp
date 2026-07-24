#include "ui/UpdateService.hpp"

#include <glibmm/error.h>
#include <glibmm/miscutils.h>
#include <glibmm/spawn.h>

#include <nlohmann/json.hpp>

#include <sys/utsname.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace voidclip::ui {

namespace {

constexpr std::string_view kReleaseApi =
    "https://api.github.com/repos/akashmahedy/VoidClip/releases/latest";
constexpr std::string_view kReleaseDownloadPrefix =
    "https://github.com/akashmahedy/VoidClip/releases/download/";
constexpr std::string_view kPackageName = "voidclip";
constexpr std::string_view kApiVersion = "2022-11-28";
constexpr std::uintmax_t kMaximumPackageBytes = 100U * 1024U * 1024U;
constexpr std::uintmax_t kMaximumManifestBytes = 1024U * 1024U;

struct Architecture {
    std::string release;
    std::string deb;
};

struct ProcessResult {
    bool success = false;
    std::string output;
};

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        std::error_code error;
        const std::filesystem::path base = std::filesystem::temp_directory_path(error);
        if (error) {
            return;
        }
        std::string pattern = (base / "voidclip-update-XXXXXX").string();
        std::vector<char> writable{pattern.begin(), pattern.end()};
        writable.push_back('\0');
        if (char* created = ::mkdtemp(writable.data()); created != nullptr) {
            path_ = created;
        }
    }

    ~TemporaryDirectory() {
        if (!path_.empty()) {
            std::error_code ignored;
            std::filesystem::remove_all(path_, ignored);
        }
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;
    TemporaryDirectory(TemporaryDirectory&&) = delete;
    TemporaryDirectory& operator=(TemporaryDirectory&&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

[[nodiscard]] std::optional<std::array<unsigned int, 3>> parse_version(std::string_view version) {
    if (version.starts_with('v')) {
        version.remove_prefix(1);
    }
    std::array<unsigned int, 3> parsed{};
    for (std::size_t index = 0; index < parsed.size(); ++index) {
        const std::size_t dot = version.find('.');
        const std::string_view part = index + 1 == parsed.size() ? version : version.substr(0, dot);
        if (part.empty() || (index + 1 < parsed.size() && dot == std::string_view::npos)) {
            return std::nullopt;
        }
        unsigned int value = 0;
        const auto [end, error] = std::from_chars(part.data(), part.data() + part.size(), value);
        if (error != std::errc{} || end != part.data() + part.size()) {
            return std::nullopt;
        }
        parsed.at(index) = value;
        if (index + 1 < parsed.size()) {
            version.remove_prefix(dot + 1);
        }
    }
    return parsed;
}

[[nodiscard]] ProcessResult run_capture(const std::vector<std::string>& argv) {
    if (argv.empty() || Glib::find_program_in_path(argv.front()).empty()) {
        return {};
    }
    int wait_status = 0;
    std::string output;
    try {
        Glib::spawn_sync("", argv, Glib::SpawnFlags::SEARCH_PATH, {}, &output, nullptr,
                         &wait_status);
    } catch (const Glib::Error&) {
        return {};
    }
    return {.success = wait_status == 0, .output = std::move(output)};
}

[[nodiscard]] bool run(const std::vector<std::string>& argv) {
    if (argv.empty() || Glib::find_program_in_path(argv.front()).empty()) {
        return false;
    }
    int wait_status = 0;
    try {
        Glib::spawn_sync("", argv, Glib::SpawnFlags::SEARCH_PATH, {}, nullptr, nullptr,
                         &wait_status);
    } catch (const Glib::Error&) {
        return false;
    }
    return wait_status == 0;
}

[[nodiscard]] std::optional<Architecture> current_architecture() {
    utsname info{};
    if (::uname(&info) != 0) {
        return std::nullopt;
    }
    const std::string_view machine{info.machine};
    if (machine == "x86_64" || machine == "amd64") {
        return Architecture{.release = "x86_64", .deb = "amd64"};
    }
    if (machine == "aarch64" || machine == "arm64") {
        return Architecture{.release = "aarch64", .deb = "arm64"};
    }
    return std::nullopt;
}

[[nodiscard]] bool automatic_deb_update_supported() {
    if (Glib::find_program_in_path("dpkg-query").empty() ||
        Glib::find_program_in_path("apt-get").empty() ||
        Glib::find_program_in_path("pkexec").empty() ||
        Glib::find_program_in_path("curl").empty() ||
        Glib::find_program_in_path("sha256sum").empty()) {
        return false;
    }
    const ProcessResult installed =
        run_capture({"dpkg-query", "-W", "-f=${db:Status-Status}", std::string{kPackageName}});
    return installed.success && installed.output == "installed";
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): names define URL components clearly.
[[nodiscard]] bool trusted_release_url(std::string_view url, std::string_view tag,
                                       std::string_view asset_name) {
    const std::string expected =
        std::string{kReleaseDownloadPrefix} + std::string{tag} + "/" + std::string{asset_name};
    return url == expected;
}

[[nodiscard]] bool download(std::string_view url, const std::filesystem::path& destination,
                            std::uintmax_t maximum_bytes) {
    const std::vector<std::string> command = {
        "curl",
        "--fail",
        "--silent",
        "--show-error",
        "--location",
        "--proto",
        "=https",
        "--tlsv1.2",
        "--connect-timeout",
        "10",
        "--max-time",
        "120",
        "--max-filesize",
        std::to_string(maximum_bytes),
        "--output",
        destination.string(),
        std::string{url},
    };
    if (!run(command)) {
        return false;
    }
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(destination, error);
    return !error && size > 0 && size <= maximum_bytes;
}

[[nodiscard]] std::optional<std::string> read_small_file(const std::filesystem::path& path,
                                                         std::uintmax_t maximum_bytes) {
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    if (error || size == 0 || size > maximum_bytes) {
        return std::nullopt;
    }
    std::ifstream stream{path, std::ios::binary};
    if (!stream) {
        return std::nullopt;
    }
    std::ostringstream contents;
    contents << stream.rdbuf();
    return contents.str();
}

[[nodiscard]] std::string lowercase(std::string value) {
    std::ranges::transform(value, value.begin(), [](unsigned char glyph) {
        return static_cast<char>(std::tolower(glyph));
    });
    return value;
}

} // namespace

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): candidate/current are intentional.
bool is_newer_version(std::string_view candidate, std::string_view current) {
    const auto parsed_candidate = parse_version(candidate);
    const auto parsed_current = parse_version(current);
    return parsed_candidate.has_value() && parsed_current.has_value() &&
           *parsed_candidate > *parsed_current;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): parameter names encode the contract.
UpdateCheckResult check_release_json(std::string_view release_json,
                                     std::string_view current_version,
                                     std::string_view release_arch, std::string_view deb_arch) {
    const auto current = parse_version(current_version);
    if (!current.has_value()) {
        return {.message = "The installed VoidClip version is not valid."};
    }

    nlohmann::json release;
    try {
        release = nlohmann::json::parse(release_json);
        if (!release.is_object() || !release.value("draft", true) ||
            release.value("prerelease", true)) {
            return {.message = "GitHub did not return a stable VoidClip release."};
        }
    } catch (const nlohmann::json::exception&) {
        return {.message = "GitHub returned update information VoidClip could not read."};
    }

    std::string tag;
    nlohmann::json assets;
    try {
        tag = release.value("tag_name", std::string{});
        assets = release.value("assets", nlohmann::json::array());
    } catch (const nlohmann::json::exception&) {
        return {.message = "GitHub returned update information VoidClip could not read."};
    }

    const auto latest = parse_version(tag);
    if (!latest.has_value()) {
        return {.message = "The latest release has an invalid version number."};
    }
    const std::string version = tag.starts_with('v') ? tag.substr(1) : tag;
    if (tag != "v" + version) {
        return {.message = "The latest release does not use a trusted version tag."};
    }
    if (*latest <= *current) {
        return {.state = UpdateCheckState::UpToDate,
                .message = "You already have the latest version."};
    }

    const std::string package_name = "voidclip_" + version + "_" + std::string{deb_arch} + ".deb";
    const std::string manifest_name = "SHA256SUMS-" + std::string{release_arch};
    std::string package_url;
    std::string checksum_url;
    if (!assets.is_array()) {
        return {.message = "The latest release has no readable download list."};
    }
    try {
        for (const nlohmann::json& asset : assets) {
            if (!asset.is_object()) {
                continue;
            }
            const std::string name = asset.value("name", std::string{});
            const std::string url = asset.value("browser_download_url", std::string{});
            if (name == package_name) {
                package_url = url;
            } else if (name == manifest_name) {
                checksum_url = url;
            }
        }
    } catch (const nlohmann::json::exception&) {
        return {.message = "The latest release has an invalid download list."};
    }
    if (!trusted_release_url(package_url, tag, package_name) ||
        !trusted_release_url(checksum_url, tag, manifest_name)) {
        return {.message = "The latest release is missing a trusted package or checksum."};
    }
    return {.state = UpdateCheckState::Available,
            .release = {.version = version,
                        .tag = tag,
                        .package_name = package_name,
                        .package_url = package_url,
                        .checksum_url = checksum_url},
            .message = "A newer VoidClip release is available."};
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): manifest/name roles are unambiguous.
std::string checksum_for_asset(std::string_view manifest, std::string_view asset_name) {
    std::istringstream lines{std::string{manifest}};
    std::string line;
    while (std::getline(lines, line)) {
        std::istringstream fields{line};
        std::string hash;
        std::string name;
        if (!(fields >> hash >> name)) {
            continue;
        }
        if (name.starts_with('*')) {
            name.erase(0, 1);
        }
        const bool valid_hash =
            hash.size() == 64 && std::ranges::all_of(hash, [](unsigned char glyph) {
                return std::isxdigit(glyph) != 0;
            });
        if (valid_hash && name == asset_name) {
            return lowercase(hash);
        }
    }
    return {};
}

UpdateCheckResult check_for_update(std::string_view current_version) {
    const auto architecture = current_architecture();
    if (!architecture.has_value()) {
        return {.message = "Automatic updates are not available for this CPU architecture."};
    }
    const ProcessResult response =
        run_capture({"curl",
                     "--fail",
                     "--silent",
                     "--show-error",
                     "--location",
                     "--proto",
                     "=https",
                     "--tlsv1.2",
                     "--connect-timeout",
                     "10",
                     "--max-time",
                     "20",
                     "--max-filesize",
                     "1048576",
                     "--header",
                     "Accept: application/vnd.github+json",
                     "--header",
                     "X-GitHub-Api-Version: " + std::string{kApiVersion},
                     "--header",
                     "User-Agent: VoidClip/" + std::string{current_version},
                     std::string{kReleaseApi}});
    if (!response.success || response.output.empty() ||
        response.output.size() > kMaximumManifestBytes) {
        return {.message = "Could not reach GitHub. Check your internet connection."};
    }
    UpdateCheckResult result = check_release_json(response.output, current_version,
                                                  architecture->release, architecture->deb);
    if (result.state == UpdateCheckState::Available) {
        result.automatic_install_supported = automatic_deb_update_supported();
    }
    return result;
}

UpdateInstallResult install_deb_update(const UpdateRelease& release) {
    if (!automatic_deb_update_supported()) {
        return {.message = "Automatic installation is available only for an existing VoidClip "
                           "Debian package install."};
    }
    const auto architecture = current_architecture();
    if (!architecture.has_value()) {
        return {.message = "Automatic updates are not available for this CPU architecture."};
    }
    const auto parsed_version = parse_version(release.version);
    const std::string expected_package =
        "voidclip_" + release.version + "_" + architecture->deb + ".deb";
    const std::string expected_manifest = "SHA256SUMS-" + architecture->release;
    if (!parsed_version.has_value() || release.tag != "v" + release.version ||
        release.package_name != expected_package ||
        !trusted_release_url(release.package_url, release.tag, release.package_name) ||
        !trusted_release_url(release.checksum_url, release.tag, expected_manifest)) {
        return {.message = "The selected update did not pass VoidClip's download checks."};
    }

    TemporaryDirectory temporary;
    if (temporary.path().empty()) {
        return {.message = "VoidClip could not create a private update folder."};
    }
    const std::filesystem::path package = temporary.path() / release.package_name;
    const std::filesystem::path manifest = temporary.path() / "SHA256SUMS";
    if (!download(release.checksum_url, manifest, kMaximumManifestBytes) ||
        !download(release.package_url, package, kMaximumPackageBytes)) {
        return {.message = "The update download failed. Check your internet connection."};
    }

    const auto manifest_contents = read_small_file(manifest, kMaximumManifestBytes);
    const std::string expected = manifest_contents.has_value()
                                     ? checksum_for_asset(*manifest_contents, release.package_name)
                                     : std::string{};
    const ProcessResult hash = run_capture({"sha256sum", package.string()});
    std::istringstream hash_fields{hash.output};
    std::string actual;
    hash_fields >> actual;
    if (!hash.success || expected.empty() || lowercase(actual) != expected) {
        return {.message = "The downloaded update failed SHA-256 verification. Nothing was "
                           "installed."};
    }

    const std::string pkexec = Glib::find_program_in_path("pkexec");
    const std::string apt_get = Glib::find_program_in_path("apt-get");
    if (!run({pkexec, apt_get, "install", "--yes", "--no-remove", package.string()})) {
        return {.message = "The update was not installed. Administrator approval may have been "
                           "cancelled."};
    }
    const ProcessResult installed =
        run_capture({"dpkg-query", "-W", "-f=${Version}", std::string{kPackageName}});
    if (!installed.success || !installed.output.starts_with(release.version)) {
        return {.message = "The installer finished, but the new VoidClip version could not be "
                           "confirmed."};
    }
    return {.success = true,
            .message = "Version " + release.version +
                       " is installed. Restart VoidClip to use the update."};
}

} // namespace voidclip::ui
