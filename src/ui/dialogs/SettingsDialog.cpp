#include "ui/dialogs/SettingsDialog.hpp"

#include "config/Constants.hpp"
#include "core/Enums.hpp"
#include "core/Models.hpp"
#include "ui/Autostart.hpp"
#include "ui/Constants.hpp"
#include "ui/DesktopShortcut.hpp"
#include "ui/GnomeShortcut.hpp"

#include <adwaita.h>

#include <giomm/appinfo.h>
#include <glibmm/error.h>

#include <spdlog/spdlog.h>

#include <array>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace voidclip::ui {

namespace {

// Theme combo options, in display order; the index maps to a core::Theme.
constexpr std::array<core::Theme, 3> kThemeOrder{core::Theme::System, core::Theme::Light,
                                                 core::Theme::Dark};
constexpr double kHistoryMinimum = 10.0;
constexpr double kHistoryMaximum = 1000.0;
constexpr double kHistoryStep = 10.0;

[[nodiscard]] unsigned int theme_index(core::Theme theme) {
    for (unsigned int i = 0; i < kThemeOrder.size(); ++i) {
        if (kThemeOrder.at(i) == theme) {
            return i;
        }
    }
    return 0;
}

// Populate a combo row with `options` and select `selected`, before signals are
// connected. The row keeps its own reference to the model.
void fill_combo(AdwComboRow* row, const std::vector<std::string>& options, unsigned int selected) {
    GtkStringList* model = gtk_string_list_new(nullptr);
    for (const std::string& option : options) {
        gtk_string_list_append(model, option.c_str());
    }
    adw_combo_row_set_model(row, G_LIST_MODEL(model));
    g_object_unref(model);
    adw_combo_row_set_selected(row, selected);
}

[[nodiscard]] AdwComboRow* add_combo_row(AdwPreferencesGroup* group, const char* title) {
    auto* row = ADW_COMBO_ROW(adw_combo_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title);
    adw_preferences_group_add(group, GTK_WIDGET(row));
    return row;
}

[[nodiscard]] AdwPreferencesGroup* add_group(AdwPreferencesPage* page, const char* title) {
    auto* group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(group, title);
    adw_preferences_page_add(page, group);
    return group;
}

} // namespace

