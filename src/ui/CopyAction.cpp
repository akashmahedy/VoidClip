#include "ui/CopyAction.hpp"

#include "core/Models.hpp"

#include <glibmm/main.h>

#include <spdlog/spdlog.h>

namespace copyclip::ui {

namespace {

constexpr unsigned int kPasteDelayMs = 120;

} // namespace

CopyAction::CopyAction(core::ClipboardSource& clipboard, core::HistoryService& history,
                       core::SettingsService& settings, Paster& paster)
    : clipboard_{clipboard}, history_{history}, settings_{settings}, paster_{paster} {}

CopyAction::~CopyAction() {
    paste_connection_.disconnect();
}

bool CopyAction::run(const core::ClipContent& content, CopyMode mode) {
    core::ClipContent resolved = content;
    if (mode == CopyMode::PastePlainText && resolved.kind == core::ClipKind::RichText) {
        resolved.kind = core::ClipKind::Text;
        resolved.html.clear();
    }

    if (!clipboard_.get().write(resolved)) {
        // The clipboard rejected the write; don't record it or hide the window, so
        // the user isn't misled into thinking the copy succeeded.
        spdlog::warn("clipboard write failed; clip not recorded");
        return false;
    }
    // Paste-as-plain-text changes only the outgoing clipboard representation.
    // Keep the original rich entry in history so this one action does not
    // permanently discard formatting the user may want next time.
    history_.get().add(content);
    const core::Settings& settings = settings_.get().settings();
    const bool should_paste = mode == CopyMode::Paste || mode == CopyMode::PastePlainText ||
                              (mode == CopyMode::FollowSettings && settings.auto_paste);
    if (should_paste) {
        // Cancel any still-pending paste, then schedule one for after focus returns.
        // A connection (not connect_once) so a pending paste can be disconnected on
        // destruction; the slot returns false to run exactly once.
        paste_connection_.disconnect();
        paste_connection_ = Glib::signal_timeout().connect(
            [this] {
                paster_.get().paste();
                return false;
            },
            kPasteDelayMs);
    }
    return mode != CopyMode::FollowSettings || settings.auto_hide_on_copy || should_paste;
}

} // namespace copyclip::ui
