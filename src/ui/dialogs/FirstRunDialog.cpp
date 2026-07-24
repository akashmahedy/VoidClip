#include "ui/dialogs/FirstRunDialog.hpp"

#include "ui/Constants.hpp"

#include <memory>
#include <string>
#include <utility>

namespace voidclip::ui {

namespace {

constexpr int kContentSpacing = 12;

} // namespace

FirstRunDialog::FirstRunDialog(GtkWidget* parent, core::Settings initial,
                               FinishedCallback on_finished)
    : on_finished_{std::move(on_finished)}, choices_{std::move(initial)},
      dialog_{adw_dialog_new()} {
    adw_dialog_set_title(dialog_, "Welcome");
    adw_dialog_set_content_width(dialog_, kDialogContentWidth);
    adw_dialog_set_presentation_mode(dialog_, ADW_DIALOG_BOTTOM_SHEET);

    GtkWidget* toolbar = adw_toolbar_view_new();
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbar), adw_header_bar_new());

    GtkWidget* status = adw_status_page_new();
    adw_status_page_set_icon_name(ADW_STATUS_PAGE(status), "edit-paste-symbolic");
    adw_status_page_set_title(ADW_STATUS_PAGE(status), "Welcome to VoidClip");
    adw_status_page_set_description(ADW_STATUS_PAGE(status),
                                    "Your clipboard history, one shortcut away.");

    GtkWidget* content = gtk_box_new(GTK_ORIENTATION_VERTICAL, kContentSpacing);

    GtkWidget* group = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(group), "Open shortcut");
    // The chooser presents its capture sheet on the same window, over the welcome
    // dialog. Its callback tracks the chosen accelerator for finish().
    chooser_ =
        std::make_unique<ShortcutChooser>(parent, ADW_PREFERENCES_GROUP(group), choices_.hotkey,
                                          [this](const std::string& accelerator) {
                                              choices_.hotkey = accelerator;
                                              return true;
                                          });
    gtk_box_append(GTK_BOX(content), group);

    GtkWidget* behaviour = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(behaviour), "Choose how VoidClip works");

    auto* auto_paste = ADW_SWITCH_ROW(adw_switch_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(auto_paste),
                                  "Paste selected clips automatically");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(auto_paste),
                                "Recommended: returns the clip to the app you were using");
    adw_switch_row_set_active(auto_paste, static_cast<gboolean>(choices_.auto_paste));
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(behaviour), GTK_WIDGET(auto_paste));
    g_signal_connect(auto_paste, "notify::active",
                     G_CALLBACK(&FirstRunDialog::on_auto_paste_toggled), this);

    auto* confidential = ADW_SWITCH_ROW(adw_switch_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(confidential), "Save password-manager clips");
    adw_action_row_set_subtitle(
        ADW_ACTION_ROW(confidential),
        "Off by default for privacy. Turn on if you often reuse copied passwords.");
    adw_switch_row_set_active(confidential,
                              static_cast<gboolean>(choices_.save_confidential_clips));
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(behaviour), GTK_WIDGET(confidential));
    g_signal_connect(confidential, "notify::active",
                     G_CALLBACK(&FirstRunDialog::on_confidential_toggled), this);

    auto* startup = ADW_SWITCH_ROW(adw_switch_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(startup), "Start when I sign in");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(startup),
                                "Keeps clipboard history available after a restart");
    adw_switch_row_set_active(startup, static_cast<gboolean>(choices_.start_at_login));
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(behaviour), GTK_WIDGET(startup));
    g_signal_connect(startup, "notify::active", G_CALLBACK(&FirstRunDialog::on_startup_toggled),
                     this);
    gtk_box_append(GTK_BOX(content), behaviour);

    GtkWidget* button = gtk_button_new_with_label("Get Started");
    gtk_widget_add_css_class(button, "suggested-action");
    gtk_widget_add_css_class(button, "pill");
    gtk_widget_set_halign(button, GTK_ALIGN_CENTER);
    g_signal_connect(button, "clicked", G_CALLBACK(&FirstRunDialog::on_get_started), this);
    gtk_box_append(GTK_BOX(content), button);

    adw_status_page_set_child(ADW_STATUS_PAGE(status), content);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(toolbar), status);
    adw_dialog_set_child(dialog_, toolbar);
    g_signal_connect(dialog_, "closed", G_CALLBACK(&FirstRunDialog::on_closed), this);
    adw_dialog_present(dialog_, parent);
}

void FirstRunDialog::on_get_started(GtkButton* /*button*/, gpointer self) {
    auto* dialog = static_cast<FirstRunDialog*>(self);
    dialog->confirmed_ = true;
    adw_dialog_close(dialog->dialog_);
}

void FirstRunDialog::on_closed(AdwDialog* /*dialog*/, gpointer self) {
    auto* first_run = static_cast<FirstRunDialog*>(self);
    if (first_run->confirmed_) {
        first_run->finish();
    }
}

void FirstRunDialog::on_auto_paste_toggled(GObject* row, GParamSpec* /*spec*/, gpointer self) {
    auto* dialog = static_cast<FirstRunDialog*>(self);
    dialog->choices_.auto_paste = adw_switch_row_get_active(ADW_SWITCH_ROW(row)) != FALSE;
    dialog->choices_.auto_hide_on_copy = dialog->choices_.auto_paste;
}

void FirstRunDialog::on_confidential_toggled(GObject* row, GParamSpec* /*spec*/, gpointer self) {
    static_cast<FirstRunDialog*>(self)->choices_.save_confidential_clips =
        adw_switch_row_get_active(ADW_SWITCH_ROW(row)) != FALSE;
}

void FirstRunDialog::on_startup_toggled(GObject* row, GParamSpec* /*spec*/, gpointer self) {
    static_cast<FirstRunDialog*>(self)->choices_.start_at_login =
        adw_switch_row_get_active(ADW_SWITCH_ROW(row)) != FALSE;
}

void FirstRunDialog::finish() {
    on_finished_(choices_);
}

} // namespace voidclip::ui
