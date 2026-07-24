#include "ui/DesktopShortcut.hpp"
#include "ui/GnomeShortcut.hpp"

#include "support/ScopedEnv.hpp"

#include <gtest/gtest.h>

namespace {

using voidclip::test::ScopedEnv;
using voidclip::ui::desktop_is_xfce;

TEST(DesktopShortcutTest, RecognizesCommonXfceDesktopNames) {
    EXPECT_TRUE(desktop_is_xfce("XFCE"));
    EXPECT_TRUE(desktop_is_xfce("xfce"));
    EXPECT_TRUE(desktop_is_xfce("X-Cinnamon:XFCE"));
}

TEST(DesktopShortcutTest, DoesNotMisclassifyOtherDesktops) {
    EXPECT_FALSE(desktop_is_xfce(""));
    EXPECT_FALSE(desktop_is_xfce("GNOME"));
    EXPECT_FALSE(desktop_is_xfce("X-Cinnamon"));
    EXPECT_FALSE(desktop_is_xfce("KDE"));
}

TEST(DesktopShortcutTest, AppImageUsesOriginalLauncherPath) {
    const ScopedEnv appimage{"APPIMAGE", "/opt/VoidClip/VoidClip.AppImage"};
    EXPECT_EQ(voidclip::ui::executable_path(), "/opt/VoidClip/VoidClip.AppImage");
}

} // namespace
