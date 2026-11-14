#include "boot_splash.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lvgl.h>

#include "hal/hal.h"
#include "ui/design_system/lv_color.hpp"

// Embedded helmet PNG (EMBED_FILES "assets/helmet.png").
extern const uint8_t helmet_png_start[] asm("_binary_helmet_png_start");
extern const uint8_t helmet_png_end[] asm("_binary_helmet_png_end");

namespace spectra5::platform {

using spectra5::ui::lv_semantic;
using SemanticColor = spectra5::ui::tokens::SemanticColor;
namespace tokens    = spectra5::ui::tokens;

namespace {

// Random Mandalorian / Boba Fett one-liners for the loading line.
const char* const kQuotes[] = {
    "He's no good to me jammed.",
    "Acquiring targets...",
    "Bounty acquired: your Wi-Fi.",
    "This is the Way... to the handshake.",
    "No disintegrations. Just deauths.",
    "Tracking probe requests...",
    "Put the packets in the cargo hold.",
    "Calibrating reticle...",
    "Dank farrik, where's the SD card?",
    "I'm just a simple radio in the spectrum.",
    "As you wish, operator.",
    "Sleep tight... you're the AP now.",
};

// Epic one-liners for the one-time C6 provisioning screen.
const char* const kProvQuotes[] = {
    "He's no good to me jammed. Arming him.",
    "Loading the beskar payload...",
    "Priming the disintegrator...",
    "Bounty hardware coming online.",
    "This is the Way.",
    "Teaching the C6 some manners.",
    "Melting down a Jawa for spare cycles.",
    "Sharpening the reticle...",
};

lv_image_dsc_t g_helmet{};
lv_obj_t* g_prov_overlay = nullptr;  // "ARMING RADIO" overlay while it is up

// --- C6 flash consent dialog -------------------------------------------------
enum C6Choice : int { kC6Pending = 0, kC6Flash = 1, kC6NotNow = 2 };
std::atomic<int> g_c6_choice{kC6Pending};

// First answer wins (a tap racing the timeout cannot flip a decision).
void c6_choose(int choice)
{
    int expected = kC6Pending;
    g_c6_choice.compare_exchange_strong(expected, choice);
}

void on_c6_flash(lv_event_t*)
{
    c6_choose(kC6Flash);
}

void on_c6_not_now(lv_event_t*)
{
    c6_choose(kC6NotNow);
}

// Esc on either button = Not now. LVGL hands a keypad Esc to the focused object as
// LV_EVENT_KEY (LV_KEY_ESC) and then LV_EVENT_CANCEL; either one is enough.
void on_c6_key(lv_event_t* e)
{
    if (lv_event_get_code(e) == LV_EVENT_CANCEL || lv_event_get_key(e) == LV_KEY_ESC) {
        c6_choose(kC6NotNow);
    }
}

lv_obj_t* c6_label(lv_obj_t* parent, const char* s, SemanticColor color, const lv_font_t* font)
{
    lv_obj_t* l = lv_label_create(parent);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, lv_pct(100));
    lv_label_set_text(l, s);
    lv_obj_set_style_text_color(l, lv_semantic(color), 0);
    lv_obj_set_style_text_font(l, font, 0);
    return l;
}

lv_obj_t* c6_button(lv_obj_t* parent, const char* txt, SemanticColor color, lv_event_cb_t cb)
{
    lv_obj_t* b = lv_button_create(parent);
    lv_obj_set_size(b, lv_pct(45), 64);
    lv_obj_set_style_bg_color(b, lv_semantic(SemanticColor::SurfaceRaised), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(b, lv_semantic(color), 0);
    lv_obj_set_style_border_width(b, 2, 0);
    lv_obj_set_style_radius(b, tokens::RadiusSm, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    // Clear keyboard focus ring, so it is obvious which button Enter will press.
    lv_obj_set_style_outline_color(b, lv_semantic(color), LV_STATE_FOCUS_KEY);
    lv_obj_set_style_outline_width(b, 3, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_outline_pad(b, 4, LV_STATE_FOCUS_KEY);

    lv_obj_t* l = lv_label_create(b);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_color(l, lv_semantic(color), 0);
    lv_obj_set_style_text_font(l, &ibm_plex_mono_18, 0);
    lv_obj_center(l);

    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(b, &on_c6_key, LV_EVENT_KEY, nullptr);
    lv_obj_add_event_cb(b, &on_c6_key, LV_EVENT_CANCEL, nullptr);
    return b;
}

void splash_anim_done(lv_anim_t* a)
{
    lv_obj_t* overlay = static_cast<lv_obj_t*>(a->user_data);
    if (overlay != nullptr) {
        lv_obj_del_async(overlay);  // reveal the app underneath
    }
}

void set_bar_value(void* bar, int32_t v)
{
    lv_bar_set_value(static_cast<lv_obj_t*>(bar), v, LV_ANIM_OFF);
}

}  // namespace

void show_boot_splash()
{
    // Subtle startup chime as the helmet appears (persistent audio task -> no
    // task-delete abort under SPIRAM XIP; kept quiet on purpose).
    if (auto* hal = GetHAL()) {
        hal->setSpeakerVolume(28);
        hal->playStartupSfx();
    }

    lv_obj_t* ov = lv_obj_create(lv_layer_top());
    lv_obj_set_size(ov, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(ov, lv_semantic(SemanticColor::Background), 0);
    lv_obj_set_style_bg_opa(ov, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ov, 0, 0);
    lv_obj_set_style_pad_all(ov, 0, 0);
    lv_obj_clear_flag(ov, LV_OBJ_FLAG_SCROLLABLE);

    // Helmet logo (decoded from the embedded PNG via LODEPNG).
    g_helmet.header.magic = LV_IMAGE_HEADER_MAGIC;
    g_helmet.header.cf    = LV_COLOR_FORMAT_RAW;
    g_helmet.header.w     = 173;
    g_helmet.header.h     = 260;
    g_helmet.data         = helmet_png_start;
    g_helmet.data_size    = static_cast<uint32_t>(helmet_png_end - helmet_png_start);
    lv_obj_t* img         = lv_image_create(ov);
    lv_image_set_src(img, &g_helmet);
    lv_obj_align(img, LV_ALIGN_CENTER, 0, -110);

    lv_obj_t* title = lv_label_create(ov);
    lv_label_set_text(title, "TERM_I");
    lv_obj_set_style_text_color(title, lv_semantic(SemanticColor::TextPrimary), 0);
    lv_obj_set_style_text_font(title, &ibm_plex_mono_32, 0);
    lv_obj_set_style_text_letter_space(title, 8, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 40);

    lv_obj_t* sub = lv_label_create(ov);
    lv_label_set_text(sub, "OFFENSIVE WIRELESS TOOLKIT");
    lv_obj_set_style_text_color(sub, lv_semantic(SemanticColor::Accent), 0);
    lv_obj_set_style_text_font(sub, &ibm_plex_mono_16, 0);
    lv_obj_set_style_text_letter_space(sub, 4, 0);
    lv_obj_align(sub, LV_ALIGN_CENTER, 0, 86);

    // Random quote.
    lv_obj_t* quote = lv_label_create(ov);
    const unsigned idx = static_cast<unsigned>(lv_tick_get()) % (sizeof(kQuotes) / sizeof(kQuotes[0]));
    lv_label_set_text(quote, kQuotes[idx]);
    lv_obj_set_style_text_color(quote, lv_semantic(SemanticColor::TextSecondary), 0);
    lv_obj_set_style_text_font(quote, &ibm_plex_mono_16, 0);
    lv_obj_align(quote, LV_ALIGN_CENTER, 0, 130);

    // Loading bar.
    lv_obj_t* bar = lv_bar_create(ov);
    lv_obj_set_size(bar, 360, 6);
    lv_obj_align(bar, LV_ALIGN_CENTER, 0, 168);
    lv_obj_set_style_bg_color(bar, lv_semantic(SemanticColor::Border), LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, lv_semantic(SemanticColor::Accent), LV_PART_INDICATOR);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);

    // Tactical corner brackets.
    for (int i = 0; i < 4; ++i) {
        lv_obj_t* c = lv_obj_create(ov);
        lv_obj_set_size(c, 28, 28);
        lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, 0);
        lv_obj_set_style_radius(c, 0, 0);
        lv_obj_set_style_border_color(c, lv_semantic(SemanticColor::Accent), 0);
        lv_obj_set_style_border_width(c, 2, 0);
        const bool top  = (i < 2);
        const bool left = (i % 2 == 0);
        lv_border_side_t side = static_cast<lv_border_side_t>(
            (top ? LV_BORDER_SIDE_TOP : LV_BORDER_SIDE_BOTTOM) |
            (left ? LV_BORDER_SIDE_LEFT : LV_BORDER_SIDE_RIGHT));
        lv_obj_set_style_border_side(c, side, 0);
        lv_obj_align(c, top ? (left ? LV_ALIGN_TOP_LEFT : LV_ALIGN_TOP_RIGHT)
                            : (left ? LV_ALIGN_BOTTOM_LEFT : LV_ALIGN_BOTTOM_RIGHT),
                     left ? 24 : -24, top ? 24 : -24);
    }

    // Fill the bar over ~2.6s, then remove the whole splash.
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, bar);
    lv_anim_set_exec_cb(&a, set_bar_value);
    lv_anim_set_values(&a, 0, 100);
    lv_anim_set_time(&a, 5500);  // ~5.5s so the helmet sits long enough to read the quote
    lv_anim_set_user_data(&a, ov);
    lv_anim_set_ready_cb(&a, splash_anim_done);
    lv_anim_start(&a);
}

void show_provisioning_overlay()
{
    auto* hal = GetHAL();
    if (hal != nullptr) {
        hal->lvglLock();
    }

    lv_obj_t* ov = lv_obj_create(lv_layer_top());
    lv_obj_set_size(ov, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(ov, lv_semantic(SemanticColor::Background), 0);
    lv_obj_set_style_bg_opa(ov, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ov, 0, 0);
    lv_obj_set_style_pad_all(ov, 0, 0);
    lv_obj_clear_flag(ov, LV_OBJ_FLAG_SCROLLABLE);

    g_helmet.header.magic = LV_IMAGE_HEADER_MAGIC;
    g_helmet.header.cf    = LV_COLOR_FORMAT_RAW;
    g_helmet.header.w     = 173;
    g_helmet.header.h     = 260;
    g_helmet.data         = helmet_png_start;
    g_helmet.data_size    = static_cast<uint32_t>(helmet_png_end - helmet_png_start);
    lv_obj_t* img         = lv_image_create(ov);
    lv_image_set_src(img, &g_helmet);
    lv_obj_align(img, LV_ALIGN_CENTER, 0, -120);

    lv_obj_t* title = lv_label_create(ov);
    lv_label_set_text(title, "ARMING RADIO");
    lv_obj_set_style_text_color(title, lv_semantic(SemanticColor::Accent), 0);
    lv_obj_set_style_text_font(title, &ibm_plex_mono_32, 0);
    lv_obj_set_style_text_letter_space(title, 6, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 40);

    lv_obj_t* quote = lv_label_create(ov);
    const unsigned idx =
        static_cast<unsigned>(lv_tick_get()) % (sizeof(kProvQuotes) / sizeof(kProvQuotes[0]));
    lv_label_set_text(quote, kProvQuotes[idx]);
    lv_obj_set_style_text_color(quote, lv_semantic(SemanticColor::TextPrimary), 0);
    lv_obj_set_style_text_font(quote, &ibm_plex_mono_16, 0);
    lv_obj_align(quote, LV_ALIGN_CENTER, 0, 84);

    lv_obj_t* sub = lv_label_create(ov);
    lv_label_set_text(sub, "First boot: arming the C6 radio -- do not unplug (~20s).");
    lv_obj_set_style_text_color(sub, lv_semantic(SemanticColor::TextSecondary), 0);
    lv_obj_set_style_text_font(sub, &ibm_plex_mono_16, 0);
    lv_obj_align(sub, LV_ALIGN_CENTER, 0, 116);

    // Spinner: driven by the LVGL task, so it keeps turning while app_main blocks
    // on the OTA -- clear "working" feedback instead of a frozen screen.
    lv_obj_t* sp = lv_spinner_create(ov);
    lv_spinner_set_anim_params(sp, 1000, 60);
    lv_obj_set_size(sp, 58, 58);
    lv_obj_align(sp, LV_ALIGN_CENTER, 0, 170);
    lv_obj_set_style_arc_color(sp, lv_semantic(SemanticColor::Border), LV_PART_MAIN);
    lv_obj_set_style_arc_color(sp, lv_semantic(SemanticColor::Accent), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(sp, 6, LV_PART_MAIN);
    lv_obj_set_style_arc_width(sp, 6, LV_PART_INDICATOR);

    g_prov_overlay = ov;

    if (hal != nullptr) {
        hal->lvglUnlock();
    }
}

void hide_provisioning_overlay()
{
    auto* hal = GetHAL();
    if (hal != nullptr) {
        hal->lvglLock();
    }
    if (g_prov_overlay != nullptr) {
        lv_obj_delete(g_prov_overlay);
        g_prov_overlay = nullptr;
    }
    if (hal != nullptr) {
        hal->lvglUnlock();
    }
}

bool ask_c6_provision(unsigned timeout_s)
{
    auto* hal = GetHAL();
    g_c6_choice.store(kC6Pending);

    if (hal != nullptr) {
        hal->lvglLock();
    }

    // Full-screen scrim: swallows taps so nothing behind the dialog can be pressed.
    lv_obj_t* modal = lv_obj_create(lv_layer_top());
    lv_obj_set_size(modal, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(modal, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(modal, LV_OPA_80, 0);
    lv_obj_set_style_border_width(modal, 0, 0);
    lv_obj_set_style_radius(modal, 0, 0);
    lv_obj_clear_flag(modal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(modal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(modal, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* panel = lv_obj_create(modal);
    lv_obj_set_width(panel, lv_pct(90));
    lv_obj_set_style_max_width(panel, 860, 0);
    lv_obj_set_height(panel, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(panel, lv_pct(95), 0);
    lv_obj_set_style_bg_color(panel, lv_semantic(SemanticColor::Surface), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(panel, tokens::RadiusMd, 0);
    lv_obj_set_style_border_width(panel, 2, 0);
    lv_obj_set_style_border_color(panel, lv_semantic(SemanticColor::Warning), 0);
    lv_obj_set_style_pad_all(panel, tokens::SpaceXl, 0);
    lv_obj_set_style_pad_row(panel, tokens::SpaceMd, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(panel, LV_DIR_VER);

    lv_obj_t* title = c6_label(panel, LV_SYMBOL_WARNING "  Replace the radio firmware?",
                               SemanticColor::Warning, &ibm_plex_mono_32);
    lv_obj_set_style_text_letter_space(title, 1, 0);
    c6_label(panel,
             "Term_I needs its own firmware on the C6 radio (esp-hosted 1.4.0). "
             "Its Wi-Fi and Bluetooth tools do not work without it.",
             SemanticColor::TextPrimary, &ibm_plex_mono_18);
    c6_label(panel,
             "Flashing replaces the firmware that is on the C6 now. Other firmware on this "
             "Tab5 that needs a different C6 firmware (Sentinel Bootloader, and firmware "
             "built for esp-hosted 2.12) will have no Wi-Fi until the C6 is flashed back.",
             SemanticColor::TextPrimary, &ibm_plex_mono_18);
    c6_label(panel,
             "Not now: Term_I starts without its radio and asks again at the next start.",
             SemanticColor::TextSecondary, &ibm_plex_mono_16);
#ifdef TERMI_C6_FLASH_DRYRUN
    c6_label(panel, "DRY-RUN BUILD: \"Flash radio\" only logs; the C6 is not touched.",
             SemanticColor::Info, &ibm_plex_mono_16);
#endif

    lv_obj_t* row = lv_obj_create(panel);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, tokens::SpaceSm, 0);  // room for the focus ring
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_t* not_now =
        c6_button(row, LV_SYMBOL_CLOSE "  Not now", SemanticColor::TextPrimary, &on_c6_not_now);
    lv_obj_t* flash =
        c6_button(row, LV_SYMBOL_DOWNLOAD "  Flash radio", SemanticColor::Danger, &on_c6_flash);

    lv_obj_t* countdown = c6_label(panel, "", SemanticColor::TextSecondary, &ibm_plex_mono_16);
    lv_obj_set_style_text_align(countdown, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text_fmt(countdown, "Not now in %u s", timeout_s);

    // Keyboard: point the keypad indev(s) at a group holding only these two buttons,
    // so arrows/Tab cannot wander onto the UI behind the dialog (spectra5_nav_group()
    // also holds the rail). The previous groups are put back below.
    lv_group_t* group = lv_group_create();
    lv_group_set_wrap(group, true);
    lv_group_add_obj(group, not_now);
    lv_group_add_obj(group, flash);
    lv_group_focus_obj(not_now);  // the default answer
    constexpr std::size_t kMaxKeypads = 4;
    lv_indev_t* keypads[kMaxKeypads]   = {};
    lv_group_t* prev_group[kMaxKeypads] = {};
    std::size_t n_keypads              = 0;
    for (lv_indev_t* i = lv_indev_get_next(nullptr); i != nullptr && n_keypads < kMaxKeypads;
         i = lv_indev_get_next(i)) {
        if (lv_indev_get_type(i) == LV_INDEV_TYPE_KEYPAD) {
            keypads[n_keypads]    = i;
            prev_group[n_keypads] = lv_indev_get_group(i);
            lv_indev_set_group(i, group);
            ++n_keypads;
        }
    }

    if (hal != nullptr) {
        hal->lvglUnlock();
    }

    // Wait for the answer. The LVGL port task runs the dialog; this only polls.
    const TickType_t t0 = xTaskGetTickCount();
    unsigned shown      = timeout_s;
    while (g_c6_choice.load() == kC6Pending) {
        vTaskDelay(pdMS_TO_TICKS(100));
        const unsigned elapsed =
            static_cast<unsigned>(pdTICKS_TO_MS(xTaskGetTickCount() - t0) / 1000);
        if (elapsed >= timeout_s) {
            c6_choose(kC6NotNow);
            break;
        }
        if (timeout_s - elapsed != shown) {
            shown = timeout_s - elapsed;
            if (hal != nullptr) {
                hal->lvglLock();
            }
            lv_label_set_text_fmt(countdown, "Not now in %u s", shown);
            if (hal != nullptr) {
                hal->lvglUnlock();
            }
        }
    }
    const bool flash_it = g_c6_choice.load() == kC6Flash;

    if (hal != nullptr) {
        hal->lvglLock();
    }
    for (std::size_t k = 0; k < n_keypads; ++k) {
        lv_indev_set_group(keypads[k], prev_group[k]);
    }
    lv_group_delete(group);
    lv_obj_delete(modal);
    if (hal != nullptr) {
        hal->lvglUnlock();
    }
    return flash_it;
}

void show_provisioning_done()
{
    auto* hal = GetHAL();
    if (hal != nullptr) {
        hal->lvglLock();
    }

    lv_obj_t* ov = lv_obj_create(lv_layer_top());
    lv_obj_set_size(ov, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(ov, lv_semantic(SemanticColor::Background), 0);
    lv_obj_set_style_bg_opa(ov, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ov, 0, 0);
    lv_obj_set_style_pad_all(ov, 0, 0);
    lv_obj_clear_flag(ov, LV_OBJ_FLAG_SCROLLABLE);

    g_helmet.header.magic = LV_IMAGE_HEADER_MAGIC;
    g_helmet.header.cf    = LV_COLOR_FORMAT_RAW;
    g_helmet.header.w     = 173;
    g_helmet.header.h     = 260;
    g_helmet.data         = helmet_png_start;
    g_helmet.data_size    = static_cast<uint32_t>(helmet_png_end - helmet_png_start);
    lv_obj_t* img         = lv_image_create(ov);
    lv_image_set_src(img, &g_helmet);
    lv_obj_align(img, LV_ALIGN_CENTER, 0, -100);

    lv_obj_t* title = lv_label_create(ov);
    lv_label_set_text(title, LV_SYMBOL_OK "  RADIO ARMED");
    lv_obj_set_style_text_color(title, lv_semantic(SemanticColor::Success), 0);
    lv_obj_set_style_text_font(title, &ibm_plex_mono_32, 0);
    lv_obj_set_style_text_letter_space(title, 6, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 60);

    lv_obj_t* sub = lv_label_create(ov);
    lv_label_set_text(sub, "Rebooting -- this is the Way.");
    lv_obj_set_style_text_color(sub, lv_semantic(SemanticColor::TextSecondary), 0);
    lv_obj_set_style_text_font(sub, &ibm_plex_mono_16, 0);
    lv_obj_align(sub, LV_ALIGN_CENTER, 0, 104);

    if (hal != nullptr) {
        hal->lvglUnlock();
    }
}

}  // namespace spectra5::platform

// Weak hook so the cross-platform AppShell can raise the splash at the very start
// of its build (covering the UI as it renders, instead of flashing it first).
extern "C" void spectra5_show_boot_splash()
{
    spectra5::platform::show_boot_splash();
}
