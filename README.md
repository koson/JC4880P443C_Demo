# ESP32-P4 + JC4880P443C Smart Home Dashboard

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![IDF](https://img.shields.io/badge/ESP--IDF-5.5.2-blue)](https://github.com/espressif/esp-idf)
[![LVGL](https://img.shields.io/badge/LVGL-v9-green)](https://lvgl.io)

A clean, working implementation for the **JC4880P443C** 480×800 MIPI-DSI display module from AliExpress, running on the **ESP32-P4** with **LVGL v9**, **GT911 capacitive touch**, **WiFi**, and **MQTT**. Built as a smart home dashboard but the display and touch layer is fully reusable for any project.

---

## Why This Exists

The JC4880P443C is a capable display module — ST7701 controller, 480×800 portrait, MIPI-DSI, capacitive touch — and it is genuinely cheap on AliExpress. The problem is that the only "example" code available is ripped from Espressif's internal BSP for the ESP32-P4 evaluation board, with hacked-in init sequences, unexplained magic numbers, and no documentation. It is very difficult to understand what is actually happening or how to adapt it for your own project.

This repository is the clean version. Every decision is explained. The display init sequence is extracted and documented. The touch driver is wired in properly using the modern IDF 5.x `i2c_master` API. The LVGL port follows the LVGL v9 API correctly. The WiFi and MQTT layers use the actor/message-queue pattern so there are no mutexes and no threading surprises.

If you have bought one of these modules and are staring at the BSP code wondering where to start — this is that starting point.

---

## Hardware

| Item | Detail |
|------|--------|
| MCU | ESP32-P4 |
| Display module | JC4880P443C (AliExpress) |
| Display controller | ST7701 |
| Resolution | 480 × 800 portrait |
| Display interface | MIPI-DSI, 2 data lanes |
| Touch controller | GT911 capacitive, I2C |
| Flash | 16 MB |
| PSRAM | Required (SPIRAM enabled) |

### Pin Assignments

These are the pins used in this project. If your board routes them differently, update the `#define` block near the top of `main/jc4880p443c_demo.c`.

| Signal | GPIO |
|--------|------|
| LCD reset | GPIO 5 |
| Backlight PWM | GPIO 23 |
| Touch SDA | GPIO 7 |
| Touch SCL | GPIO 8 |
| Touch RST | GPIO 35 |
| Touch INT | GPIO 3 |
| MIPI PHY LDO channel | 3 (2500 mV) |

The MIPI DSI data lanes and clock are handled internally by the ESP32-P4 MIPI DSI peripheral — no separate GPIO assignment is needed for those.

---

## What is Included

```
JC4880P443C_Demo/
├── components/
│   └── jc4880p443c/          ST7701 init sequence + timing constants
│       ├── jc4880p443c.c      Validated init commands (39 commands)
│       ├── jc4880p443c.h      Public API — get_init_cmds, get_timing, etc.
│       └── idf_component.yml
├── main/
│   ├── jc4880p443c_demo.c     Top-level: display, touch, LVGL, app_main
│   ├── dashboard_ui.c         LVGL UI — screens, widgets, update functions
│   ├── dashboard_ui.h         Public UI API
│   ├── wifi_manager.c         WiFi station — connects, exposes g_wifi_state
│   ├── wifi_manager.h
│   ├── mqtt_manager.c         MQTT client — feeds g_mqtt_queue
│   ├── mqtt_manager.h
│   ├── Kconfig.projbuild      WiFi SSID/password + MQTT broker via menuconfig
│   ├── CMakeLists.txt
│   └── idf_component.yml
├── partitions.csv             Custom partition table (16 MB flash)
├── sdkconfig.defaults         Pre-configured for ESP32-P4
└── README.md
```

---

## Requirements

- **ESP-IDF 5.5.x** — this project uses the new `i2c_master` driver API introduced in IDF 5.x. It will not build on IDF 4.x or early 5.x releases.
- **Python 3.8+** (for IDF tools)
- A local **MQTT broker** (Mosquitto recommended) if you want live dashboard data. The display and touch work without it.

The following components are fetched automatically by the IDF Component Manager on first build:

| Component | Version |
|-----------|---------|
| `lvgl/lvgl` | ^9.0.0 |
| `espressif/esp_lcd_st7701` | ^2.0.2 |
| `espressif/esp_lcd_touch` | ^1.1.0 |
| `espressif/esp_lcd_touch_gt911` | ^1.1.0 |
| `espressif/esp_wifi_remote` | >=0.10,<2.0 |
| `espressif/esp_hosted` | ~2 |

> **Note on WiFi:** The ESP32-P4 has no built-in WiFi silicon. It uses an external coprocessor via `esp_wifi_remote` and `esp_hosted`. These components make the standard `esp_wifi` API work transparently — the rest of the code is unchanged from a normal WiFi project.

---

## Getting Started

### 1. Clone and enter the directory

```bash
git clone https://github.com/lalith-ais/JC4880P443C_Demo.git
cd JC4880P443C_Demo
```

### 2. Set your target

```bash
idf.py set-target esp32p4
```

### 3. Configure WiFi and MQTT

```bash
idf.py menuconfig
```

Navigate to **Dashboard WiFi Configuration** and enter your SSID and password.
Navigate to **Dashboard MQTT Configuration** and enter your broker URI, for example `mqtt://192.168.1.10`.

If you just want to test the display and touch without any network, skip this step — the dashboard will start with `WiFi --` and `MQTT --` in the status bar and everything else will work normally.

### 4. Build, flash, and monitor

```bash
idf.py -p /dev/ttyACM0 build flash monitor
```

Replace `/dev/ttyACM0` with your actual serial port (`COM3` on Windows, `/dev/tty.usbmodem*` on macOS).

### Expected boot log

```
I (1528) DEMO: Smart home dashboard — ESP32-P4
I (1623) st7701_mipi: LCD ID: FF FF FF
I (1764) DEMO: Display initialized successfully
I (1768) DEMO: Initializing GT911 touch controller
I (1838) GT911: TouchPad_ID:0x39,0x31,0x31
I (1839) GT911: TouchPad_Config_Version:250
I (1839) DEMO: GT911 touch initialized (SDA=7 SCL=8 RST=35 INT=3)
I (1842) DEMO: Touch registered as LVGL input device
I (1850) DEMO: System ready — 480x800, touch active
```

The GT911 reporting `TouchPad_ID: 0x39, 0x31, 0x31` is correct — that is ASCII for "911", the chip literally spells its own name. Touch is working if you see that line.

---

## How the Display Driver Works

The ST7701 is initialised over the MIPI DSI command channel (DBI mode) before the panel switches to video streaming (DPI mode). The key insight — which is not documented anywhere obvious — is that the init sequence must be sent while the DSI bus is in command mode, and DPI video only starts after `esp_lcd_panel_init()` completes.

The `jc4880p443c` component encapsulates everything specific to this module:

- The 39-command ST7701 init sequence
- The DSI lane configuration (2 lanes, 750 Mbps)
- The DPI video timing (`pclk=34 MHz`, `HBP=42`, `HFP=42`, `VBP=8`, `VFP=166`)

These values were extracted from the manufacturer's BSP and validated on hardware. If you have a different ST7701-based panel you can create your own component alongside this one following the same interface, and swap it in `display_init()` with a single include change.

The `LCD ID: FF FF FF` line in the boot log is normal for this module — the ST7701 does not respond to the standard ID read command on this panel variant. It does not indicate a fault.

---

## How the Touch Driver Works

The GT911 communicates over I2C via Espressif's `esp_lcd_panel_io` abstraction, which gives a unified interface shared with the display command channel. Two things commonly cause problems when setting this up from scratch:

**I2C address selection at power-on**

The GT911 selects its I2C address based on the state of the INT pin during the hardware reset sequence:

- INT held low at reset → address `0x5D` (default, used here)
- INT held high at reset → address `0x14`

The driver handles this automatically when RST and INT GPIO numbers are provided. If your module does not respond, try swapping `dev_addr` to `ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP` (`0x14`) in `touch_init()` in `jc4880p443c_demo.c`.

**IDF 5.x requires `scl_speed_hz` on the device config**

The new `i2c_master` driver moved the clock frequency from the bus-level config to the per-device config. The `ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG()` convenience macro does not set this field, so without the explicit line below you will see `invalid scl frequency` and the touch driver will fail silently:

```c
esp_lcd_panel_io_i2c_config_t tp_io_cfg = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
tp_io_cfg.scl_speed_hz = 400000;  // required — the macro leaves this zero
```

This is documented here because it is easy to miss and costs real debugging time.

---

## Architecture

### Thread model

There are three concurrent contexts at runtime:

| Context | Core | Responsibility |
|---------|------|---------------|
| `app_main` | 0 | Startup sequence, WiFi, MQTT init |
| MQTT client task (esp-mqtt) | 0 | Receives messages, posts to `g_mqtt_queue` |
| `lvgl` task | 1 | All rendering, touch polling, UI updates |

### The MQTT → UI data flow

The fundamental rule is: **no `lv_*` function is ever called from outside the LVGL task.** This eliminates the need for mutexes entirely and makes the threading model trivial to reason about.

```
MQTT broker
    │
    ▼  (esp-mqtt client task, Core 0)
g_mqtt_queue              ← the only crossing point between domains
    │
    ▼  (100 ms lv_timer, inside LVGL task, Core 1)
mqtt_drain_cb()           ← drains queue, dispatches by topic string
    │
    ▼
dashboard_ui_update_*()   ← updates widget label/colour/state directly
    │
    ▼
Widget handles             ← permanent in RAM, update whether visible or not
```

Incoming MQTT messages are packed into a small `mqtt_message_t` struct (topic + payload strings) and posted to a FreeRTOS queue. An `lv_timer` running inside the LVGL task drains that queue every 100 ms and calls the appropriate update function. Off-screen pages update silently — swipe to any page and the data is already current.

### Page model

All five pages are created once at boot and kept alive in RAM throughout the session. Page switching uses LVGL's built-in slide animation with the delete flag set to `false`:

```c
lv_screen_load_anim(g_screens[n], LV_SCR_LOAD_ANIM_MOVE_LEFT, 200, 0, false);
//                                                                         ^^^^
//                                             false = keep old screen in RAM
```

This works because the ESP32-P4 has sufficient RAM. It makes page switching instant with no rebuild cost, and allows off-screen widget handles to remain valid for background updates.

---

## Dashboard Pages

| Page | Name | Purpose |
|------|------|---------|
| 0 | Home | At-a-glance summary — time, live power, domain status dots |
| 1 | Heating | Boiler state, winter/summer mode toggle, boost button |
| 2 | Weather | Current conditions, 24-hour forecast strip, 5-day forecast |
| 3 | Controls | Binary on/off tile grid for lights and devices |
| 4 | System | Engineer's diagnostic — RSSI, heap, uptime, data freshness |

Navigate between pages by swiping left or right.

### MQTT topics consumed

The dashboard subscribes to topics published by a Node-RED flow that bridges OpenWeatherMap and local home automation:

| Namespace | Data |
|-----------|------|
| `/OWM/*` | Weather — temperature, condition, icon, wind, humidity, forecast |
| `/HALL/*` | Energy — live watts, today kWh, power factor, voltage |
| `/BOI/*` | Boiler — firing state, winter/summer mode |
| `/LIGHTS/*/state` | Individual light on/off states |
| `/FAN/*/state` | Fan states |
| `/INTERNET/rtt` | Latency to internet in ms |
| `/SYS/time`, `/SYS/datel` | Current time and date strings from Node-RED |

The boost button on the heating page publishes `"1"` to `/BOI/home`.

---

## Adapting for Your Own Project

### Display and touch only, no WiFi/MQTT

Remove `wifi_manager.c`, `mqtt_manager.c`, and `dashboard_ui.c` from `CMakeLists.txt`. Remove the corresponding `#include` lines and function calls from `jc4880p443c_demo.c`. Replace `dashboard_ui_create()` with your own LVGL code. The display and touch initialisation in `jc4880p443c_demo.c` does not need to change.

### Different display panel

Create a new component alongside `jc4880p443c/` with your own implementations of `get_init_cmds()`, `get_timing()`, `get_resolution()`, and `get_dsi_config()`. The `display_init()` function calls these and will work with any panel that follows the same interface.

### Changing pin assignments

All hardware pin numbers are `#define` constants grouped together near the top of `main/jc4880p443c_demo.c`. Change them there and nowhere else.

### Adding a new MQTT topic

Add a case to the `if/else` dispatch chain in `mqtt_drain_cb()` inside `dashboard_ui.c`, and add a corresponding `dashboard_ui_update_*()` function. No other files need to change.

### Adding a control tile (lights, fan, etc.)

Add a single entry to the `g_controls[]` array in `dashboard_ui.c`:

```c
{ "My device", "/MYDEV/state", "/MYDEV/set" },
```

The tile renderer iterates the array automatically. No layout code changes are needed.

---

## Troubleshooting

**Display shows nothing / backlight is off**

Check that GPIO 23 (backlight PWM) is wired correctly. Check that LDO channel 3 at 2500 mV is available on your board — this powers the MIPI DSI PHY and the display will not initialise without it. Verify GPIO 5 (LCD reset) is connected.

**Display initialises but image looks wrong**

The ST7701 init sequence in `jc4880p443c.c` is specific to this module. A different panel revision may need a different sequence. The `LCD ID: FF FF FF` in the boot log is normal and does not indicate a problem — some ST7701 variants do not respond to ID reads.

**Touch not initialising — `invalid scl frequency`**

You are on a version of the code missing the `tp_io_cfg.scl_speed_hz = 400000` line in `touch_init()`. See the touch driver section above.

**Touch initialises but no response to taps**

Confirm the I2C pins match your board (SDA=7, SCL=8 by default). Check that the boot log shows the `TouchPad_ID: 0x39, 0x31, 0x31` line — if it does, the chip is communicating correctly. If touch registers but coordinates are wrong (mirrored or transposed), adjust `swap_xy`, `mirror_x`, or `mirror_y` in `tp_cfg` inside `touch_init()`.

**Touch initialises but I2C NACKs during operation**

Try changing `dev_addr` in `touch_init()` to `ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP` (0x14). The address depends on the INT pin state at power-on and may differ between board revisions.

**WiFi not connecting**

Credentials are set via `idf.py menuconfig` → Dashboard WiFi Configuration, not in source code. Make sure `espressif/esp_wifi_remote` and `espressif/esp_hosted` components are present — they are listed in `idf_component.yml` and will be fetched on first build, but if the build was interrupted they may be incomplete. Run `idf.py update-dependencies` to force a re-fetch.

**Build fails with "component not found"**

Run `idf.py update-dependencies` to force the component manager to re-fetch all managed dependencies.

---

## Key IDF 5.x API Changes

If you are porting from an older project or following older tutorials, these are the specific things that changed and affect this codebase:

- The `i2c` driver was replaced by `i2c_master`. Use `i2c_new_master_bus()` and `i2c_master_bus_config_t` — the old `i2c_param_config()` / `i2c_driver_install()` pattern is gone.
- Clock frequency in the new I2C API is set on the **device** config via `scl_speed_hz`, not on the bus config.
- LVGL v9 changed the display buffer API — use `lv_display_set_buffers()` directly. The old `lv_disp_draw_buf_init()` / `lv_disp_drv_init()` pattern no longer exists.
- `esp_lcd_touch_get_coordinates()` is deprecated in current `esp_lcd_touch` — use `esp_lcd_touch_get_data()` instead.

---

## Licence

MIT. Do whatever you like with it. If it saves you the hours of confusion it took to put together, that is more than enough.
