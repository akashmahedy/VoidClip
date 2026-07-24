#pragma once

// Builds and presents a plain AdwDialog (as a bottom sheet, like the welcome
// dialog — an AdwPreferencesDialog floats centered on a fixed-size window) holding
// theme, the open shortcut, and behaviour rows, persisting changes through the
// SettingsService. Applying the theme live is delegated to a callback (the window
// owns the style manager). libadwaita has no C++ binding, so the dialog and its
// rows are driven through its C API.

#include "core/HistoryService.hpp"
#include "core/SettingsService.hpp"
#include "ui/UpdateService.hpp"
#include "ui/widgets/ShortcutChooser.hpp"

#include <adwaita.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace voidclip::ui {

class SettingsDialog {
public:
    using ThemeChangedCallback = std::function<void()>;
    using PanelIconChangedCallback = std::function<void()>;
    using ClosedCallback = std::function<void()>;

    SettingsDialog(GtkWidget* parent, core::SettingsService& settings,
                   core::HistoryService& history, ThemeChangedCallback on_theme_changed,
                   PanelIconChangedCallback on_panel_icon_changed, ClosedCallback on_closed);
    ~SettingsDialog();

    SettingsDialog(const SettingsDialog&) = delete;
    SettingsDialog& operator=(const SettingsDialog&) = delete;
    SettingsDialog(SettingsDialog&&) = delete;
    SettingsDialog& operator=(SettingsDialog&&) = delete;

private:
    static void on_theme_selected(GObject* row, GParamSpec* spec, gpointer self);
    static void on_shortcut_toggled(GObject* row, GParamSpec* spec, gpointer self);
    static void on_auto_hide_toggled(GObject* row, GParamSpec* spec, gpointer self);
    static void on_auto_paste_toggled(GObject* row, GParamSpec* spec, gpointer self);
    static void on_capture_paused_toggled(GObject* row, GParamSpec* spec, gpointer self);
    static void on_history_limit_changed(GObject* row, GParamSpec* spec, gpointer self);
    static void on_panel_icon_toggled(GObject* row, GParamSpec* spec, gpointer self);
    static void on_confidential_toggled(GObject* row, GParamSpec* spec, gpointer self);
    static void on_startup_toggled(GObject* row, GParamSpec* spec, gpointer self);
    static void on_update_clicked(GtkButton* button, gpointer self);
    static void on_dialog_closed(AdwDialog* dialog, gpointer self);

    void apply_theme(unsigned int index);
    [[nodiscard]] bool apply_accelerator(const std::string& accelerator);
    [[nodiscard]] bool apply_shortcut_enabled(bool active);
    void apply_auto_hide(bool active);
    void apply_auto_paste(bool active);
    void apply_capture_paused(bool paused);
    void apply_history_limit(int max_items);
    void apply_panel_icon(bool active);
    void apply_confidential(bool active);
    [[nodiscard]] bool apply_startup(bool active);
    void check_for_updates();
    void apply_update_check(UpdateCheckResult result);
    void install_update();
    void apply_update_install(const UpdateInstallResult& result);
    void open_release_page();
    void set_update_row(const std::string& title, const std::string& subtitle,
                        const std::string& button_label, bool button_sensitive);
    void show_error(const std::string& heading, const std::string& body);

    enum class UpdateButtonAction : std::uint8_t {
        Check,
        Install,
        OpenRelease,
        None,
    };

    struct AsyncState {
        std::atomic_bool alive{true};
    };

    std::reference_wrapper<core::SettingsService> settings_;
    std::reference_wrapper<core::HistoryService> history_;
    ThemeChangedCallback on_theme_changed_;
    PanelIconChangedCallback on_panel_icon_changed_;
    ClosedCallback on_closed_;
    std::unique_ptr<ShortcutChooser> shortcut_chooser_;
    std::shared_ptr<AsyncState> async_state_ = std::make_shared<AsyncState>();
    AdwDialog* dialog_ = nullptr;
    AdwActionRow* update_row_ = nullptr;
    GtkButton* update_button_ = nullptr;
    UpdateRelease available_update_;
    UpdateButtonAction update_button_action_ = UpdateButtonAction::Check;
    bool update_busy_ = false;
    bool suppress_shortcut_ = false;
    bool suppress_startup_ = false;
};

} // namespace voidclip::ui
