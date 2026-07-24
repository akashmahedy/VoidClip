#include "ui/widgets/ClipCard.hpp"

#include "ui/ClipText.hpp"

#include <gtkmm/enums.h>
#include <gtkmm/image.h>
#include <gtkmm/picture.h>

#include <gdkmm/enums.h>
#include <gdkmm/pixbuf.h>
#include <gdkmm/texture.h>

#include <giomm/memoryinputstream.h>

#include <glibmm/bytes.h>
#include <glibmm/error.h>
#include <glibmm/main.h>

#include <spdlog/spdlog.h>

#include <glib.h>

#include <chrono>
#include <cstddef>
#include <ctime>
#include <format>
#include <string>
#include <utility>
#include <vector>

namespace voidclip::ui {

namespace {

// Vertical gap between the content and its metadata row.
constexpr int kCardSpacing = 4;
// Padding inside the card.
constexpr int kCardMargin = 8;

// Gap below each card so they read as separate elevated cards (clearer than a
// grouped boxed-list, especially in light mode).
constexpr int kCardGap = 6;

// Lines of content shown on a collapsed card before it ellipsizes.
constexpr int kCollapsedLines = 2;

// Target height for an image thumbnail; the picture scales to fit, keeping aspect.
constexpr int kImageThumbHeight = 128;

// Height we actually decode the image to (2× the display height, for HiDPI
// crispness): a full-resolution decode costs ~33 MB for a 4K screenshot, a
// thumbnail this tall only tens of KB, and the card never shows more than this.
constexpr int kImageThumbDecodeHeight = kImageThumbHeight * 2;

// Gap between the leading content-type icon and the content column.
constexpr int kRowSpacing = 10;
// Gap between items in the subordinate metadata row.
constexpr int kMetaSpacing = 8;
// Leading content-type icon (text vs image) — the card's primary type cue.
constexpr int kTypeIconSize = 16;
// Small icons (e.g. the pin marker) in the metadata row.
constexpr int kMetaIconSize = 12;

// Local-time "YYYY-MM-DD HH:MM" for the card's timestamp, via GLib's date API.
[[nodiscard]] std::string format_timestamp(std::chrono::system_clock::time_point when) {
    const auto unix_seconds = static_cast<gint64>(std::chrono::system_clock::to_time_t(when));
    GDateTime* moment = g_date_time_new_from_unix_local(unix_seconds);
    if (moment == nullptr) {
        return {};
    }
    char* formatted = g_date_time_format(moment, "%Y-%m-%d %H:%M");
    std::string result = formatted != nullptr ? formatted : std::string{};
    g_free(formatted);
    g_date_time_unref(moment);
    return result;
}

// Symbolic icon name for the clip's content type — the leading cue on each card.
[[nodiscard]] const char* type_icon_name(core::ClipKind kind) {
    return kind == core::ClipKind::Image ? "image-x-generic-symbolic" : "text-x-generic-symbolic";
}

} // namespace

ClipCard::ClipCard(const core::ClipboardEntry& entry, std::vector<std::byte> image,
                   std::size_t max_chars, ActionCallback on_action)
    : entry_{entry}, max_chars_{max_chars}, on_action_{std::move(on_action)} {
    add_css_class("card");
    set_margin_bottom(kCardGap);

    // Distribution mirrors the reference: a leading content-type icon, the content
    // itself as the focus, and a small subordinate metadata row beneath it — rather
    // than a prominent badge/timestamp header competing above the content.
    auto* row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, kRowSpacing);
    row->set_margin(kCardMargin);

    auto* type_icon = Gtk::make_managed<Gtk::Image>();
    type_icon->set_from_icon_name(type_icon_name(entry.kind));
    type_icon->add_css_class("dim-label");
    type_icon->set_valign(Gtk::Align::START);
    type_icon->set_pixel_size(kTypeIconSize);
    row->append(*type_icon);

