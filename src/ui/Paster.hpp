#pragma once

// Abstract paste capability: simulate pasting the clipboard into the focused
// window. Injected so the copy use case can be exercised without spawning input
// tools (see KeystrokePaster for the real implementation).

#include <functional>

namespace voidclip::ui {

class Paster {
public:
    using FinishedCallback = std::function<void(bool)>;

    Paster() = default;
    virtual ~Paster() = default;

    Paster(const Paster&) = delete;
    Paster& operator=(const Paster&) = delete;
    Paster(Paster&&) = delete;
    Paster& operator=(Paster&&) = delete;

    // Starts a paste attempt and reports success on the GTK main loop.
    virtual void paste(FinishedCallback on_finished) const = 0;
};

} // namespace voidclip::ui
