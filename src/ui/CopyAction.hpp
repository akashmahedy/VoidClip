#pragma once

// The "copy a clip" use case: write the clipboard, record it in history, and —
// when auto-paste is enabled — paste it into the focused window after a short
// delay (so focus has returned to the target). Reports whether the window should
// hide afterwards. The pending paste is cancelled on destruction so it can't fire
// into a destroyed window. Collaborators are injected as references.

#include "core/HistoryService.hpp"
#include "core/Interfaces.hpp"
#include "core/SettingsService.hpp"
#include "ui/Paster.hpp"

#include <sigc++/connection.h>

#include <functional>
#include <string>

namespace copyclip::ui {

// Explicit keyboard actions can override the persistent auto-paste preference.
// FollowSettings preserves click behavior; the other modes always close the
// picker after a successful clipboard write.
enum class CopyMode {
    FollowSettings,
    CopyOnly,
    Paste,
    PastePlainText,
};

class CopyAction {
public:
    CopyAction(core::ClipboardSource& clipboard, core::HistoryService& history,
               core::SettingsService& settings, Paster& paster);
    ~CopyAction();

    CopyAction(const CopyAction&) = delete;
    CopyAction& operator=(const CopyAction&) = delete;
    CopyAction(CopyAction&&) = delete;
    CopyAction& operator=(CopyAction&&) = delete;

    // Copy `content`; returns whether the caller should hide the window afterwards.
    [[nodiscard]] bool run(const core::ClipContent& content,
                           CopyMode mode = CopyMode::FollowSettings);

private:
    std::reference_wrapper<core::ClipboardSource> clipboard_;
    std::reference_wrapper<core::HistoryService> history_;
    std::reference_wrapper<core::SettingsService> settings_;
    std::reference_wrapper<Paster> paster_;
    sigc::connection paste_connection_;
};

} // namespace copyclip::ui