    auto* column = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, kCardSpacing);
    column->set_hexpand(true);

    if (entry.kind == core::ClipKind::Image) {
        // Decode to a thumbnail, not the full image: scaling at decode time keeps
        // only a small texture resident (a 4K screenshot is ~33 MB at full size).
        // Fall back to a label if the bytes won't decode.
        try {
            const Glib::RefPtr<Glib::Bytes> bytes = Glib::Bytes::create(image.data(), image.size());
            const Glib::RefPtr<Gio::MemoryInputStream> stream = Gio::MemoryInputStream::create();
            stream->add_bytes(bytes);
            // Scale down large images; never up. A clip shorter than the thumbnail
            // decodes at its own height (crisp and cheaper) when we know it.
            const int decode_height =
                (entry.image_height > 0 && entry.image_height < kImageThumbDecodeHeight)
                    ? entry.image_height
                    : kImageThumbDecodeHeight;
            const Glib::RefPtr<Gdk::Pixbuf> thumbnail =
                Gdk::Pixbuf::create_from_stream_at_scale(stream, -1, decode_height, true);
            auto* picture = Gtk::make_managed<Gtk::Picture>();
            picture->set_paintable(Gdk::Texture::create_for_pixbuf(thumbnail));
            picture->set_can_shrink(true);
            picture->set_content_fit(Gtk::ContentFit::CONTAIN);
            picture->set_halign(Gtk::Align::START);
            picture->set_size_request(-1, kImageThumbHeight);
            column->append(*picture);
        } catch (const Glib::Error& error) {
            spdlog::warn("could not render clipboard image: {}", error.what());
            auto* fallback = Gtk::make_managed<Gtk::Label>("[image]");
            fallback->add_css_class("dim-label");
            fallback->set_halign(Gtk::Align::START);
            column->append(*fallback);
        }
    } else {
        content_label_ = Gtk::make_managed<Gtk::Label>();
        content_label_->set_halign(Gtk::Align::START);
        content_label_->set_xalign(0.0F);
        content_label_->set_wrap(true);
        content_label_->set_wrap_mode(Pango::WrapMode::WORD_CHAR);
        column->append(*content_label_);

        toggle_button_ = Gtk::make_managed<Gtk::Button>();
        toggle_button_->add_css_class("flat");
        toggle_button_->signal_clicked().connect(sigc::mem_fun(*this, &ClipCard::toggle_expand));
    }

    // Subordinate metadata row: pin marker, timestamp, image dimensions, and the
    // expand toggle — all small/dim so the content stays the visual focus.
    auto* meta = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, kMetaSpacing);
    if (entry.pinned) {
        auto* pin = Gtk::make_managed<Gtk::Image>();
        pin->set_from_icon_name("view-pin-symbolic");
        pin->add_css_class("accent");
        pin->set_pixel_size(kMetaIconSize);
        meta->append(*pin);
    }
    auto* time_label = Gtk::make_managed<Gtk::Label>(format_timestamp(entry.created_at));
    time_label->add_css_class("dim-label");
    time_label->add_css_class("caption");
    meta->append(*time_label);
    if (entry.kind == core::ClipKind::Image && entry.image_width > 0) {
        auto* dimension = Gtk::make_managed<Gtk::Label>(
            std::format("{}×{}", entry.image_width, entry.image_height));
        dimension->add_css_class("dim-label");
        dimension->add_css_class("caption");
        meta->append(*dimension);
    }
    if (toggle_button_ != nullptr) {
        // Push the expand toggle to the right edge of the metadata row.
        auto* spacer = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 0);
        spacer->set_hexpand(true);
        meta->append(*spacer);
        meta->append(*toggle_button_);
    }
    column->append(*meta);

    // Keep the common actions visible. A new user should not have to discover
    // undocumented modifier-clicks or keyboard shortcuts before they can paste,
    // pin, or remove an item.
    actions_ = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, kMetaSpacing);
    actions_->set_halign(Gtk::Align::END);

    auto* copy_button = Gtk::make_managed<Gtk::Button>("Copy");
    copy_button->add_css_class("flat");
    copy_button->signal_clicked().connect([this] { dispatch(ClipAction::Copy); });
    actions_->append(*copy_button);

    auto* paste_button = Gtk::make_managed<Gtk::Button>("Paste");
    paste_button->add_css_class("flat");
    paste_button->signal_clicked().connect([this] { dispatch(ClipAction::Paste); });
    actions_->append(*paste_button);

    if (entry.kind != core::ClipKind::Image) {
        auto* plain_button = Gtk::make_managed<Gtk::Button>("Plain text");
        plain_button->add_css_class("flat");
        plain_button->set_tooltip_text("Paste without formatting");
        plain_button->signal_clicked().connect([this] { dispatch(ClipAction::PastePlainText); });
        actions_->append(*plain_button);
    }

    auto* pin_button = Gtk::make_managed<Gtk::Button>(entry.pinned ? "Unpin" : "Pin");
    pin_button->add_css_class("flat");
    pin_button->signal_clicked().connect([this] { dispatch(ClipAction::TogglePin); });
    actions_->append(*pin_button);

    auto* delete_button = Gtk::make_managed<Gtk::Button>();
    delete_button->set_icon_name("user-trash-symbolic");
    delete_button->add_css_class("flat");
    delete_button->set_tooltip_text("Delete this clip");
    delete_button->signal_clicked().connect([this] { dispatch(ClipAction::Delete); });
    actions_->append(*delete_button);

    column->append(*actions_);

    row->append(*column);
    set_child(*row);

    if (content_label_ != nullptr) {
        render_text();
    }

    gesture_ = Gtk::GestureClick::create();
    gesture_->set_button(GDK_BUTTON_PRIMARY);
    gesture_->signal_pressed().connect(sigc::mem_fun(*this, &ClipCard::on_pressed));
    add_controller(gesture_);
}

