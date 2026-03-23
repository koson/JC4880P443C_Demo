# ESP32-P4 Smart Home Dashboard — Design Specification
## Version 1.2  |  Portrait 480 × 800  |  LVGL v9

---

## 1. Project Context

- **Hardware**: ESP32-P4, JC4880P443C display, 480 × 800 portrait
- **Framework**: LVGL v9, FreeRTOS
- **Data source**: MQTT via mosquitto broker at 192.168.124.4
- **Middleware**: Node-RED bridges OpenWeatherMap and MQTT
- **Touch**: GT991,  swipe gesture for page navigation

---

## 2. Architecture

### Screen Model — Wide Canvas with Viewport (v1.2)

All pages live side by side on one wide `lv_obj_t` canvas created at boot.
The visible display is a 480px viewport sliding over a 2400px wide canvas.
This is the same model used by the LVGL9 music player demo — finger tracking
during swipe, snap to page boundary on release.

```
RAM layout:

  canvas  2400 × 800 px  (N_PAGES × SCR_W)
  ┌──────────┬──────────┬──────────┬──────────┬──────────┐
  │  page 0  │  page 1  │  page 2  │  page 3  │  page 4  │
  │   Home   │ Heating  │ Weather  │ Controls │  System  │
  │  480×800 │  480×800 │  480×800 │  480×800 │  480×800 │
  └──────────┴──────────┴──────────┴──────────┴──────────┘
       ↑
  viewport (480px wide) slides left/right over canvas
  all widgets exist at fixed absolute X positions
  MQTT updates all handles regardless of viewport position
```

**Key LVGL flags — the two lines that make it work:**
```c
lv_obj_add_flag(canvas, LV_OBJ_FLAG_SCROLL_ONE);
lv_obj_set_scroll_snap_x(canvas, LV_SCROLL_SNAP_CENTER);
```

- `SCROLL_ONE` — one swipe gesture moves exactly one page, cannot fling past two
- `SCROLL_SNAP_CENTER` — on finger release, snaps to centre of nearest page child

**Behaviour during swipe:**
```
finger down + drag   → viewport follows finger in real time
                        neighbouring page slides in from edge
                        half-and-half transition visible mid-swipe
                        (identical to LVGL9 music player demo)

finger release       → snaps to nearest page centre
                        smooth deceleration animation
                        always lands on complete page content

fast fling           → SCROLL_ONE limits to one page per gesture
                        cannot accidentally skip pages
```

### Canvas Setup
```c
#define N_PAGES  5
#define CANVAS_W (SCR_W * N_PAGES)   // 2400 px

static lv_obj_t *g_canvas;
static lv_obj_t *g_pages[N_PAGES];
static int32_t   s_cur_page = 0;

void build_canvas(lv_obj_t *scr)
{
    g_canvas = lv_obj_create(scr);
    lv_obj_set_size(g_canvas, SCR_W, SCR_H);    // viewport = screen size
    lv_obj_set_pos(g_canvas, 0, 0);
    lv_obj_set_style_bg_color(g_canvas, C_BG, 0);
    lv_obj_set_style_bg_opa(g_canvas, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(g_canvas, 0, 0);
    lv_obj_set_style_border_width(g_canvas, 0, 0);

    // scroll behaviour
    lv_obj_set_scroll_dir(g_canvas, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(g_canvas, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(g_canvas, LV_OBJ_FLAG_SCROLL_ONE);
    lv_obj_set_scroll_snap_x(g_canvas, LV_SCROLL_SNAP_CENTER);

    // create page children at fixed X positions
    for (int i = 0; i < N_PAGES; i++) {
        g_pages[i] = lv_obj_create(g_canvas);
        lv_obj_set_size(g_pages[i], SCR_W, SCR_H);
        lv_obj_set_pos(g_pages[i], i * SCR_W, 0);
        lv_obj_set_style_bg_color(g_pages[i], C_BG, 0);
        lv_obj_set_style_bg_opa(g_pages[i], LV_OPA_COVER, 0);
        lv_obj_set_style_pad_all(g_pages[i], 0, 0);
        lv_obj_set_style_border_width(g_pages[i], 0, 0);
        lv_obj_clear_flag(g_pages[i], LV_OBJ_FLAG_SCROLLABLE);
    }

    // page-change event — update dot indicator
    lv_obj_add_event_cb(g_canvas, on_scroll_end, LV_EVENT_SCROLL_END, NULL);
}

static void on_scroll_end(lv_event_t *e)
{
    lv_obj_t *canvas = lv_event_get_target(e);
    int32_t x = lv_obj_get_scroll_x(canvas);
    s_cur_page = LV_CLAMP(0, (x + SCR_W / 2) / SCR_W, N_PAGES - 1);
    update_page_dots(s_cur_page);
}
```

