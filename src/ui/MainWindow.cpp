#include "ui/MainWindow.hpp"

#include "config/Constants.hpp"
#include "core/Hash.hpp"
#include "ui/ClipText.hpp"
#include "ui/Constants.hpp"
#include "ui/Fuzzy.hpp"
#include "ui/Theme.hpp"
#include "ui/widgets/ClipCard.hpp"

#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/eventcontrollerkey.h>
#include <gtkmm/image.h>
#include <gtkmm/scrolledwindow.h>

#include <gdk/gdkkeysyms.h>

#include <glibmm/main.h>
#include <glibmm/ustring.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifdef __GLIBC__
#include <malloc.h> // malloc_trim — return freed heap to the OS on hide (glibc only)
#endif

namespace voidclip::ui {

namespace {

constexpr int kPlaceholderIconSize = 48;
constexpr const char* kPageList = "list";
constexpr const char* kPageEmpty = "empty";

// Return freed heap pages to the OS; glibc otherwise keeps them in its arenas, so
// RSS never falls back after a peak (e.g. an image card's decode buffers). A free
// function so the glibc guard stays out of the G_CALLBACK macro at the call site.
void trim_heap() {
#ifdef __GLIBC__
    malloc_trim(0);
#endif
}

// The window decoration layout with the maximize button removed (it appears at
// most once, in either button group).
[[nodiscard]] std::string layout_without_maximize(std::string layout) {
    for (const std::string_view token : {",maximize", "maximize,", "maximize"}) {
        if (const std::size_t pos = layout.find(token); pos != std::string::npos) {
            layout.erase(pos, token.size());
            break;
        }
    }
    return layout;
}

// Orders rows like HistoryService::sorted (pinned first, then newest first) so the
// ListBox places an incrementally-inserted card in its right spot without a full
// rebuild. Rows are always ClipCards; a defensive null check keeps it total.
[[nodiscard]] int clip_card_sort(Gtk::ListBoxRow* lhs, Gtk::ListBoxRow* rhs) {
    const auto* a = dynamic_cast<const ClipCard*>(lhs);
    const auto* b = dynamic_cast<const ClipCard*>(rhs);
    if (a == nullptr || b == nullptr) {
        return 0;
    }
    const core::ClipboardEntry& ea = a->entry();
    const core::ClipboardEntry& eb = b->entry();
    if (ea.pinned != eb.pinned) {
        return ea.pinned ? -1 : 1; // pinned rows first
    }
    if (ea.created_at != eb.created_at) {
        return ea.created_at > eb.created_at ? -1 : 1; // newer first
    }
    return 0;
}

} // namespace

MainWindow::MainWindow(GtkApplication* application, core::HistoryService& history,
                       core::SettingsService& settings, core::ClipboardSource& clipboard,
                       Paster& paster)
    : history_{history}, settings_{settings},
      copy_action_{clipboard, history, settings, paster,
                   [this](bool success) {
                       if (!success) {
                           present();
                           show_error("Paste did not run",
                                      "The clip is still on your clipboard. Paste it manually "
                                      "with Ctrl+V, or install a supported input helper.");
                       }
                   }},
      application_{application} {
    build_ui(application);
    history_subscription_ = history_.get().subscribe([this] { schedule_refresh(); });
    apply_theme(settings.settings().theme);
    rebuild_cards();
    refresh_capture_button();
    refresh_tray();
}

MainWindow::~MainWindow() {
    // Unsubscribe first so a late notification can't reach a half-torn-down window,
    // then destroy the window so its child widgets — and the signal slots bound to
    // `this` — die with it rather than later with the GtkApplication.
    copy_action_.cancel_pending();
    history_subscription_ = {};
    if (window_ != nullptr) {
        gtk_window_destroy(GTK_WINDOW(window_));
    }
}

void MainWindow::build_ui(GtkApplication* application) {
    window_ = ADW_APPLICATION_WINDOW(adw_application_window_new(application));

    // Closing hides the window so the app keeps capturing in the background.
    g_signal_connect(window_, "close-request",
                     G_CALLBACK(+[](GtkWindow* window, gpointer) -> gboolean {
                         gtk_widget_set_visible(GTK_WIDGET(window), FALSE);
                         return TRUE;
                     }),
                     nullptr);

    // Trim on every hide — close, copy, and toggle all route through this signal.
    g_signal_connect(window_, "hide", G_CALLBACK(+[](GtkWidget*, gpointer) { trim_heap(); }),
                     nullptr);

    // The window must stay resizable for libadwaita to present dialogs as bottom
    // sheets (a non-resizable window forces them to float), so instead snap back
    // from maximize/fullscreen to keep it at a sane popup size.
    g_signal_connect(window_, "notify::maximized",
                     G_CALLBACK(+[](GObject* obj, GParamSpec*, gpointer) {
                         if (gtk_window_is_maximized(GTK_WINDOW(obj)) != FALSE) {
                             gtk_window_unmaximize(GTK_WINDOW(obj));
                         }
                     }),
                     nullptr);
    g_signal_connect(window_, "notify::fullscreened",
                     G_CALLBACK(+[](GObject* obj, GParamSpec*, gpointer) {
                         if (gtk_window_is_fullscreen(GTK_WINDOW(obj)) != FALSE) {
                             gtk_window_unfullscreen(GTK_WINDOW(obj));
                         }
                     }),
                     nullptr);

    const std::string title{config::kAppName};
    gtk_window_set_title(GTK_WINDOW(window_), title.c_str());
    gtk_window_set_default_size(GTK_WINDOW(window_), kWindowDefaultWidth, kWindowDefaultHeight);
    gtk_widget_set_size_request(GTK_WIDGET(window_), kWindowMinWidth, kWindowMinHeight);

    AdwToolbarView* toolbar = ADW_TOOLBAR_VIEW(adw_toolbar_view_new());
    AdwHeaderBar* header = ADW_HEADER_BAR(adw_header_bar_new());

    // Drop the maximize button (the window stays resizable for the bottom-sheet
    // dialogs, but isn't maximizable) while keeping the system's other controls.
    GValue layout_value = G_VALUE_INIT;
    g_value_init(&layout_value, G_TYPE_STRING);
    g_object_get_property(G_OBJECT(gtk_settings_get_default()), "gtk-decoration-layout",
                          &layout_value);
    const char* system_layout = g_value_get_string(&layout_value);
    const std::string decoration_layout =
        layout_without_maximize(system_layout != nullptr ? system_layout : ":minimize,close");
    adw_header_bar_set_decoration_layout(header, decoration_layout.c_str());
    g_value_unset(&layout_value);

    // libadwaita has no gtkmm binding, so its widgets (header bar, toolbar view,
    // window) are driven through the C API; gtkmm child widgets are handed across
    // with GTK_WIDGET(...->gobj()).
    auto* clear_button = Gtk::make_managed<Gtk::Button>();
    clear_button->set_icon_name("user-trash-symbolic");
    clear_button->set_tooltip_text("Clear unpinned history");
    clear_button->signal_clicked().connect(
        sigc::mem_fun(*this, &MainWindow::confirm_clear_history));
    adw_header_bar_pack_end(header, GTK_WIDGET(clear_button->gobj()));

    ignore_button_ = Gtk::make_managed<Gtk::Button>();
    ignore_button_->set_icon_name("media-skip-forward-symbolic");
    ignore_button_->set_tooltip_text("Ignore the next thing you copy");
    ignore_button_->signal_clicked().connect(
        sigc::mem_fun(*this, &MainWindow::toggle_ignore_next_copy));
    adw_header_bar_pack_end(header, GTK_WIDGET(ignore_button_->gobj()));

    capture_button_ = Gtk::make_managed<Gtk::Button>();
    capture_button_->signal_clicked().connect(sigc::mem_fun(*this, &MainWindow::toggle_capture));
    adw_header_bar_pack_end(header, GTK_WIDGET(capture_button_->gobj()));

    auto* settings_button = Gtk::make_managed<Gtk::Button>();
    settings_button->set_icon_name("emblem-system-symbolic");
    settings_button->set_tooltip_text("Settings");
    settings_button->signal_clicked().connect(sigc::mem_fun(*this, &MainWindow::open_settings));
    adw_header_bar_pack_start(header, GTK_WIDGET(settings_button->gobj()));

    auto* quit_button = Gtk::make_managed<Gtk::Button>();
    quit_button->set_icon_name("application-exit-symbolic");
    quit_button->set_tooltip_text("Quit VoidClip");
    quit_button->signal_clicked().connect(
        [this] { g_application_quit(G_APPLICATION(application_)); });
    adw_header_bar_pack_start(header, GTK_WIDGET(quit_button->gobj()));

    adw_toolbar_view_add_top_bar(toolbar, GTK_WIDGET(header));

    auto* content = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, kContentMargin);
    content->set_margin(kContentMargin);

