#include "ui/DesktopShortcut.hpp"

#include <gtest/gtest.h>

namespace {

using copyclip::ui::desktop_is_xfce;

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

} // namespace
