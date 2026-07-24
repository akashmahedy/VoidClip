#include "ui/Autostart.hpp"

#include "config/Constants.hpp"
#include "support/ScopedEnv.hpp"
#include "support/TempDir.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include <gtest/gtest.h>

namespace {

using voidclip::test::ScopedEnv;
using voidclip::testing::TempDir;

[[nodiscard]] std::string read_all(const std::filesystem::path& path) {
    std::ifstream stream{path};
    return {std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}

TEST(AutostartTest, EnabledEntryUsesOriginalExecutableAndBackgroundFlag) {
    TempDir temp;
    const ScopedEnv xdg{"XDG_CONFIG_HOME", temp.path().string()};

    ASSERT_TRUE(voidclip::ui::set_start_at_login(true, "/opt/Void Clip/voidclip"));
    const std::string entry = read_all(voidclip::config::autostart_file());
    EXPECT_NE(entry.find("Exec='/opt/Void Clip/voidclip' --background"), std::string::npos);
    EXPECT_NE(entry.find("X-GNOME-Autostart-enabled=true"), std::string::npos);
}

TEST(AutostartTest, DisabledEntryOverridesSystemAutostart) {
    TempDir temp;
    const ScopedEnv xdg{"XDG_CONFIG_HOME", temp.path().string()};

    ASSERT_TRUE(voidclip::ui::set_start_at_login(false, "/usr/bin/voidclip"));
    const std::string entry = read_all(voidclip::config::autostart_file());
    EXPECT_NE(entry.find("Hidden=true"), std::string::npos);
    EXPECT_NE(entry.find("X-GNOME-Autostart-enabled=false"), std::string::npos);
}

} // namespace