SettingsDialog::SettingsDialog(GtkWidget* parent, core::SettingsService& settings,
                               core::HistoryService& history, ThemeChangedCallback on_theme_changed,
                               PanelIconChangedCallback on_panel_icon_changed,
                               ClosedCallback on_closed)
    : settings_{settings}, history_{history}, on_theme_changed_{std::move(on_theme_changed)},
      on_panel_icon_changed_{std::move(on_panel_icon_changed)}, on_closed_{std::move(on_closed)},
      dialog_{adw_dialog_new()} {
    const core::Settings& current = settings.settings();

    // A plain AdwDialog (not AdwPreferencesDialog) so it presents as a bottom sheet
    // like the welcome dialog, instead of floating centered.
    adw_dialog_set_title(dialog_, "Settings");
    adw_dialog_set_content_width(dialog_, kDialogContentWidth);
    adw_dialog_set_presentation_mode(dialog_, ADW_DIALOG_BOTTOM_SHEET);
    auto* page = ADW_PREFERENCES_PAGE(adw_preferences_page_new());

    AdwComboRow* theme_row = add_combo_row(add_group(page, "Appearance"), "Theme");
    gtk_widget_set_tooltip_text(GTK_WIDGET(theme_row), "Light, dark, or match the system");
    fill_combo(theme_row, {"Follow system", "Light", "Dark"}, theme_index(current.theme));
    g_signal_connect(theme_row, "notify::selected", G_CALLBACK(&SettingsDialog::on_theme_selected),
                     this);

    AdwPreferencesGroup* shortcut_group = add_group(page, "Shortcut");

    auto* shortcut_enabled_row = ADW_SWITCH_ROW(adw_switch_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(shortcut_enabled_row), "Global shortcut");
    gtk_widget_set_tooltip_text(GTK_WIDGET(shortcut_enabled_row),
                                "Open VoidClip from anywhere with a keyboard shortcut");
    adw_switch_row_set_active(
        shortcut_enabled_row,
        static_cast<gboolean>(is_desktop_shortcut_registered(executable_path(), current.hotkey)));
    adw_preferences_group_add(shortcut_group, GTK_WIDGET(shortcut_enabled_row));
    g_signal_connect(shortcut_enabled_row, "notify::active",
                     G_CALLBACK(&SettingsDialog::on_shortcut_toggled), this);

    // Free-form shortcut capture plus preset quick-picks; applies on change.
    shortcut_chooser_ = std::make_unique<ShortcutChooser>(
        parent, shortcut_group, current.hotkey,
        [this](const std::string& accelerator) { return apply_accelerator(accelerator); });

    AdwPreferencesGroup* behaviour_group = add_group(page, "Behaviour");

    auto* auto_hide_row = ADW_SWITCH_ROW(adw_switch_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(auto_hide_row), "Hide after copying");
    gtk_widget_set_tooltip_text(GTK_WIDGET(auto_hide_row),
                                "Hide the window right after you pick a clip");
    adw_switch_row_set_active(auto_hide_row, static_cast<gboolean>(current.auto_hide_on_copy));
    adw_preferences_group_add(behaviour_group, GTK_WIDGET(auto_hide_row));
    g_signal_connect(auto_hide_row, "notify::active",
                     G_CALLBACK(&SettingsDialog::on_auto_hide_toggled), this);

    auto* auto_paste_row = ADW_SWITCH_ROW(adw_switch_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(auto_paste_row), "Auto-paste");
    gtk_widget_set_tooltip_text(GTK_WIDGET(auto_paste_row),
                                "Paste the clip into the focused window after copying");
    adw_switch_row_set_active(auto_paste_row, static_cast<gboolean>(current.auto_paste));
    adw_preferences_group_add(behaviour_group, GTK_WIDGET(auto_paste_row));
    g_signal_connect(auto_paste_row, "notify::active",
                     G_CALLBACK(&SettingsDialog::on_auto_paste_toggled), this);

    auto* capture_paused_row = ADW_SWITCH_ROW(adw_switch_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(capture_paused_row), "Pause recording");
    gtk_widget_set_tooltip_text(GTK_WIDGET(capture_paused_row),
                                "Keep VoidClip running without saving new clipboard contents");
    adw_switch_row_set_active(capture_paused_row, static_cast<gboolean>(current.capture_paused));
    adw_preferences_group_add(behaviour_group, GTK_WIDGET(capture_paused_row));
    g_signal_connect(capture_paused_row, "notify::active",
                     G_CALLBACK(&SettingsDialog::on_capture_paused_toggled), this);

    auto* panel_icon_row = ADW_SWITCH_ROW(adw_switch_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(panel_icon_row), "Show panel icon");
    gtk_widget_set_tooltip_text(
        GTK_WIDGET(panel_icon_row),
        "Show a panel icon to open VoidClip (needs a tray/AppIndicator host)");
    adw_switch_row_set_active(panel_icon_row, static_cast<gboolean>(current.show_panel_icon));
    adw_preferences_group_add(behaviour_group, GTK_WIDGET(panel_icon_row));
    g_signal_connect(panel_icon_row, "notify::active",
                     G_CALLBACK(&SettingsDialog::on_panel_icon_toggled), this);

    AdwPreferencesGroup* history_group = add_group(page, "History");

    auto* confidential_row = ADW_SWITCH_ROW(adw_switch_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(confidential_row),
                                  "Save password-manager clips");
    adw_action_row_set_subtitle(
        ADW_ACTION_ROW(confidential_row),
        "Stores confidential clipboard items locally until you delete them");
    adw_switch_row_set_active(confidential_row,
                              static_cast<gboolean>(current.save_confidential_clips));
    adw_preferences_group_add(history_group, GTK_WIDGET(confidential_row));
    g_signal_connect(confidential_row, "notify::active",
                     G_CALLBACK(&SettingsDialog::on_confidential_toggled), this);

    auto* history_limit_row =
        ADW_SPIN_ROW(adw_spin_row_new_with_range(kHistoryMinimum, kHistoryMaximum, kHistoryStep));
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(history_limit_row), "Maximum items");
    gtk_widget_set_tooltip_text(GTK_WIDGET(history_limit_row),
                                "Older unpinned clips are removed first");
    adw_spin_row_set_value(history_limit_row, static_cast<double>(current.max_history_items));
    adw_preferences_group_add(history_group, GTK_WIDGET(history_limit_row));
    g_signal_connect(history_limit_row, "notify::value",
                     G_CALLBACK(&SettingsDialog::on_history_limit_changed), this);

    AdwPreferencesGroup* system_group = add_group(page, "System");
    auto* startup_row = ADW_SWITCH_ROW(adw_switch_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(startup_row), "Start when I sign in");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(startup_row), "Keep VoidClip ready after a restart");
    adw_switch_row_set_active(startup_row, static_cast<gboolean>(current.start_at_login));
    adw_preferences_group_add(system_group, GTK_WIDGET(startup_row));
    g_signal_connect(startup_row, "notify::active", G_CALLBACK(&SettingsDialog::on_startup_toggled),
                     this);

    auto* releases_row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(releases_row), "Check for updates");
    const std::string version = "Installed version " + std::string{config::kAppVersion};
    adw_action_row_set_subtitle(releases_row, version.c_str());
    GtkWidget* releases_button = gtk_button_new_with_label("Open Releases");
    gtk_widget_set_valign(releases_button, GTK_ALIGN_CENTER);
    gtk_widget_add_css_class(releases_button, "flat");
    g_signal_connect(releases_button, "clicked", G_CALLBACK(+[](GtkButton*, gpointer self) {
                         try {
                             Gio::AppInfo::launch_default_for_uri(
                                 "https://github.com/akashmahedy/VoidClip/releases");
                         } catch (const Glib::Error& error) {
                             spdlog::warn("could not open the releases page: {}", error.what());
                             static_cast<SettingsDialog*>(self)->show_error(
                                 "Could not open the releases page",
                                 "Open github.com/akashmahedy/VoidClip/releases in your browser.");
                         }
                     }),
                     this);
    adw_action_row_add_suffix(releases_row, releases_button);
    adw_preferences_group_add(system_group, GTK_WIDGET(releases_row));

    GtkWidget* toolbar = adw_toolbar_view_new();
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbar), adw_header_bar_new());
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(toolbar), GTK_WIDGET(page));
    adw_dialog_set_child(dialog_, toolbar);
    g_signal_connect(dialog_, "closed", G_CALLBACK(&SettingsDialog::on_dialog_closed), this);
    adw_dialog_present(dialog_, parent);
}

