# RideCast — Phase 2 OpenStreetMap Bring-Up

**Hardware:** STM32H7B3I-DK (480 × 272 LCD, FT5336 touchscreen)  
**Software:** Zephyr 4.5.0, Windows PowerShell, ST-LINK  
**Scope:** Two *offline* demonstrations: **static map** (2A) and **interactive map** (2B). Neither needs the TI CC3351, internet access during runtime, GPS, a tile engine, routing, or NavBoxLib.

> **Commands:** Every PowerShell command in this guide is on **one line**. Run them individually. All builds use the **same** project-level `build/` directory; switching applications requires a pristine build. Run commands from **native Windows PowerShell**, not WSL.

## 1. Expected project layout

```text
zephyr_ridecast/
├── app/                              # Real RideCast application (unchanged)
├── build/                            # One shared Zephyr output directory
├── bringup/
│   └── openstreetmap/
│       ├── README.md                  # This guide
│       ├── static/                    # Phase 2A: static display
│       │   ├── CMakeLists.txt
│       │   ├── prj.conf
│       │   ├── map.png                # OpenStreetMap export (input)
│       │   ├── osm_map_preview.png    # Generated PC preview
│       │   ├── tools/
│       │   │   └── prepare_map.py
│       │   └── src/
│       │       ├── main.c
│       │       └── osm_map_rgb888.h   # Generated bitmap (shared with 2B)
│       └── interactive/               # Phase 2B: LVGL + touch
│           ├── CMakeLists.txt
│           ├── prj.conf
│           └── src/
│               └── main.c
├── cc335x_port/                      # Not needed for these tests
└── zephyr_workspace/
    ├── .venv/
    ├── zephyr/
    ├── .west/
    └── modules/
```

The bitmap is generated **once**, inside `static/src/`, and used by **both** applications. Do not duplicate the generated header in the interactive folder.

### Important updates if you moved the earlier Phase 2A files

The original Phase 2A app lived directly under `bringup/openstreetmap/`. After moving its files into `static/`, verify:

**`bringup/openstreetmap/static/CMakeLists.txt`:**

```cmake
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS "${CMAKE_CURRENT_LIST_DIR}/../../../zephyr_workspace/zephyr")
project(ridecast_osm_display_test LANGUAGES C)
target_sources(app PRIVATE src/main.c)
```

**`bringup/openstreetmap/interactive/CMakeLists.txt`:** The same relative Zephyr hint (`../../../zephyr_workspace/zephyr`) is correct because `interactive/` and `static/` are siblings.

**`bringup/openstreetmap/interactive/src/main.c`:** Change the existing image include to:

```c
#include "../../static/src/osm_map_rgb888.h"
```

Previously this pointed to `../../src/osm_map_rgb888.h`; that path is no longer correct after relocating the static source. Keep the remaining interactive code unchanged.

**`bringup/openstreetmap/static/tools/prepare_map.py`:** No path changes are required if the converter itself was moved into `static/tools/`—its existing root calculation will generate files inside `static/`.

## 2. Prerequisites

These should already be verified by Phase 1:

- Zephyr 4.5 workspace and Python virtual environment work on Windows.
- Windows Zephyr SDK contains a working ARM toolchain; `west build` succeeds.
- ST-LINK flashing succeeds with `west flash`.
- LCD color test, Zephyr LVGL sample, and touchscreen sample all pass.
- Your exported `static/map.png` is an OpenStreetMap image you are allowed to use with appropriate attribution.

Open **PowerShell in the `zephyr_ridecast` project root**, then activate your existing virtual environment:

```powershell
.\zephyr_workspace\.venv\Scripts\Activate.ps1
```

If this is a fresh clone, complete the Zephyr workspace setup first; these map demos are not a replacement for Phase 1.

## 3. Phase 2A — Static offline map

**Goal:** Show a real 480 × 272 street map on the LCD using the Zephyr display API, without LVGL interaction.

### 3.1 Prepare the image (only once, or after replacing `map.png`)

Export one map image from https://www.openstreetmap.org/ using its **Share → Image** option. Save it as `bringup/openstreetmap/static/map.png`.

From the project root:

```powershell
python -m pip install pillow
python .\bringup\openstreetmap\static\tools\prepare_map.py .\bringup\openstreetmap\static\map.png
Start-Process .\bringup\openstreetmap\static\osm_map_preview.png
```

The converter resizes/crops the input to 480 × 272, adds an offline-test label and OpenStreetMap attribution, and generates `static/src/osm_map_rgb888.h` containing **391,680 bytes** of 24-bit pixel data (RGB888 format, B-G-R byte order for this display path).

The PNG preview is only for checking on the PC. The generated header is what gets compiled into the STM32 program. The map is **not** downloaded by STM32.

### 3.2 Build and flash

Run these lines from the project root:

```powershell
cd .\zephyr_workspace
west build -p always -b stm32h7b3i_dk -s ..\bringup\openstreetmap\static -d ..\build
west flash -d ..\build
```

**Expected:** The full-screen OpenStreetMap image appears on the LCD, with readable streets and attribution. It is a static image; swiping and zooming do nothing.

**Typical UART output:**

```text
RideCast Phase 2 - OpenStreetMap static preview
LCD: 480 x 272, pixel format 0x...
Map displayed. This is OFFLINE ONLY; no Wi-Fi/network tested.
```