### MQTT → UI Data Flow
```
MQTT broker
    ↓  async
FreeRTOS queue  (g_mqtt_queue)       ← MQTT callback writes here only
    ↓  100ms lv_timer
mqtt_drain_cb()                      ← LVGL task, drains queue completely
    ↓  dispatch by topic string
dashboard_ui_update_*()              ← updates widget handles directly
    ↓
widget handles (g_lbl_*, g_tile_*)   ← exist at fixed positions on canvas
                                     ← updated whether in viewport or not
```

- No mutexes needed — queue is the only crossing point between domains
- All UI updates happen inside LVGL task — thread safe by design
- Off-viewport widgets update silently — page shows fresh data instantly on swipe
- No gesture detection code needed — LVGL scroll engine handles everything

### Comparison: Old Model vs New Model
```
Old (v1.1)                          New (v1.2)
────────────────────────────────────────────────────────────
5 × lv_obj_create(NULL) screens     5 × lv_obj_create(canvas) pages
lv_screen_load_anim()               LVGL scroll engine (automatic)
lv_indev gesture detection          not needed
manual debounce guard               not needed — SCROLL_ONE handles it
discrete jump between pages         fluid finger-tracked pan + snap
```

---

## 3. Design System — dashboard_theme.h

### Colour Palette

| Constant       | Hex       | Usage                                      |
|----------------|-----------|--------------------------------------------|
| `C_BG`         | `#0F1117` | Screen background                          |
| `C_CARD`       | `#1A1D27` | Card / panel surface                       |
| `C_CARD2`      | `#22263A` | Raised element on a card (button bg)       |
| `C_BORDER`     | `#2A2E42` | Card borders, dividers                     |
| `C_BLUE`       | `#4A90D9` | Energy / information                       |
| `C_TEAL`       | `#2ECC8E` | Success / online / good                    |
| `C_WARM`       | `#E8834A` | Active / heating / warning                 |
| `C_GOLD`       | `#E8C44A` | On-state dot / clear sky icon              |
| `C_RED`        | `#E05050` | Error / offline / danger                   |
| `C_PRI`        | `#F0F0F5` | Primary text (hero numbers, names)         |
| `C_SEC`        | `#9094AB` | Secondary text (labels, sub-values)        |
| `C_DIM`        | `#4A4F65` | Disabled / off-state / dim labels          |
| `C_TILE_WARM`  | `#1E1A10` | Active tile background (warm)              |
| `C_TILE_TEAL`  | `#121E13` | Active tile background (teal)              |
| `C_BDR_WARM`   | `#3A2E10` | Active tile border (warm)                  |
| `C_BDR_TEAL`   | `#1A3020` | Active tile border (teal)                  |
| `C_BTN_PRESSED`| `#2A2520` | Button pressed state                       |
| `C_THUNDER`    | `#AA88FF` | OWM icon — thunder                         |
| `C_SNOW`       | `#C8E0FF` | OWM icon — snow                            |
| `C_RAIN_HEAVY` | `#7777FF` | Rain ≥ 5mm                                 |

### Typography — Montserrat

| Alias         | Size  | Usage                                      |
|---------------|-------|--------------------------------------------|
| `FONT_HERO`   | 48px  | Time, temperature, live watts              |
| `FONT_SUB`    | 20px  | Status text, card sub-values               |
| `FONT_BODY`   | 16px  | Tile names, value readouts, date           |
| `FONT_LABEL`  | 14px  | Dim labels, status bar, hints              |

