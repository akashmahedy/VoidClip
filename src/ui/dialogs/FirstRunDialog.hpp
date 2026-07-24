#pragma once

// One-time welcome dialog: introduces VoidClip and lets the user choose the
// summon shortcut (free-form capture or a preset). On close it reports the chosen
// GTK accelerator; the caller completes first run and registers the shortcut.
// Built with the libadwaita C API.

#include "core/Models.hpp"
#include "ui/widgets/ShortcutChooser.hpp"

#include <adwaita.h>

#include <functional>
#include <memory>
#include <string>

namespace voidclip::ui {

class FirstRunDialog {
public:
    using FinishedCallback = std::function<void(const core::Settings&)>;

    FirstRunDialog(GtkWidget* parent, core::Settings initial, FinishedCallback on_finished);
    ~FirstRunDialog() = default;

    FirstRunDialog(const FirstRunDialog&) = delete;
    FirstRunDialog& operator=(const FirstRunDialog&) = delete;
    FirstRunDialog(FirstRunDialog&&) = delete;
    FirstRunDialog& operator=(FirstRunDialog&&) = delete;

private:
    static void on_get_started(GtkButton* button, gpointer self);
    static void on_closed(AdwDialog* dialog, gpointer self);
    static void on_auto_paste_toggled(GObject* row, GParamSpec* spec, gpointer self);
    static void on_confidential_toggled(GObject* row, GParamSpec* spec, gpointer self);
    static void on_startup_toggled(GObject* row, GParamSpec* spec, gpointer self);
    void finish();

    FinishedCallback on_finished_;
    core::Settings choices_;
    AdwDialog* dialog_;
    std::unique_ptr<ShortcutChooser> chooser_;
    bool confirmed_ = false;
};

} // namespace voidclip::ui