    status_banner_ = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, kContentMargin);
    status_banner_->add_css_class("card");
    status_banner_->set_margin_bottom(kContentMargin);
    status_label_ = Gtk::make_managed<Gtk::Label>();
    status_label_->set_hexpand(true);
    status_label_->set_halign(Gtk::Align::START);
    status_label_->set_wrap(true);
    status_banner_->append(*status_label_);
    status_button_ = Gtk::make_managed<Gtk::Button>("Resume");
    status_button_->add_css_class("flat");
    status_button_->signal_clicked().connect([this] {
        if (ignore_next_copy_armed_) {
            toggle_ignore_next_copy();
        } else if (settings_.get().settings().capture_paused) {
            toggle_capture();
        }
    });
    status_banner_->append(*status_button_);
    content->append(*status_banner_);

    search_ = Gtk::make_managed<Gtk::SearchEntry>();
    search_->set_placeholder_text("Search clipboard history…");
    search_->signal_search_changed().connect([this] {
        search_text_ = search_->get_text().raw();
        apply_filter();
    });
    const Glib::RefPtr<Gtk::EventControllerKey> key_controller = Gtk::EventControllerKey::create();
    key_controller->signal_key_pressed().connect(sigc::mem_fun(*this, &MainWindow::on_key_pressed),
                                                 false);
    search_->add_controller(key_controller);
    content->append(*search_);

    stack_ = Gtk::make_managed<Gtk::Stack>();
    stack_->set_vexpand(true);

    auto* scrolled = Gtk::make_managed<Gtk::ScrolledWindow>();
    scrolled->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    list_ = Gtk::make_managed<Gtk::ListBox>();
    list_->set_selection_mode(Gtk::SelectionMode::SINGLE);
    list_->set_activate_on_single_click(false);
    list_->add_css_class("background");
    list_->set_valign(Gtk::Align::START);
    // Keep rows ordered so incrementally-added cards land in place (see rebuild_cards).
    list_->set_sort_func(
        [](Gtk::ListBoxRow* a, Gtk::ListBoxRow* b) { return clip_card_sort(a, b); });
    scrolled->set_child(*list_);
    stack_->add(*scrolled, kPageList);

    auto* empty = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, kContentMargin);
    empty->set_valign(Gtk::Align::CENTER);
    empty->set_halign(Gtk::Align::CENTER);
    auto* icon = Gtk::make_managed<Gtk::Image>();
    icon->set_from_icon_name("edit-paste-symbolic");
    icon->set_pixel_size(kPlaceholderIconSize);
    icon->add_css_class("dim-label");
    empty->append(*icon);
    empty_title_ = Gtk::make_managed<Gtk::Label>();
    empty_title_->add_css_class("title-2");
    empty->append(*empty_title_);
    empty_description_ = Gtk::make_managed<Gtk::Label>();
    empty_description_->add_css_class("dim-label");
    empty->append(*empty_description_);
    stack_->add(*empty, kPageEmpty);

    content->append(*stack_);

    auto* help = Gtk::make_managed<Gtk::Label>(
        "↑↓ Navigate   Enter Copy   Alt+Enter Paste   Ctrl+P Pin   Alt+Delete Delete");
    help->add_css_class("caption");
    help->add_css_class("dim-label");
    help->set_wrap(true);
    content->append(*help);

    adw_toolbar_view_set_content(toolbar, GTK_WIDGET(content->gobj()));
    toast_overlay_ = ADW_TOAST_OVERLAY(adw_toast_overlay_new());
    adw_toast_overlay_set_child(toast_overlay_, GTK_WIDGET(toolbar));
    adw_application_window_set_content(window_, GTK_WIDGET(toast_overlay_));
}