Enable in lv_conf.h:
```c
#define LV_FONT_MONTSERRAT_14  1
#define LV_FONT_MONTSERRAT_16  1
#define LV_FONT_MONTSERRAT_20  1
#define LV_FONT_MONTSERRAT_48  1
```

### Layout Grid

```c
#define SCR_W    480
#define SCR_H    800
#define MARGIN    12
#define GAP        8
#define CARD_W   (SCR_W - MARGIN * 2)   // 456 px
```

### Component Rules

| Component   | bg          | border          | radius | pad |
|-------------|-------------|-----------------|--------|-----|
| Card        | `C_CARD`    | `C_BORDER` 1px  | 12     | 0   |
| Tile        | `C_CARD`    | `C_BORDER` 1px  | 10     | 10  |
| Button      | `C_CARD2`   | `C_BDR_WARM` 1px| 8      | 0   |
| Status bar  | `C_CARD`    | none            | 0      | 0   |

### Status Dot Rules

| State       | fill      | size | shape  |
|-------------|-----------|------|--------|
| Off         | `C_DIM`   | 10px | circle |
| On (warm)   | `C_GOLD`  | 10px | circle |
| On (teal)   | `C_TEAL`  | 10px | circle |

### Tile Active State

| Element  | Warm active     | Teal active     | Inactive    |
|----------|-----------------|-----------------|-------------|
| bg       | `C_TILE_WARM`   | `C_TILE_TEAL`   | `C_CARD`    |
| border   | `C_BDR_WARM`    | `C_BDR_TEAL`    | `C_BORDER`  |
| dot      | `C_GOLD`        | `C_TEAL`        | `C_DIM`     |
| text     | `C_WARM`        | `C_TEAL`        | `C_DIM`     |

### Semantic Colour Thresholds

**WiFi RSSI**
```
> -70 dBm   → C_TEAL
> -85 dBm   → C_WARM
≤ -85 dBm   → C_RED
no signal   → C_DIM
```

**Internet RTT**
```
< 50 ms     → C_TEAL
< 200 ms    → C_WARM
≥ 200 ms    → C_RED
0 / unknown → C_DIM
```

**Power factor**
```
≥ 0.90      → C_TEAL
≥ 0.80      → C_WARM
< 0.80      → C_RED
```

**Rain mm**
```
0           → C_DIM
< 1 mm      → C_SEC
< 5 mm      → C_BLUE
≥ 5 mm      → C_RAIN_HEAVY (#7777FF)
```

**OWM weather icon colour map**
```
01 clear    → C_GOLD
02-04 cloud → C_SEC
09-10 rain  → C_BLUE
11 thunder  → C_THUNDER (#AA88FF)
13 snow     → C_SNOW (#C8E0FF)
else        → C_DIM
```

---

## 4. Information Architecture

### Progressive Disclosure Principle
Each page has ONE job:
- **Home page** — "Is everything OK?" — glanceable from 3 metres
- **Detail pages** — "What exactly is happening?" — read up close

### Page Map

| Index | Name      | Hero                    | Purpose                        |
|-------|-----------|-------------------------|--------------------------------|
| 0     | Home      | Time + Watts            | At-a-glance summary of all domains |
| 1     | Heating   | Heating / Idle (48px)   | Boiler status + controls       |
| 2     | Weather   | Temperature + icon      | Full weather detail + forecast |
| 3     | Controls  | Grid of tiles (no hero) | All binary on/off switches     |
| 4     | System    | None (list view)        | Engineer's diagnostic page     |

### Visual Hierarchy Rules
```
48px  C_PRI / C_BLUE / C_WARM   Hero        readable at 3 metres
20px  C_PRI / C_SEC             Sub-hero    readable at arm's length
16px  C_PRI                     Body        normal reading distance
14px  C_DIM                     Label/hint  detail on demand
13px  C_SEC (monospace)         System rows up close only
```

---

## 5. Page 0 — Home (Summary)

