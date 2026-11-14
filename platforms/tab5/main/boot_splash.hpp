#pragma once

namespace spectra5::platform {

// Show the Boba-Fett boot splash (helmet logo + loading bar + a random Mandalorian
// quote) as a top-layer overlay that removes itself after a few seconds. The app UI
// is built underneath, so a decode/asset failure can never block boot.
void show_boot_splash();

// Show a persistent full-screen "arming the radio" overlay (helmet + spinner) for
// the one-time first-boot C6 provisioning. Unlike the boot splash it does NOT remove
// itself: the P4 reboots when provisioning finishes, so it just stays up (the LVGL
// task keeps the spinner animating even while app_main blocks on the OTA). Takes the
// LVGL lock internally, so it is safe to call from app_main.
void show_provisioning_overlay();

// Remove the "arming the radio" overlay again. Only needed when provisioning returns
// instead of rebooting (it failed, or this is a TERMI_C6_FLASH_DRYRUN build), so the
// app underneath is usable. Takes the LVGL lock.
void hide_provisioning_overlay();

// Ask before replacing the ESP32-C6 radio firmware. The C6 is shared with every other
// firmware on this Tab5, so Term_I never flashes it unasked. Shows a modal with
// "Not now" (focused by default) and "Flash radio", and BLOCKS the caller until the
// user answers or timeout_s elapses; the LVGL port task keeps the dialog live
// meanwhile. Touch, or the keyboard: arrows/Tab move, Enter picks, Esc = Not now.
// Returns true only on an explicit "Flash radio"; Not now, Esc and the timeout all
// return false. Takes the LVGL lock internally; call from app_main after app::Init().
bool ask_c6_provision(unsigned timeout_s);

// Full-screen "RADIO ARMED -- rebooting" confirmation, shown for a moment after a
// successful C6 provision so the imminent reboot (a brief blank/blue while the chip
// resets) reads as the last step of the process, not a hang. Takes the LVGL lock.
void show_provisioning_done();

}  // namespace spectra5::platform