void MainWindow::schedule_refresh() {
    if (refresh_pending_) {
        return;
    }
    refresh_pending_ = true;
    Glib::signal_idle().connect_once([this] { rebuild_cards(); });
}

void MainWindow::rebuild_cards() {
    refresh_pending_ = false;
    const std::vector<core::ClipboardEntry> entries = history_.get().entries();
    card_count_ = entries.size();

    // Index the entries we want shown, by their key, for O(1) lookup below.
    std::map<std::string, const core::ClipboardEntry*> wanted;
    for (const core::ClipboardEntry& entry : entries) {
        wanted.emplace(entry.content, &entry);
    }

    // Drop focus if it sits on a row we may remove, so GTK never accounts a
    // destroyed focused/active child (the "Broken accounting of active state"
    // warning). The search entry lives outside the list, so its focus is untouched.
    if (GtkWidget* focus = gtk_window_get_focus(GTK_WINDOW(window_));
        focus != nullptr && gtk_widget_is_ancestor(focus, GTK_WIDGET(list_->gobj())) != FALSE) {
        gtk_window_set_focus(GTK_WINDOW(window_), nullptr);
    }

    // Remove cards that are gone, or whose displayed data (pin or timestamp)
    // changed — those few are recreated below; every unchanged card is reused.
    for (auto it = cards_.begin(); it != cards_.end();) {
        const auto found = wanted.find(it->first);
        const core::ClipboardEntry& shown = it->second->entry();
        const bool stale = found == wanted.end() || found->second->pinned != shown.pinned ||
                           found->second->created_at != shown.created_at;
        if (stale) {
            list_->remove(*it->second);
            it = cards_.erase(it);
        } else {
            ++it;
        }
    }

    // Add a card only for entries that lack one. The sort function drops each new
    // row into its ordered position, so no existing card is rebuilt.
    for (const core::ClipboardEntry& entry : entries) {
        if (cards_.contains(entry.content)) {
            continue;
        }
        std::vector<std::byte> image; // fetched lazily, only for image cards
        if (entry.kind == core::ClipKind::Image) {
            image = history_.get().image(entry.content);
        }
        auto* card = Gtk::make_managed<ClipCard>(
            entry, std::move(image), kMaxPreviewChars,
            [this](const core::ClipboardEntry& clip, ClipAction action) {
                handle_card_action(clip, action);
            });
        list_->append(*card);
        cards_.emplace(entry.content, card);
    }

    list_->invalidate_sort();
    apply_filter();
}