**Pass when:** The image matches the PC preview, correct colors appear, attribution is visible, and there is no flickering, corruption or unexpected reset.

## 4. Phase 2B — Offline interactive map

**Goal:** Reuse **the same static map image** under LVGL 9 and demonstrate finger panning, zoom buttons and a simulated position marker.

Before building, confirm this file exists:

```powershell
Test-Path ..\bringup\openstreetmap\static\src\osm_map_rgb888.h
```

Run the above command while inside `zephyr_workspace/`. Expected: `True`. If `False`, regenerate the image as described in Section 3.1 (from the project root).

### 4.1 Build and flash

From `zephyr_workspace/`:

```powershell
west build -p always -b stm32h7b3i_dk -s ..\bringup\openstreetmap\interactive -d ..\build
west flash -d ..\build
```

**Expected:** The existing map is visible with zoom controls, a zoom percentage, a red centre marker, and OpenStreetMap attribution.

### 4.2 How to test

1. **Start state:** Map starts at **150%** zoom.
2. **Pan:** Place one finger on the map and drag. The underlying map moves until its edge reaches the LCD boundary.
3. **Zoom in (`+`):** Increase zoom in **25%** steps up to **200%**.
4. **Zoom out (`−`):** Decrease zoom in **25%** steps down to **100%**.
5. **Marker:** The red marker remains at the **screen centre** while the map moves beneath it. This is a *simulated marker*, not GPS tracking.
6. **Boundaries:** Drag to all edges. The app should not show empty margins outside the map.
7. **Stability:** Pan and zoom repeatedly; verify no unintended resets, freezing or display artifacts.

Supported labels are **100%, 125%, 150%, 175%, 200%**. At **100%**, the source image is exactly the LCD size, so there is naturally no room to pan. At **200%**, the same pixels are enlarged—roads and text will look less sharp because no higher-resolution tiles are loaded.

**Typical UART output:**

```text
RideCast Phase 2B: offline map pan/zoom
Touch and drag map; use + and - buttons to zoom.
Zoom: 175%
Zoom: 200%
```

**Pass when:** Touch dragging, zoom buttons, boundary clamping and the fixed centre marker all behave correctly without crashes.

## 5. Fast development builds

**Important:** Both tests intentionally share **one** `build/` directory.

When **switching** from `static/` to `interactive/` or vice versa, use the full commands above with `-p always`. When **editing only source** in the *already configured* test, use this incremental build from `zephyr_workspace/`:

```powershell
west build -d ..\build -o=-j8
west flash -d ..\build
```

`-j8` is an example; adjust the parallel-job count to the Windows PC's available CPU/RAM. After changing the board or application source path, do a pristine build.

## 6. Troubleshooting

| Symptom | Check |
|---|---|
| `osm_map_rgb888.h: No such file` | Ensure `static/src/osm_map_rgb888.h` was generated and the **interactive** include uses `../../static/src/osm_map_rgb888.h`. |
| CMake cannot find Zephyr | Ensure both map-app CMake files use the correct `../../../zephyr_workspace/zephyr` hint; activate the existing `.venv`. |
| Static display is blank | Re-check the working Phase 1 display sample, pixel format `CONFIG_STM32_LTDC_RGB888=y`, and serial logs. |
| Colors are swapped | Regenerate the image with the supplied converter; it outputs B-G-R byte order required by the original test. |
| Interactive map does not respond to touch | Check `CONFIG_LV_Z_POINTER_FROM_CHOSEN_TOUCH=y` and `CONFIG_LV_Z_POINTER_INPUT=y` in `build/zephyr/.config`; confirm Phase 1 touchscreen still works. |
| Cannot pan at 100% | Expected: source image and LCD are both 480 × 272. |
| Map is blurry at 175–200% | Expected: scaling a single bitmap, not requesting new map tiles. |
| Build uses the wrong application | Run the intended app's `west build -p always ...` command to reconfigure the shared `build/` directory. |
| LVGL deprecated-API warnings | The older `lv_obj_add_flag` / `lv_obj_remove_flag` calls may warn; dedicated `lv_obj_set_clickable()` and `lv_obj_set_scrollable()` setters can replace them without changing functionality. |

## 7. What these demos do **not** verify

- No CC3351 SPI/Wi-Fi or BLE functionality.
- No live OpenStreetMap network requests or tile downloads.
- No GPS, navigation, routing or vehicle-position tracking.
- No performance guarantee for a future dynamic map engine.

These tests validate **offline image display** (2A) and **LVGL touch interaction / bitmap scaling** (2B). Do not add NavBoxLib to either test.

OpenStreetMap attribution must remain visible. See https://www.openstreetmap.org/copyright for attribution and data licensing information.

## 8. Validation record

| Test | What to verify | Result |
|---|---|---|
| 2A | Image visible; colors correct; attribution displayed | ☐ PASS / ☐ FAIL |
| 2B | Pan and zoom from 100% to 200%; marker remains centred | ☐ PASS / ☐ FAIL |
| 2B | Repeated operation without reset or visual corruption | ☐ PASS / ☐ FAIL |

Record the board revision, Zephyr version, build date and any serial logs when handing this workspace to another developer.