### Design Philosophy
Brutally simple. One signal per domain. Answers "is everything OK?" in one glance.
No min/max, no kWh detail, no power factor — all on detail pages.

### Layout
```
┌─────────────────────────────────┐
│  Sat 21 March     ●MQTT  ●WiFi  │  status bar H=30  C_CARD bg
├─────────────────────────────────┤
│                                 │
│         20:04                   │  48px  C_PRI     FONT_HERO
│  [icon]  8.2°    Partly cloudy  │  48px  C_PRI  +  20px C_SEC
│                                 │
│  ↓ 7°  ↑ 11°   Rochdale        │  14px  C_DIM     one line only
│                                 │
├─────────────────────────────────┤
│  whole-house power              │  14px  C_DIM
│                                 │
│         553 W                   │  48px  C_BLUE    FONT_HERO
│                                 │
│  9.9 kWh today                  │  16px  C_DIM     one line only
│                                 │
├──────────┬──────────┬──────────┬──────────┤
│ ●Boiler  │●Internet │ ●Lights  │  ●Rain   │  H=155  status tiles
│ Heating  │ Online   │  3 on    │   Dry    │  20px labels
└──────────┴──────────┴──────────┴──────────┘
         ●  ○  ○  ○  ○   page dots
```

### Status Tiles (bottom row)
- 4 equal tiles, each showing: dot (16px) + one word (20px)
- No numbers — colour tells the story from distance
- Boiler: Heating / Idle — warm/dim
- Internet: Online / Offline — teal/red
- Lights: "3 on" / "All off" — warm/dim
- Rain: Dry / Light / Rain / Heavy — dim/sec/blue/heavy

### What is NOT on home page
- Min/max temp → weather page
- Humidity, wind, pressure → weather page
- kWh yesterday, power factor, voltage → energy page (future)
- Boost button → heating page
- Individual light states → controls page
- RTT exact value → system page

---

## 6. Page 1 — Heating (Boiler)

### Specification
```
Hero:     "Heating"  C_WARM  48px   when boiler firing
          "Idle"     C_DIM   48px   when boiler not firing
          flame icon alongside hero (animate pulse when firing)

Mode badge (top right, always visible):
          "Winter"   C_WARM  — warm tinted pill
          "Summer"   C_SEC   — neutral pill

Button 1 — Mode toggle (always active):
          Segmented control: [Winter] [Summer]
          Selected = warm highlight, unselected = C_DIM
          Publishes: /BOI/mode  "winter" or "summer"

Button 2 — Boost (conditional):
          ENABLED  when: winter mode AND boiler idle
          DISABLED when: summer mode OR boiler already firing
          Label:   "Tap for 1 hr boost"
          Publishes: /BOI/home  "1"
          After tap: shows "Sent!" in C_TEAL for 2 seconds, then reverts
          Disabled reason shown: "not available — boiler already running"
```

### MQTT Topics
```
Subscribe:
  /BOI/power    "1"/"0"     → hero state
  /BOI/mode     "winter"/"summer" → mode badge + button 2 enable

Publish:
  /BOI/mode     "winter"/"summer" → button 1
  /BOI/home     "1"              → button 2 (boost)
```

### Supporting detail (below hero divider)
```
Boiler firing  ·  Winter mode     16px  C_SEC
since 06:14  ·  last ran 3 hrs ago   14px  C_DIM
target 20°   current 18.4°           14px  C_DIM
```

---

## 7. Page 2 — Weather

### Hero
- Large hi-res weather icon (left, ~52px circle placeholder until icon sourced)
- Current temperature: 48px `C_PRI` `FONT_HERO`
- Condition string: 20px `C_SEC` (from `/OWM/detail`)
- Min/max + sunrise/sunset: one line 14px `C_DIM` below divider
- "Feels like" + pressure: 13px `C_DIM`