void MainWindow::apply_filter() {
    // The search bar is only useful once there is something to search.
    search_->set_visible(card_count_ > 0);

    std::size_t visible = 0;
    for (Gtk::Widget* child = list_->get_first_child(); child != nullptr;
         child = child->get_next_sibling()) {
        auto* card = dynamic_cast<ClipCard*>(child);
        if (card == nullptr) {
            continue;
        }
        const bool shown = matches(card->content());
        card->set_visible(shown);
        if (shown) {
            ++visible;
        }
    }

    if (visible > 0) {
        stack_->set_visible_child(kPageList);
        ensure_selection();
        // The window can open before the first clip exists, leaving the hidden
        // search entry without focus. When that first clip arrives, restore the
        // intended keyboard-first state without stealing focus from another child.
        if (gtk_widget_get_visible(GTK_WIDGET(window_)) != FALSE &&
            gtk_window_get_focus(GTK_WINDOW(window_)) == nullptr) {
            search_->grab_focus();
        }
        return;
    }
    if (card_count_ == 0) {
        empty_title_->set_text("No clipboard history yet");
        empty_description_->set_text("Copy something to get started");
    } else {
        empty_title_->set_text("No results");
        empty_description_->set_text("Nothing matches your search");
    }
    stack_->set_visible_child(kPageEmpty);
}

std::vector<ClipCard*> MainWindow::visible_cards() const {
    std::vector<ClipCard*> visible;
    for (Gtk::Widget* child = list_->get_first_child(); child != nullptr;
         child = child->get_next_sibling()) {
        auto* card = dynamic_cast<ClipCard*>(child);
        if (card != nullptr && card->get_visible()) {
            visible.push_back(card);
        }
    }
    return visible;
}

ClipCard* MainWindow::selected_card() const {
    Gtk::ListBoxRow* selected = list_->get_selected_row();
    auto* card = dynamic_cast<ClipCard*>(selected);
    return card != nullptr && card->get_visible() ? card : nullptr;
}

void MainWindow::ensure_selection() {
    if (selected_card() != nullptr) {
        return;
    }
    const std::vector<ClipCard*> visible = visible_cards();
    if (!visible.empty()) {
        list_->select_row(*visible.front());
    }
}

