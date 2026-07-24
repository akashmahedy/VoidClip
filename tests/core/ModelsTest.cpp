// Port of the reference oracle tests/core/test_models.py.
//
// The reference uses @dataclass(frozen=True). The frozen/"raises on set" behavior
// has no compile-time C++ equivalent, so these cases assert what does translate:
// the default field values and the HotkeySpec::display_name formatting contract.

#include "core/Models.hpp"
#include "config/Constants.hpp"
#include "core/Enums.hpp"

#include <chrono>

#include <gtest/gtest.h>

namespace {

namespace core = voidclip::core;
namespace config = voidclip::config;

// A fresh entry is not pinned; Python's "assignment raises" check has no analogue.
TEST(ModelsTest, ClipboardEntryDefaultsToUnpinned) {
    const core::ClipboardEntry entry{
        .content = "hi", .created_at = std::chrono::system_clock::time_point{}, .pinned = {}};
    EXPECT_EQ(entry.content, "hi");
    EXPECT_FALSE(entry.pinned);
}

// display_name(): each modifier Title-cased, the key fully upper-cased, '+'-joined.
TEST(ModelsTest, DisplayNameTitleCasesModifiersAndUppercasesKey) {
    const core::HotkeySpec spec{.modifiers = {core::Modifier::Ctrl, core::Modifier::Alt},
                                .key = core::Key::V};
    EXPECT_EQ(spec.display_name(), "Ctrl+Alt+V");
}

// A single modifier plus a single-letter key.
TEST(ModelsTest, DisplayNameHandlesSingleModifier) {
    const core::HotkeySpec spec{.modifiers = {core::Modifier::Super}, .key = core::Key::V};
    EXPECT_EQ(spec.display_name(), "Super+V");
}

// A multi-character key value is upper-cased in full ("space" -> "SPACE").
TEST(ModelsTest, DisplayNameUppercasesMultiCharacterKey) {
    const core::HotkeySpec spec{.modifiers = {core::Modifier::Super}, .key = core::Key::Space};
    EXPECT_EQ(spec.display_name(), "Super+SPACE");
}

// A default-constructed Settings mirrors the reference defaults, including
// max_history_items bound to the shared config constant.
TEST(ModelsTest, SettingsHaveReferenceDefaults) {
    const core::Settings settings{};
    EXPECT_EQ(settings.theme, core::Theme::System);
    EXPECT_EQ(settings.hotkey, config::kDefaultHotkeyAccelerator);
    EXPECT_FALSE(settings.first_run_completed);
    EXPECT_EQ(settings.max_history_items, config::kDefaultMaxHistoryItems);
    EXPECT_EQ(settings.max_history_items, 70);
    EXPECT_TRUE(settings.auto_hide_on_copy);
    EXPECT_TRUE(settings.auto_paste);
    EXPECT_FALSE(settings.capture_paused);
    EXPECT_TRUE(settings.show_panel_icon);
    EXPECT_FALSE(settings.save_confidential_clips);
    EXPECT_TRUE(settings.start_at_login);
}

} // namespace