### 24-Hour Forecast Strip (MOST IMPORTANT element)
8 slots × 3-hour intervals = 24 hours
```
slot:  [time] [icon circle] [temp°] [condition word]
now    21:00  00:00  03:00  06:00  09:00  12:00  15:00
```
- Dashed temperature trend line connecting slot centres
- Icon circle colours follow OWM colour map
- Requires Node-RED `/forecast` API call — topics:
```
/OWM/forecast/0/tempc    /OWM/forecast/0/wicon
/OWM/forecast/1/tempc    /OWM/forecast/1/wicon
... up to /OWM/forecast/7
```

### Stat Grid (2×3)
```
humidity    |  pressure
wind m/s dir|  rain mm
sunrise     |  sunset
```
All equal weight — 20px `C_PRI` values, 12px `C_DIM` labels
- Rain uses `theme_rain_color()` threshold
- Sunrise `C_GOLD`, sunset `C_WARM`

### 5-Day Forecast Strip
Compact 5-column card below the stat grid. One column per day.

```
┌────────┬────────┬────────┬────────┬────────┐
│ Mon 24 │ Tue 25 │ Wed 26 │ Thu 27 │ Fri 28 │  10px  C_DIM
│  [☁]  │  [🌧]  │  [☀]  │  [⛅]  │  [🌧]  │  icon circle
│ ↓7 ↑13 │ ↓5 ↑11 │ ↓6 ↑12 │ ↓8 ↑14 │ ↓4 ↑10 │  10px  min=C_DIM  max=C_SEC
│overcast│lt rain │clear   │few cld │lt rain │   9px  C_DIM / colour-coded
└────────┴────────┴────────┴────────┴────────┘
```

- Column width: `CARD_W / 5` = 91px each
- Icon circle: 18px radius, colour follows OWM colour map
- Rain icon adds 3 drop lines below cloud shape (stroke `C_BLUE`)
- Partly cloudy: small `C_GOLD` circle behind grey cloud ellipses
- Min temp: `C_DIM`, max temp: `C_SEC`
- Condition label: colour matches icon colour (`C_DIM` / `C_BLUE` / `C_GOLD`)
- Vertical `C_BORDER` 1px dividers between columns
- Horizontal `C_BORDER` 1px divider below section label

### Node-RED Forecast Pipeline

The 5-day forecast uses a **separate inject node** polling every 3 hours (not the 10-minute current-weather timer). This keeps API calls well within the free tier limit.

```
[inject every 3h] → [owm forecast node] → [function: summarise] → [MQTT out]
```

**Function node logic — summarise 40 entries to 5 daily rows:**
```javascript
const forecasts = msg.payload;
const daily = {};

forecasts.forEach(entry => {
    const date = new Date(entry.dt * 1000)
        .toLocaleDateString('en-GB', { weekday: 'short', day: 'numeric', month: 'short' });

    if (!daily[date]) {
        daily[date] = { min: Infinity, max: -Infinity, descriptions: {}, wicon: null };
    }

    // Track min/max across all 8 readings
    const t = entry.temp?.min ?? entry.temp;
    daily[date].min = Math.min(daily[date].min, t);
    daily[date].max = Math.max(daily[date].max, entry.temp?.max ?? entry.temp);

    // Count description occurrences — dominant wins
    const desc = entry.weather?.description ?? entry.weather?.main ?? '';
    daily[date].descriptions[desc] = (daily[date].descriptions[desc] || 0) + 1;

    // Store icon for dominant description slot
    daily[date].wicon = entry.weather?.icon ?? daily[date].wicon;
});

// Pick dominant description per day
const summary = Object.entries(daily).map(([date, d]) => ({
    date,
    min: Math.round(d.min),
    max: Math.round(d.max),
    condition: Object.keys(d.descriptions)
        .reduce((a, b) => d.descriptions[a] > d.descriptions[b] ? a : b),
    wicon: d.wicon
}));

msg.payload = summary;
return msg;
```

**MQTT topics published (one per day, index 0–4):**
```
/OWM/fc/0/date        "Mon 24"
/OWM/fc/0/minc        "7"
/OWM/fc/0/maxc        "13"
/OWM/fc/0/condition   "overcast clouds"
/OWM/fc/0/wicon       "04d"
... repeated for indices 1–4
```