void MainWindow::select_relative(int direction) {
    const std::vector<ClipCard*> visible = visible_cards();
    if (visible.empty()) {
        return;
    }
    ClipCard* current = selected_card();
    const auto found = std::find(visible.begin(), visible.end(), current);
    std::ptrdiff_t index = found == visible.end() ? 0 : std::distance(visible.begin(), found);
    index += static_cast<std::ptrdiff_t>(direction);
    index = std::clamp(index, std::ptrdiff_t{0}, static_cast<std::ptrdiff_t>(visible.size() - 1));
    list_->select_row(*visible.at(static_cast<std::size_t>(index)));
}

void MainWindow::select_index(std::size_t index) {
    const std::vector<ClipCard*> visible = visible_cards();
    if (index < visible.size()) {
        list_->select_row(*visible.at(index));
    }
}

bool MainWindow::on_key_pressed(unsigned int keyval, unsigned int /*keycode*/,
                                Gdk::ModifierType state) {
    const bool control =
        (state & Gdk::ModifierType::CONTROL_MASK) == Gdk::ModifierType::CONTROL_MASK;
    const bool alt = (state & Gdk::ModifierType::ALT_MASK) == Gdk::ModifierType::ALT_MASK;
    const bool shift = (state & Gdk::ModifierType::SHIFT_MASK) == Gdk::ModifierType::SHIFT_MASK;

    if (keyval == GDK_KEY_Escape) {
        gtk_widget_set_visible(GTK_WIDGET(window_), FALSE);
        return true;
    }
    if (keyval == GDK_KEY_Down || keyval == GDK_KEY_Up) {
        select_relative(keyval == GDK_KEY_Down ? 1 : -1);
        return true;
    }
    if (control && keyval >= GDK_KEY_1 && keyval <= GDK_KEY_9) {
        select_index(static_cast<std::size_t>(keyval - GDK_KEY_1));
        if (ClipCard* card = selected_card(); card != nullptr) {
            copy(card->entry(), CopyMode::CopyOnly);
        }
        return true;
    }
    if (control && (keyval == GDK_KEY_p || keyval == GDK_KEY_P)) {
        if (ClipCard* card = selected_card(); card != nullptr) {
            pin(card->content());
        }
        return true;
    }
    if (alt && keyval == GDK_KEY_Delete) {
        remove_selected();
        return true;
    }
    if (control && keyval == GDK_KEY_comma) {
        open_settings();
        return true;
    }
    if (keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter) {
        if (ClipCard* card = selected_card(); card != nullptr) {
            const CopyMode mode =
                alt ? (shift ? CopyMode::PastePlainText : CopyMode::Paste) : CopyMode::CopyOnly;
            copy(card->entry(), mode);
        }
        return true;
    }
    return false;
}

void MainWindow::copy(const core::ClipboardEntry& entry, CopyMode mode) {
    // Reconstruct the clipboard payload for the entry's kind. Image bytes are
    // fetched lazily by hash; rich text carries its HTML alongside the plain text.
    core::ClipContent content;
    content.kind = entry.kind;
    if (entry.kind == core::ClipKind::Image) {
        content.image = history_.get().image(entry.content);
        content.image_width = entry.image_width;
        content.image_height = entry.image_height;
    } else {
        content.text = entry.content;
        content.html = entry.html;
    }
    content.confidential = entry.confidential;
    const CopyOutcome outcome = copy_action_.run(content, mode);
    if (outcome == CopyOutcome::Failed) {
        show_error("Could not copy",
                   "VoidClip could not place this item on the clipboard. Please try again.");
    } else if (outcome == CopyOutcome::CopiedHide) {
        gtk_widget_set_visible(GTK_WIDGET(window_), FALSE);
    } else {
        show_toast("Copied to clipboard");
    }
}

void MainWindow::handle_card_action(const core::ClipboardEntry& entry, ClipAction action) {
    switch (action) {
    case ClipAction::FollowSettings:
        copy(entry);
        break;
    case ClipAction::Copy:
        copy(entry, CopyMode::CopyOnly);
        break;
    case ClipAction::Paste:
        copy(entry, CopyMode::Paste);
        break;
    case ClipAction::PastePlainText:
        copy(entry, CopyMode::PastePlainText);
        break;
    case ClipAction::TogglePin:
        pin(entry.content);
        break;
    case ClipAction::Delete:
        remove(entry);
        break;
    }
}

void MainWindow::pin(const std::string& content) {
    history_.get().toggle_pin(content);
}