void ClipCard::render_text() {
    const Preview preview = make_preview(entry_.content, max_chars_);

    content_label_->set_text(expanded_ ? entry_.content : preview.text);
    content_label_->set_ellipsize(expanded_ ? Pango::EllipsizeMode::NONE
                                            : Pango::EllipsizeMode::END);
    content_label_->set_lines(expanded_ ? -1 : kCollapsedLines);

    toggle_button_->set_visible(preview.truncated);
    if (preview.truncated) {
        toggle_button_->set_label(expanded_ ? "Show less" : "Show all");
    }
}

void ClipCard::toggle_expand() {
    expanded_ = !expanded_;
    render_text();
}

void ClipCard::on_pressed(int /*n_press*/, double x, double y) {
    // Child buttons handle their own clicks; the row must not also perform its
    // primary action.
    if (Gtk::Widget* target = pick(x, y, Gtk::PickFlags::DEFAULT); target != nullptr) {
        if ((toggle_button_ != nullptr &&
             (target == toggle_button_ || toggle_button_->is_ancestor(*target))) ||
            (actions_ != nullptr && (target == actions_ || actions_->is_ancestor(*target)))) {
            return;
        }
    }
    const Gdk::ModifierType state = gesture_->get_current_event_state();
    const bool ctrl = (state & Gdk::ModifierType::CONTROL_MASK) == Gdk::ModifierType::CONTROL_MASK;
    // Run the copy/pin on the next idle, not inside the gesture's "pressed"
    // dispatch: both rebuild the history list, destroying this very card while its
    // GestureClick is still active. Tearing a widget down mid-gesture corrupts
    // GTK's active-state accounting ("Broken accounting of active state") and can
    // crash. Capture the callback and entry by value so it stays valid once the
    // card (and `this`) is gone.
    const ClipAction action = ctrl ? ClipAction::TogglePin : ClipAction::FollowSettings;
    const ActionCallback callback = on_action_;
    const core::ClipboardEntry entry = entry_;
    Glib::signal_idle().connect_once([callback, entry, action] { callback(entry, action); });
}

void ClipCard::dispatch(ClipAction action) {
    const ActionCallback callback = on_action_;
    const core::ClipboardEntry entry = entry_;
    Glib::signal_idle().connect_once([callback, entry, action] { callback(entry, action); });
}

} // namespace voidclip::ui