### MQTT Topics Used
```
CURRENT WEATHER (every 10 min):
  /OWM/tempc          hero temperature
  /OWM/wicon          icon colour
  /OWM/weather        condition (fallback)
  /OWM/detail         condition (preferred)
  /OWM/temp_maxc      max temp (today)
  /OWM/temp_minc      min temp (today)
  /OWM/humidity       stat grid
  /OWM/pressure       stat grid
  /OWM/windspeed      stat grid
  /OWM/winddirection  stat grid (converted to compass)
  /OWM/rain           stat grid
  /OWM/sunrise_l      stat grid
  /OWM/sunset_l       stat grid
  /OWM/location       shown once at top

24-HOUR STRIP (every 10 min, from owm forecast node):
  /OWM/forecast/0/tempc    /OWM/forecast/0/wicon
  /OWM/forecast/1/tempc    /OWM/forecast/1/wicon
  ... up to /OWM/forecast/7

5-DAY STRIP (every 3 hours, pre-summarised by Node-RED function):
  /OWM/fc/0/date  /OWM/fc/0/minc  /OWM/fc/0/maxc  /OWM/fc/0/condition  /OWM/fc/0/wicon
  /OWM/fc/1/date  /OWM/fc/1/minc  /OWM/fc/1/maxc  /OWM/fc/1/condition  /OWM/fc/1/wicon
  /OWM/fc/2/date  /OWM/fc/2/minc  /OWM/fc/2/maxc  /OWM/fc/2/condition  /OWM/fc/2/wicon
  /OWM/fc/3/date  /OWM/fc/3/minc  /OWM/fc/3/maxc  /OWM/fc/3/condition  /OWM/fc/3/wicon
  /OWM/fc/4/date  /OWM/fc/4/minc  /OWM/fc/4/maxc  /OWM/fc/4/condition  /OWM/fc/4/wicon

DROP (never display):
  /OWM/tempk          Kelvin — never show
  /OWM/maxtemp        Kelvin duplicate
  /OWM/mintemp        Kelvin duplicate
  /OWM/id             internal OWM code
  /OWM/clouds         % redundant with icon
  /OWM/description    paragraph — no use
  /OWM/humidity_l     duplicate
  /OWM/sunrise        unix timestamp — use _l version
  /OWM/sunset         unix timestamp — use _l version
```

---

## 8. Page 3 — Controls

### Design Philosophy
No hero. Equal-weight grid. Pure binary on/off. Gallery layout — user scans, not reads.
Tap tile = toggle = publish to `/set` topic.
State confirmed by inbound `/state` topic (optimistic update on tap).

### Grid
- 2 columns, tiles `160 × 88px`, gap 8px
- Section label + 1px `C_BORDER` divider between groups

### Sections

**Lights**
```
Living room   Kitchen
Hallway       Bedroom
Bathroom      Office
```

**Other**
```
Garden fan    Security (override)
```

**Spare footprints** (dashed border, empty dot outline)
```
— spare —     — spare —
— spare —     — spare —
```

### Tile States
```
ON:   bg=C_TILE_WARM  border=C_BDR_WARM  dot=C_GOLD  name=C_PRI  "On"=C_WARM
OFF:  bg=C_CARD       border=C_BORDER    dot=C_DIM   name=C_SEC   "Off"=C_DIM
```

### Special labels
- Security light shows "Override on" not just "On" — signals manual override

### MQTT Topic Pattern
```c
typedef struct {
    const char *name;
    const char *topic_state;   // subscribe — "1"/"0"
    const char *topic_set;     // publish on tap
    bool        state;
    lv_obj_t   *tile;
    lv_obj_t   *dot;
    lv_obj_t   *lbl_state;
} control_t;

static control_t g_controls[] = {
    { "Living room", "/LIGHTS/living/state",   "/LIGHTS/living/set"   },
    { "Kitchen",     "/LIGHTS/kitchen/state",  "/LIGHTS/kitchen/set"  },
    { "Hallway",     "/LIGHTS/hallway/state",  "/LIGHTS/hallway/set"  },
    { "Bedroom",     "/LIGHTS/bedroom/state",  "/LIGHTS/bedroom/set"  },
    { "Bathroom",    "/LIGHTS/bathroom/state", "/LIGHTS/bathroom/set" },
    { "Office",      "/LIGHTS/office/state",   "/LIGHTS/office/set"   },
    { "Garden fan",  "/FAN/garden/state",      "/FAN/garden/set"      },
    { "Security",    "/LIGHTS/security/state", "/LIGHTS/security/set" },
};
```