void SettingsDialog::on_dialog_closed(AdwDialog* /*dialog*/, gpointer self) {
    static_cast<SettingsDialog*>(self)->on_closed_();
}

void SettingsDialog::on_theme_selected(GObject* row, GParamSpec* /*spec*/, gpointer self) {
    static_cast<SettingsDialog*>(self)->apply_theme(adw_combo_row_get_selected(ADW_COMBO_ROW(row)));
}

void SettingsDialog::on_shortcut_toggled(GObject* row, GParamSpec* /*spec*/, gpointer self) {
    auto* dialog = static_cast<SettingsDialog*>(self);
    if (dialog->suppress_shortcut_) {
        return;
    }
    static_cast<void>(
        dialog->apply_shortcut_enabled(adw_switch_row_get_active(ADW_SWITCH_ROW(row)) != FALSE));
    // Registration can fail (e.g. off GNOME); make the switch reflect what actually
    // happened rather than the user's intent, so it can't show "on" while unbound.
    const core::Settings& current = dialog->settings_.get().settings();
    dialog->suppress_shortcut_ = true;
    adw_switch_row_set_active(
        ADW_SWITCH_ROW(row),
        is_desktop_shortcut_registered(executable_path(), current.hotkey) ? TRUE : FALSE);
    dialog->suppress_shortcut_ = false;
}

void SettingsDialog::on_auto_hide_toggled(GObject* row, GParamSpec* /*spec*/, gpointer self) {
    static_cast<SettingsDialog*>(self)->apply_auto_hide(
        adw_switch_row_get_active(ADW_SWITCH_ROW(row)) != FALSE);
}

void SettingsDialog::apply_theme(unsigned int index) {
    core::Settings updated = settings_.get().settings();
    updated.theme = kThemeOrder.at(index < kThemeOrder.size() ? index : 0);
    settings_.get().update(updated);
    on_theme_changed_();
}

bool SettingsDialog::apply_accelerator(const std::string& accelerator) {
    core::Settings updated = settings_.get().settings();
    const std::string previous = updated.hotkey;
    if (is_desktop_shortcut_registered(executable_path(), previous) &&
        !rebind_desktop_shortcut(executable_path(), previous, accelerator)) {
        spdlog::warn("global shortcut could not be rebound to {}", accelerator);
        show_error(
            "Shortcut was not changed",
            "That shortcut could not be registered. Your previous shortcut is still active.");
        return false;
    }
    updated.hotkey = accelerator;
    settings_.get().update(updated);
    return true;
}

