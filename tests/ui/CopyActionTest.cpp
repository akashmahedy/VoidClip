#include "ui/CopyAction.hpp"

#include "core/HistoryService.hpp"
#include "core/Models.hpp"
#include "core/SettingsService.hpp"
#include "support/Fakes.hpp"
#include "ui/Paster.hpp"

#include <gtest/gtest.h>

#include <chrono>

namespace {

using voidclip::core::HistoryService;
using voidclip::core::Settings;
using voidclip::core::SettingsService;
using voidclip::testing::FakeClipboardSource;
using voidclip::testing::FakeClock;
using voidclip::testing::InMemoryHistoryRepository;
using voidclip::testing::InMemorySettingsRepository;
using voidclip::testing::text_clip;

// Records paste() calls without spawning any input tool.
struct FakePaster final : public voidclip::ui::Paster {
    void paste(FinishedCallback on_finished) const override {
        ++pastes;
        on_finished(succeeds);
    }
    mutable int pastes = 0;
    bool succeeds = true;
};

// Wires CopyAction to in-memory fakes; tweak behaviour via set().
struct CopyActionHarness {
    FakeClipboardSource clipboard;
    FakeClock clock{std::chrono::system_clock::time_point{}};
    InMemoryHistoryRepository history_repo;
    HistoryService history{history_repo, clock, 100};
    InMemorySettingsRepository settings_repo;
    SettingsService settings{settings_repo};
    FakePaster paster;
    voidclip::ui::CopyAction action{clipboard, history, settings, paster};

    void set(bool auto_hide, bool auto_paste) {
        Settings updated = settings.settings();
        updated.auto_hide_on_copy = auto_hide;
        updated.auto_paste = auto_paste;
        settings.update(updated);
    }
};

TEST(CopyActionTest, WritesClipboardAndRecordsHistory) {
    CopyActionHarness harness;
    EXPECT_TRUE(harness.action.run(text_clip("hello"))); // default auto-hide -> hide
    EXPECT_EQ(harness.clipboard.text, "hello");
    EXPECT_EQ(harness.history.entries().size(), 1U);
}

TEST(CopyActionTest, HidesWhenAutoHideOn) {
    CopyActionHarness harness;
    harness.set(true, false);
    EXPECT_TRUE(harness.action.run(text_clip("x")));
}

TEST(CopyActionTest, DoesNotHideWhenBothOff) {
    CopyActionHarness harness;
    harness.set(false, false);
    EXPECT_FALSE(harness.action.run(text_clip("x")));
}

TEST(CopyActionTest, HidesWhenAutoPasteEvenWithAutoHideOff) {
    CopyActionHarness harness;
    harness.set(false, true);
    EXPECT_TRUE(harness.action.run(text_clip("x")));
}

TEST(CopyActionTest, ExplicitCopyOnlyHidesWithoutAutoPaste) {
    CopyActionHarness harness;
    harness.set(false, false);

    EXPECT_TRUE(harness.action.run(text_clip("x"), voidclip::ui::CopyMode::CopyOnly));
    EXPECT_EQ(harness.clipboard.text, "x");
}

TEST(CopyActionTest, PlainTextModeDropsRichFormatting) {
    CopyActionHarness harness;
    const voidclip::core::ClipContent rich{
        .kind = voidclip::core::ClipKind::RichText, .text = "hello", .html = "<b>hello</b>"};

    EXPECT_TRUE(harness.action.run(rich, voidclip::ui::CopyMode::PastePlainText));
    EXPECT_EQ(harness.clipboard.written.kind, voidclip::core::ClipKind::Text);
    EXPECT_EQ(harness.clipboard.written.text, "hello");
    EXPECT_TRUE(harness.clipboard.written.html.empty());
    const auto entries = harness.history.entries();
    ASSERT_EQ(entries.size(), 1U);
    EXPECT_EQ(entries.front().kind, voidclip::core::ClipKind::RichText);
    EXPECT_EQ(entries.front().html, "<b>hello</b>");
}

} // namespace
