// Port of the reference oracle tests/config/test_constants.py.
//
// Verifies that path helpers read the environment at call time (so tests and
// sandboxed runs can redirect them via XDG variables) and that the named
// defaults match the reference values exactly.

#include "config/Constants.hpp"
#include "support/ScopedEnv.hpp"
#include "support/TempDir.hpp"

#include <pwd.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <optional>

#include <gtest/gtest.h>

namespace {

namespace config = voidclip::config;
using voidclip::test::ScopedEnv;
using voidclip::testing::TempDir;

TEST(ConstantsTest, DataDirFollowsXdg) {
    const ScopedEnv xdg{"XDG_DATA_HOME", "/tmp/xdg"};
    EXPECT_EQ(config::data_dir(), std::filesystem::path{"/tmp/xdg/voidclip"});
}

TEST(ConstantsTest, HistoryAndSettingsLiveUnderDataDir) {
    const ScopedEnv xdg{"XDG_DATA_HOME", "/tmp/xdg"};
    EXPECT_EQ(config::history_db().parent_path(), config::data_dir());
    EXPECT_EQ(config::settings_file().parent_path(), config::data_dir());
}

TEST(ConstantsTest, MigratesLegacyCopyClipDataWhenVoidClipIsEmpty) {
    TempDir temp;
    const ScopedEnv xdg{"XDG_DATA_HOME", temp.path().string()};
    const std::filesystem::path legacy = temp.path() / "copyclip";
    std::filesystem::create_directory(legacy);
    const std::filesystem::path marker = legacy / "settings.json";
    std::ofstream{marker} << "{}";

    EXPECT_TRUE(config::migrate_legacy_data());
    EXPECT_FALSE(std::filesystem::exists(legacy));
    EXPECT_TRUE(std::filesystem::exists(config::data_dir() / "settings.json"));
}

TEST(ConstantsTest, NeverOverwritesExistingVoidClipDataDuringMigration) {
    TempDir temp;
    const ScopedEnv xdg{"XDG_DATA_HOME", temp.path().string()};
    const std::filesystem::path legacy = temp.path() / "copyclip";
    std::filesystem::create_directory(legacy);
    std::filesystem::create_directory(config::data_dir());
    std::ofstream{legacy / "legacy.txt"} << "legacy";
    std::ofstream{config::data_dir() / "current.txt"} << "current";

    EXPECT_TRUE(config::migrate_legacy_data());
    EXPECT_TRUE(std::filesystem::exists(legacy / "legacy.txt"));
    EXPECT_TRUE(std::filesystem::exists(config::data_dir() / "current.txt"));
}

TEST(ConstantsTest, NamedDefaultsExist) {
    EXPECT_EQ(config::kAppName, "VoidClip");
    EXPECT_EQ(config::kAppId, "voidclip");
    EXPECT_GT(config::kDefaultMaxHistoryItems, 0);
}

// With both XDG_DATA_HOME and HOME unset, data_dir() falls back to the passwd
// home (mirroring Python's Path.home()) -> an absolute path, never a relative
// ".local/share".
TEST(ConstantsTest, DataDirFallsBackToPasswdHomeWhenHomeUnset) {
    const ScopedEnv xdg{"XDG_DATA_HOME", std::nullopt};
    const ScopedEnv home{"HOME", std::nullopt};

    const passwd* entry = ::getpwuid(::getuid());
    ASSERT_NE(entry, nullptr);
    const std::filesystem::path expected =
        std::filesystem::path{entry->pw_dir} / ".local/share" / "voidclip";
    EXPECT_EQ(config::data_dir(), expected);
}

} // namespace