Adding a new device = one line in the array. Nothing else changes.

### Drain loop pattern
```c
// matches any /LIGHTS/*/state or /FAN/*/state topic
else if (strncmp(t, "/LIGHTS/", 8) == 0 && strstr(t, "/state"))
    dashboard_ui_update_control(t, atoi(p) != 0);
else if (strncmp(t, "/FAN/", 5) == 0 && strstr(t, "/state"))
    dashboard_ui_update_control(t, atoi(p) != 0);
```

### Note
All-lights-off is handled automatically by Node-RED at 22:30 — no button needed on display.

---

## 9. Page 4 — System

### Design Philosophy
Engineer's diagnostic page. Read up close, standing at the display.
Smaller fonts, monospace precision, exact values + units.
No heroes, no colour-coded tiles — key:value rows with semantic colour on values only.

### Configurable Row Structure
```c
typedef struct {
    const char *section;   // NULL = continue current section
    const char *label;     // left side
    const char *unit;      // right side suffix
    char        value[32]; // updated at runtime
    lv_color_t  color;     // C_PRI / C_TEAL / C_WARM / C_RED / C_DIM
} sys_row_t;
```

Adding a row = one line of data. Renderer iterates and draws — no layout changes.

### Sections and Rows

**Network**
```
Internet RTT       8 ms          C_TEAL/WARM/RED (threshold)
WiFi RSSI         -72 dBm        C_TEAL/WARM/RED (threshold)
WiFi SSID          HomeNet-5G    C_PRI
MQTT broker        192.168.124.4 C_SEC
MQTT status        connected     C_TEAL / C_RED
MQTT uptime        4h 23m        C_PRI
IP address         192.168.124.42 C_SEC
— spare —
```

**System**
```
Uptime             2d 14h 22m    C_PRI
Free heap          184 KB        C_TEAL (healthy)
CPU freq           240 MHz       C_PRI
CPU temp           52 °C         C_WARM if high
— spare —
```

**Data freshness**
```
Last OWM update    4 min ago     C_PRI / C_WARN if stale
Last boiler msg    8 sec ago     C_TEAL
Last energy msg    7 sec ago     C_TEAL
— spare —
```

**Build** (C_DIM — static, least important)
```
Firmware           v1.0.3
Build date         21 Mar 2025
LVGL               v9.2.0
— spare —
```

---

## 10. Graph / History Page (Future — Page 5)

### RRD-Style Multi-Resolution Buffers
```c
// Fine — 1 hour at 7-second intervals (one per MQTT message)
#define FINE_POINTS   514
static int16_t g_watts_fine[FINE_POINTS];   // ~1 KB

// Coarse — 24 hours at 5-minute averages
#define COARSE_POINTS 288
static int16_t g_watts_coarse[COARSE_POINTS];  // ~576 bytes

// Total for 3 metrics (watts, temp, boiler) ≈ 18 KB
```

Buffers updated in background by `mqtt_drain_cb` regardless of active page.

### Layout
```
┌─────────────────────────────────┐
│  Last hour  (7s resolution)     │  lv_chart  LV_CHART_TYPE_LINE
│  ∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿  │  C_BLUE series (watts)
├─────────────────────────────────┤
│  Last 24 hrs  (5 min avg)       │  lv_chart  LV_CHART_TYPE_LINE
│  ∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿  │  C_WARM overlay (temp)
└─────────────────────────────────┘
```

### lv_chart setup
```c
lv_chart_set_update_mode(chart, LV_CHART_UPDATE_MODE_CIRCULAR);
lv_obj_set_style_size(chart, 0, LV_PART_INDICATOR);  // line only, no dots
```

---

## 11. Boot Sequence