void MainWindow::remove_selected() {
    if (ClipCard* card = selected_card(); card != nullptr) {
        remove(card->entry());
    }
}

void MainWindow::remove(const core::ClipboardEntry& entry) {
    deleted_content_ = {};
    deleted_content_.kind = entry.kind;
    deleted_content_.confidential = entry.confidential;
    if (entry.kind == core::ClipKind::Image) {
        deleted_content_.image = history_.get().image(entry.content);
        deleted_content_.image_width = entry.image_width;
        deleted_content_.image_height = entry.image_height;
    } else {
        deleted_content_.text = entry.content;
        deleted_content_.html = entry.html;
    }
    deleted_was_pinned_ = entry.pinned;
    history_.get().remove(entry.content);

    if (undo_toast_ != nullptr) {
        adw_toast_dismiss(undo_toast_);
    }
    undo_toast_ = adw_toast_new("Clip deleted");
    adw_toast_set_button_label(undo_toast_, "Undo");
    g_signal_connect(undo_toast_, "button-clicked", G_CALLBACK(+[](AdwToast*, gpointer self) {
                         static_cast<MainWindow*>(self)->undo_delete();
                     }),
                     this);
    g_signal_connect(undo_toast_, "dismissed", G_CALLBACK(+[](AdwToast* toast, gpointer self) {
                         auto* window = static_cast<MainWindow*>(self);
                         if (window->undo_toast_ == toast) {
                             window->undo_toast_ = nullptr;
                         }
                     }),
                     this);
    adw_toast_overlay_add_toast(toast_overlay_, undo_toast_);
}

void MainWindow::undo_delete() {
    if (undo_toast_ == nullptr || !history_.get().add(deleted_content_)) {
        return;
    }
    const std::string key = deleted_content_.kind == core::ClipKind::Image
                                ? core::content_hash(deleted_content_.image)
                                : deleted_content_.text;
    if (deleted_was_pinned_) {
        history_.get().toggle_pin(key);
    }
    undo_toast_ = nullptr;
}

void MainWindow::clear_history() {
    history_.get().clear_unpinned();
}

void MainWindow::confirm_clear_history() {
    const std::vector<core::ClipboardEntry> entries = history_.get().entries();
    const bool has_unpinned = std::ranges::any_of(
        entries, [](const core::ClipboardEntry& entry) { return !entry.pinned; });
    if (!has_unpinned) {
        return;
    }

    auto* dialog = ADW_ALERT_DIALOG(
        adw_alert_dialog_new("Clear unpinned history?",
                             "This removes every unpinned clipboard item. Pinned items stay."));
    adw_alert_dialog_add_response(dialog, "cancel", "Cancel");
    adw_alert_dialog_add_response(dialog, "clear", "Clear");
    adw_alert_dialog_set_default_response(dialog, "cancel");
    adw_alert_dialog_set_close_response(dialog, "cancel");
    adw_alert_dialog_set_response_appearance(dialog, "clear", ADW_RESPONSE_DESTRUCTIVE);
    g_signal_connect(dialog, "response",
                     G_CALLBACK(+[](AdwAlertDialog*, const char* response, gpointer self) {
                         if (std::string_view{response} == "clear") {
                             static_cast<MainWindow*>(self)->clear_history();
                         }
                     }),
                     this);
    adw_dialog_present(ADW_DIALOG(dialog), GTK_WIDGET(window_));
}

void MainWindow::toggle_capture() {
    core::Settings updated = settings_.get().settings();
    updated.capture_paused = !updated.capture_paused;
    settings_.get().update(updated);
    refresh_capture_button();
}

void MainWindow::toggle_ignore_next_copy() {
    ignore_next_copy_armed_ = !ignore_next_copy_armed_;
    refresh_status_banner();
}

void MainWindow::refresh_capture_button() {
    if (capture_button_ == nullptr) {
        return;
    }
    const bool paused = settings_.get().settings().capture_paused;
    capture_button_->set_icon_name(paused ? "media-playback-start-symbolic"
                                          : "media-playback-pause-symbolic");
    capture_button_->set_tooltip_text(paused ? "Resume clipboard recording"
                                             : "Pause clipboard recording");
    if (paused) {
        capture_button_->add_css_class("warning");
    } else {
        capture_button_->remove_css_class("warning");
    }
    refresh_status_banner();
}