bool SettingsDialog::apply_shortcut_enabled(bool active) {
    const std::string command = executable_path();
    const std::string accelerator = settings_.get().settings().hotkey;
    const bool ok = active ? register_desktop_shortcut(command, accelerator)
                           : unregister_desktop_shortcut(command, accelerator);
    if (!ok) {
        spdlog::warn("global shortcut could not be {}", active ? "registered" : "removed");
        show_error(active ? "Shortcut was not enabled" : "Shortcut was not disabled",
                   "VoidClip could not change the desktop shortcut. Try another shortcut or "
                   "check your desktop keyboard settings.");
    }
    return ok;
}

void SettingsDialog::apply_auto_hide(bool active) {
    core::Settings updated = settings_.get().settings();
    updated.auto_hide_on_copy = active;
    settings_.get().update(updated);
}

void SettingsDialog::on_auto_paste_toggled(GObject* row, GParamSpec* /*spec*/, gpointer self) {
    static_cast<SettingsDialog*>(self)->apply_auto_paste(
        adw_switch_row_get_active(ADW_SWITCH_ROW(row)) != FALSE);
}

void SettingsDialog::on_capture_paused_toggled(GObject* row, GParamSpec* /*spec*/, gpointer self) {
    static_cast<SettingsDialog*>(self)->apply_capture_paused(
        adw_switch_row_get_active(ADW_SWITCH_ROW(row)) != FALSE);
}

void SettingsDialog::on_history_limit_changed(GObject* row, GParamSpec* /*spec*/, gpointer self) {
    static_cast<SettingsDialog*>(self)->apply_history_limit(
        static_cast<int>(adw_spin_row_get_value(ADW_SPIN_ROW(row))));
}

void SettingsDialog::apply_auto_paste(bool active) {
    core::Settings updated = settings_.get().settings();
    updated.auto_paste = active;
    settings_.get().update(updated);
}

void SettingsDialog::apply_capture_paused(bool paused) {
    core::Settings updated = settings_.get().settings();
    updated.capture_paused = paused;
    settings_.get().update(updated);
}

void SettingsDialog::apply_history_limit(int max_items) {
    core::Settings updated = settings_.get().settings();
    updated.max_history_items = max_items;
    settings_.get().update(updated);
    history_.get().set_max_items(max_items);
}

void SettingsDialog::on_panel_icon_toggled(GObject* row, GParamSpec* /*spec*/, gpointer self) {
    static_cast<SettingsDialog*>(self)->apply_panel_icon(
        adw_switch_row_get_active(ADW_SWITCH_ROW(row)) != FALSE);
}

void SettingsDialog::apply_panel_icon(bool active) {
    core::Settings updated = settings_.get().settings();
    updated.show_panel_icon = active;
    settings_.get().update(updated);
    on_panel_icon_changed_(); // let the window add/remove the tray icon live
}

void SettingsDialog::on_confidential_toggled(GObject* row, GParamSpec* /*spec*/, gpointer self) {
    static_cast<SettingsDialog*>(self)->apply_confidential(
        adw_switch_row_get_active(ADW_SWITCH_ROW(row)) != FALSE);
}

void SettingsDialog::apply_confidential(bool active) {
    core::Settings updated = settings_.get().settings();
    updated.save_confidential_clips = active;
    settings_.get().update(updated);
}

void SettingsDialog::on_startup_toggled(GObject* row, GParamSpec* /*spec*/, gpointer self) {
    auto* dialog = static_cast<SettingsDialog*>(self);
    if (dialog->suppress_startup_) {
        return;
    }
    const bool requested = adw_switch_row_get_active(ADW_SWITCH_ROW(row)) != FALSE;
    if (!dialog->apply_startup(requested)) {
        dialog->suppress_startup_ = true;
        adw_switch_row_set_active(ADW_SWITCH_ROW(row), requested ? FALSE : TRUE);
        dialog->suppress_startup_ = false;
    }
}

bool SettingsDialog::apply_startup(bool active) {
    if (!set_start_at_login(active, executable_path())) {
        show_error("Startup setting was not changed",
                   "VoidClip could not update your sign-in startup setting.");
        return false;
    }
    core::Settings updated = settings_.get().settings();
    updated.start_at_login = active;
    settings_.get().update(updated);
    return true;
}

void SettingsDialog::show_error(const std::string& heading, const std::string& body) {
    auto* alert = ADW_ALERT_DIALOG(adw_alert_dialog_new(heading.c_str(), body.c_str()));
    adw_alert_dialog_add_response(alert, "ok", "OK");
    adw_alert_dialog_set_default_response(alert, "ok");
    adw_alert_dialog_set_close_response(alert, "ok");
    adw_dialog_present(ADW_DIALOG(alert), GTK_WIDGET(dialog_));
}

} // namespace voidclip::ui