```c
void dashboard_ui_create(lv_display_t *disp)
{
    lv_obj_t *scr = lv_display_get_screen_active(disp);
    lv_obj_set_style_bg_color(scr, C_BG, 0);

    // 1. Create wide canvas + all page children
    build_canvas(scr);           // sets up g_canvas and g_pages[0..4]

    // 2. Build all page content — all widget handles assigned before any MQTT arrives
    build_home_page(g_pages[0]);
    build_heating_page(g_pages[1]);
    build_weather_page(g_pages[2]);
    build_controls_page(g_pages[3]);
    build_system_page(g_pages[4]);

    // 3. Scroll to first page (no animation at boot)
    lv_obj_scroll_to_x(g_canvas, 0, LV_ANIM_OFF);

    // 4. Start timers AFTER all handles exist
    lv_timer_create(mqtt_drain_cb,  100,  NULL);
    lv_timer_create(status_poll_cb, 5000, NULL);
}
```

---

## 12. Key Design Principles Agreed

1. **Progressive disclosure** — home page answers "OK?" in one glance. Detail pages answer "what exactly?". User decides how deep to go.

2. **Colour before text** — status dots communicate from 3 metres before any word is read. C_TEAL=good, C_WARM=attention, C_RED=problem, C_DIM=off/inactive.

3. **One hero per page** — single dominant focal point per page. Home has two (time + watts) which is accepted dashboard pattern.

4. **Information density matches reading distance** — home page: large fonts, few items. System page: small fonts, dense rows. Each page designed for its expected viewing distance.

5. **Spare footprints** — unused tile slots shown with dashed border. Like unpopulated PCB pads. Adding a new device = one data line, no layout change.

6. **Correct by construction** — MQTT queue is the only crossing point between async MQTT domain and synchronous LVGL domain. No mutexes needed. Actor model / Erlang philosophy.

7. **Wide canvas viewport** — all pages side by side on one 2400×800 canvas. Viewport (480px) slides over it. Finger-tracked during swipe, snaps to page on release. Identical to LVGL9 music player demo. No gesture detection code needed — LVGL scroll engine handles everything natively.

8. **Theme as single source of truth** — dashboard_theme.h contains all colours, fonts, layout constants, and threshold helper functions. New pages just `#include "dashboard_theme.h"`.

9. **Empathy drives hierarchy** — every layout decision starts from "what does the person standing here need to know first?" Not what data is available, but what is useful at this moment, at this distance. Progressive disclosure is the answer: colour from 3 metres, numbers at arm's length, detail on demand.

---

## 13. Files Produced So Far

| File                       | Purpose                                      |
|----------------------------|----------------------------------------------|
| `dashboard_ui.h`           | Original public API (uploaded)               |
| `dashboard_ui.c`           | Original implementation (uploaded)           |
| `dashboard_theme.h`        | Extracted design system — all pages use this |
| `dashboard_design_spec.md` | This document                                |

### Next Step: Skeleton Code
Wide canvas + five page children, placeholder content, real LVGL, real fonts, real theme colours.
Static/fake data — no MQTT yet. Run on hardware to verify scroll feel and layout before wiring data.
Get `SCROLL_ONE` + `SCROLL_SNAP_CENTER` feeling right first — that is the veroboard moment.

---

*v1.2 — Architecture revised: replaced 5 × lv_screen model with single wide canvas (2400×800)
viewport model. Pages are children at fixed X positions. LVGL scroll engine handles finger
tracking and snap natively via LV_OBJ_FLAG_SCROLL_ONE + LV_SCROLL_SNAP_CENTER.
No gesture detection code required. Matches LVGL9 music player demo pattern.
Boot sequence updated. Design principle 7 updated, principle 9 added (empathy).*

*v1.1 — Added 5-day forecast strip to Page 2 (Weather): layout, icon rules, Node-RED pipeline,
summarisation function node, and MQTT topic map for `/OWM/fc/*` topics.*

*v1.0 — Specification derived from design session — covers architecture, theme, all 5 page layouts,
MQTT topic mapping, component rules, and agreed design principles.*