void MainWindow::refresh_status_banner() {
    if (status_banner_ == nullptr) {
        return;
    }
    if (ignore_next_copy_armed_) {
        status_label_->set_text("The next thing you copy will not be saved");
        status_button_->set_label("Cancel");
        status_banner_->set_visible(true);
    } else if (settings_.get().settings().capture_paused) {
        status_label_->set_text("Clipboard recording is paused");
        status_button_->set_label("Resume");
        status_banner_->set_visible(true);
    } else {
        status_banner_->set_visible(false);
    }
    if (ignore_button_ != nullptr) {
        if (ignore_next_copy_armed_) {
            ignore_button_->add_css_class("accent");
        } else {
            ignore_button_->remove_css_class("accent");
        }
    }
}

void MainWindow::show_toast(const std::string& message) {
    adw_toast_overlay_add_toast(toast_overlay_, adw_toast_new(message.c_str()));
}

void MainWindow::handle_clipboard_change(const core::ClipContent& content) {
    if (settings_.get().settings().capture_paused) {
        return;
    }
    if (ignore_next_copy_armed_) {
        ignore_next_copy_armed_ = false;
        refresh_status_banner();
        show_toast("Clipboard item ignored");
        return;
    }
    if (content.confidential && !settings_.get().settings().save_confidential_clips) {
        return;
    }
    if (core::HistoryService::is_oversized(content)) {
        show_toast("Item is too large to save in history");
        return;
    }
    history_.get().add(content);
}

void MainWindow::open_settings() {
    if (settings_dialog_) {
        return; // already open — a second dialog would leave dangling row callbacks
    }
    settings_dialog_ = std::make_unique<SettingsDialog>(
        GTK_WIDGET(window_), settings_.get(), history_.get(),
        [this] { apply_theme(settings_.get().settings().theme); }, [this] { refresh_tray(); },
        // On close, drop the wrapper (on idle, not mid-signal) so it can reopen.
        [this] {
            refresh_capture_button();
            Glib::signal_idle().connect_once([this] { settings_dialog_.reset(); });
        });
}

bool MainWindow::matches(const std::string& content) const {
    if (search_text_.empty()) {
        return true;
    }
    // Fuzzy subsequence match, case-folded through Glib for Unicode correctness.
    // Sanitize the content first: a legacy clip may hold invalid UTF-8, which
    // Glib::ustring's case-folding rejects.
    return fuzzy_matches(Glib::ustring{search_text_}.lowercase().raw(),
                         Glib::ustring{make_valid_utf8(content)}.lowercase().raw());
}

GtkWidget* MainWindow::native() const {
    return GTK_WIDGET(window_);
}

void MainWindow::show_error(const std::string& heading, const std::string& body) {
    auto* dialog = ADW_ALERT_DIALOG(adw_alert_dialog_new(heading.c_str(), body.c_str()));
    adw_alert_dialog_add_response(dialog, "ok", "OK");
    adw_alert_dialog_set_default_response(dialog, "ok");
    adw_alert_dialog_set_close_response(dialog, "ok");
    adw_dialog_present(ADW_DIALOG(dialog), GTK_WIDGET(window_));
}

void MainWindow::toggle() {
    if (gtk_widget_get_visible(GTK_WIDGET(window_)) != FALSE) {
        gtk_widget_set_visible(GTK_WIDGET(window_), FALSE);
        return;
    }
    present();
}

void MainWindow::present() {
    refresh_capture_button();
    if (!search_->get_text().empty()) {
        search_->set_text("");
    }
    gtk_window_present(GTK_WINDOW(window_));
    // Start on the search field (type to filter); never leave a header button
    // showing the focus ring.
    if (gtk_widget_get_visible(GTK_WIDGET(search_->gobj())) != FALSE) {
        search_->grab_focus();
    } else {
        gtk_window_set_focus(GTK_WINDOW(window_), nullptr);
    }
}

void MainWindow::refresh_tray() {
    const bool wanted = settings_.get().settings().show_panel_icon;
    if (wanted && !tray_) {
        tray_ = std::make_unique<StatusNotifierItem>(
            std::string{kPanelIconName}, [this] { present(); },
            [this] {
                present();
                open_settings();
            },
            [this] { g_application_quit(G_APPLICATION(application_)); });
    } else if (!wanted && tray_) {
        tray_.reset();
    }
}

} // namespace voidclip::ui
