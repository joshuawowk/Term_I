# CLAUDE.md — Term_I agent notes

Read this first when working in this repo.

**Term_I** is an offensive-security firmware for the M5Stack Tab5 (ESP32-P4 UI +
ESP32-C6 radio over ESP-Hosted/SDIO), renamed from upstream "Slave I".

## Name

The product was renamed **Slave I → Term_I on purpose**. Every on-device UI
string must read `Term_I` / `TERM_I` — never "Slave I"/"Slave_I". Renamed sites:
`app/core/version.hpp` (`kProjectName`, `kVersionLabel`), `platforms/tab5/main/
boot_splash.cpp`, `about_overlay.cpp`, `tests/unit/smoke_test.cpp`, and a decoy
SSID in `app/ui/screens/wifi_screen.cpp`. **Leave hardware/protocol uses of the
word "slave"** (esp_hosted "slave", I²C/SPI slave) untouched.

## Build (device, ESP-IDF 5.4.2)

```bash
export IDF_PATH=~/esp/esp-idf IDF_TOOLS_PATH=~/.espressif
source "$IDF_PATH/export.sh"
# The C6 image is embedded in the P4 app and must exist or the build FATALs:
mkdir -p platforms/tab5/managed_components/espressif__esp_hosted/examples/slave/build
cp <c6 network_adapter.bin> platforms/tab5/managed_components/espressif__esp_hosted/examples/slave/build/network_adapter.bin
cd platforms/tab5
idf.py -DSPECTRA5_RADIO_CAPTEST=0 -DSPECTRA5_C6_OTA=0 build
# Factory image for flashing at 0x0 / for the bmorcelli Launcher's SD install
#   (IDF esptool is v4 -> "merge_bin", underscore):
cd build && python -m esptool --chip esp32p4 merge_bin -o term-i-p4-factory.bin @flash_args
```

A prebuilt C6 image ships in the upstream 0day1day/Slave_I release
(`*-c6-network_adapter.bin`, ~1.12 MB) — embedding it is enough; you don't need to
build the C6 (which would need the esp32c6 toolchain).

## esp_hosted build fix (important)

Upstream **does not build against the stock esp_hosted 1.4.0** managed component:
`hosted_radio_engine.cpp` calls `esp_hosted_send_priv_command`, which only existed
in the author's uncommitted esp_hosted patch. It is now **defined in
`platforms/tab5/main/services/hosted_radio_engine.cpp`** (self-contained: `extern`
the internal `esp_hosted_tx`, build an esp_priv event `[0x55][len][payload]` on
`ESP_PRIV_IF` (=5), DMA-capable buffer). `0x55` = `SPECTRA5_PRIV_EVENT_COMMAND`,
matched by the C6's `spectra5_offensive` `process_priv_pkt`. Do **not** re-add the
old copy inside `managed_components/.../sdio_drv.c` (that dir is gitignored and
would duplicate the symbol).

## Keyboard (M5Stack A164)

The Tab5 keyboard (A164) is an **I²C 0x6D** STM32 smart keyboard on ExtPort1
(SDA=GPIO0, SCL=GPIO1, INT=GPIO50). Term_I reads it in
`platforms/tab5/main/hal/components/hal_usb.cpp` (`tab5_keyboard_task`, `KB_SMART_
ADDR 0x6D`), mapping chars to LVGL keys → the `spectra5_nav_group` indev. The
`platforms/tab5/components/keypad_scanner_tca8418` component is **dead code** (the
A164 is not a TCA8418; 0x34 is the PMIC). Verified on hardware: `M5 keyboard
detected`.

## Loading onto the Tab5

Term_I is a full-flash firmware. It's loaded via the **bmorcelli M5StackLauncher**
(sibling repo): put `term-i-p4-factory.bin` on the Tab5 SD and install it from the
Launcher's SD menu (extracts app + creates the SPIFFS data partitions), or flash at
`0x0` with esptool. First boot provisions the C6 over the air (~15 s "ARMING RADIO"
/ `C6 provisioned OK`) then reboots. See that repo's `CLAUDE.md` for the load
workflow and the SD-card quirk.

## Gotchas

- `.gitignore` excludes `build/`, `/dependencies/`, and `managed_components` — the
  esp_hosted fix therefore lives in tracked `main` source, not the component.
- Desktop SDL sim needs GCC 13+ (`#include <format>`); the device build uses the
  IDF toolchain and is unaffected.
