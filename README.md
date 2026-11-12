# OpenSlate

Open-source ESP32 BLE bridge that turns a radio
switch (or your arming switch!) into record control for **DJI Osmo Action**
and **GoPro HERO8+** cameras, and pushes live camera telemetry into the
Betaflight OSD - with a built-in **mobile-first Web UI**.

Builds for both **ESP32-C3** and the classic **ESP32** (DevKitC / WROOM-32)
from one source tree - see [Step 3 - Build & flash the firmware](#step-3---build--flash-the-firmware).

> **Requires a Betaflight build with Custom Message OSD elements**
> (`MSP2_SET_TEXT` 0x3007, custom message types 7-10). Betaflight 4.x and
> 4.5 are **not** supported.
>
> **Not affiliated with or endorsed by DJI or GoPro.**
> All protocols based on public documentation and own reverse-engineering.

---

## Features

- **RC-switch record control** - start/stop recording from *any* AUX channel,
  read straight from Betaflight via MSP. Channel, threshold and debounce are
  configurable from the Web UI - no recompiling.
- **Record-on-arm** - optional auto-start when the FC arms, with optional
  stop-on-disarm. Toggle it in the Web UI.
- **Parallel OSD telemetry on all 4 Custom Messages** - assign any of
  Cam status / Rec time / Battery / Link state / FC battery / Arm state /
  Cam count / Off to each slot via the Web UI. Pushed with `MSP2_SET_TEXT`
  (MSP v2, `0x3007`).
- **Two camera backends** - DJI Osmo Action (DUML over BLE) and GoPro HERO8
  through HERO13 (official Open GoPro BLE API), switchable at runtime.
- **Saved-camera registry + discovery scanning** - scan for nearby cameras
  from the Web UI, **Pair** the one that's yours (up to 4 saved), and
  the ESP32 auto-reconnects to it forever after - no ghost devices, nothing
  is written to flash without your explicit consent.
- **Hotspot off while armed** (on by default) - Wi-Fi powers down when the FC
  arms and comes back on disarm, so the shared 2.4 GHz radio belongs to the
  camera links in flight (also saves ~60-100 mA).
- **Wi-Fi power switch** - optionally assign a spare AUX channel to toggle the
  Web-UI hotspot on/off by hand (the BLE camera link keeps running).
- **Fast stickcam reconnect** - a camera that is out of range during the
  flight is searched for continuously at low radio duty (25 %) and caught
  as soon as it advertises again after landing; GATT setup runs on a fast
  connection interval (~1-1.5 s from link to ready). A search never blocks
  the copter camera: the shared BLE worker is handed over in 4 s slices.
  Camera commands are absolute start/stop (never toggles), so a STOP queued
  while the stickcam was away is sent the moment it is back.
- **Built-in Web UI** - connect to the ESP32's Wi-Fi network and the
  dashboard opens automatically (captive portal): live status, one-tap
  record button, all configuration with auto-save, OTA firmware updates,
  light & dark mode. Mobile-first, no internet needed.
- **Persistent settings** - everything you configure lives in NVS flash.
- **Status LED patterns** - know your link state at a glance on the bench.
- **Modular firmware** - `msp_protocol` (FC side), `fc_status` (arming),
  `dji_camera` / `gopro_camera` (camera side), `camera_manager` (dispatch),
  `cam_registry` + `scan_results` (pairing), `recorder` (decision engine),
  `osd_slots` (OSD), `wifiswitch` (AP power), `web_server` + `web_assets`
  (UI).

---

## How It Works

```
 +-----------+  RC frames   +------------------+ MSP (UART) +---------------+
 | Radio TX  | -----------> | Flight Controller| <--------> |  ESP32-C3 /   |
 +-----------+              |   (Betaflight)   |  Serial1   |  ESP32 (WROOM)|
                            +------------------+            +-------+-------+
                                     ^                              | BLE
                                     |                              +-> DJI DUML
                          HD OSD custom                             +-> GoPro Open
                          messages 1-4 <-- telemetry --+                BLE API
                                                                       |
                 phone/PC  <-- Wi-Fi AP + captive portal --------------+
                                  (Web UI)
```

1. **Switch / arm to ESP32:** polls `MSP_RC` every 200 ms; watches your
   configured AUX channel (debounced, configurable threshold). Polls
   `MSP_STATUS` + `MSP_BOXIDS` for arming state.
2. **ESP32 to Camera:** desired-recording = switch ON OR (record-on-arm AND
   armed). On transitions it sends the absolute start/stop command to **every
   assigned camera**; each camera keeps its own pending command, so a camera
   that reconnects later still receives it. Manual buttons in the Web UI do
   the same.
3. **Camera to OSD:** up to four independent strings pushed on change (checked
   every 500 ms) into Betaflight Custom Messages 1-4 via `MSP2_SET_TEXT`.
4. **Web UI:** the ESP32 runs a SoftAP (default SSID `OpenSlate`, password
   `openslate`). Browse to `http://192.168.4.1`.

---

## Hardware Requirements

| Item | Notes |
|---|---|
| **ESP32-C3** board | DevKitM-1 / "Super Mini" class. BLE 5.0, NimBLE stack. |
| **or ESP32** board | Classic ESP32 / WROOM-32 (38-pin DevKitC, 4 MB flash). |
| **Flight controller** | Betaflight with Custom Message OSD elements + one free UART. |
| **Camera** | DJI Osmo Action family **or** GoPro HERO8/9/10/11/12/13. |
| Wiring | 4 wires: 5 V, GND, FC TX, FC RX. |

One of the two boards is enough. Both are supported by the same firmware; the
pin defaults are picked automatically per PlatformIO environment.

---

## Step-by-Step Setup Guide

### Step 1 - Wire the ESP32 to the flight controller

Four wires between the ESP32 and a free UART on your FC:

| Board | RX pin | TX pin | Notes |
|---|---|---|---|
| **ESP32-C3** | **GPIO5** | **GPIO6** | **Never use GPIO20/21** - those are UART0, the serial console |
| **ESP32 (classic)** | **GPIO16** | **GPIO17** | GPIO20 does not exist, GPIO21 is I2C SDA |

Wiring is identical for both boards:

| ESP32 pin | Flight Controller | Notes |
|---|---|---|
| **RX** (see table above) | UART **TX** | FC transmits → ESP32 receives |
| **TX** (see table above) | UART **RX** | ESP32 transmits → FC receives |
| **5 V** | 5 V | Or power the ESP32 from its own USB while bench-testing |
| **GND** | GND | Common ground is mandatory |

> TX goes to RX on both ends - it's a crossover, not pin-to-pin. Double-check
> 5 V tolerance: most ESP32 dev boards accept 5 V on the VIN/5V pin only.
> On the classic ESP32 avoid GPIO6-11 (flash) and GPIO0/2/12/15 (strapping).

> **C3: do not put the FC UART on GPIO20/21.** On the ESP32-C3 those are
> UART0 — the ROM/Arduino console (`CONFIG_ESP_CONSOLE_UART_NUM=0`). Mapping
> UART1 there steals the console pins, and from that moment on **every**
> `DBG()` line disappears while the firmware keeps running normally. The
> symptom is a boot log that stops dead at `BOOT: reset reason = …` even
> though the AP is up — easy to mistake for a hung board. GPIO20/21 were the
> historical defaults for this reason until they were moved to GPIO5/6.

The on-board status LED blinks the link state (GPIO8 on most C3 boards, GPIO2
on classic ESP32 boards — active HIGH there, active LOW on the C3):

| Pattern | Meaning |
|---|---|
| Slow blink (1 s) | Disconnected / scanning |
| Fast blink (200 ms) | Connecting / authenticating |
| Solid | Connected, camera standby |
| Medium blink (500 ms) | Recording desired |

### Step 2 - Configure Betaflight

1. **Ports tab:** enable **MSP** on the UART wired to the ESP32
   (115200 baud, leave everything else off for that port).
2. **OSD tab:** place **Custom Message 1-4** elements wherever you want them
   on your screen - position comes from Betaflight, *content* is pushed live
   by OpenSlate.
3. Assign an AUX channel (on your radio's mixes tab) as your record switch if
   you want switch control. You don't need to create a Betaflight *mode* for
   it - OpenSlate reads the raw RC channels.

> Remember: your Betaflight build must support **Custom Message OSD
> elements** (`MSP2_SET_TEXT`). Stock 4.x / 4.5 does not.

### Step 3 - Build & flash the firmware

```bash
# 1. Clone
git clone https://github.com/myaicloud/OpenSlate.git
cd OpenSlate

# 2. Build — pick the env that matches your board
pio run -e esp32c3     # ESP32-C3 (DevKitM-1, "Super Mini")
pio run -e esp32dev    # classic ESP32 (DevKitC / WROOM-32)

# Flight build (no USB debug output, CORE_DEBUG_LEVEL=1) — use this on the quad
pio run -e esp32c3_release     # ...or -e esp32dev_release

# 3. Flash (hold BOOT on some boards if upload doesn't start)
pio run -e esp32dev -t upload    # ...or -e esp32c3
# if the port is not detected:
pio run -e esp32dev -t upload --upload-port COM6

# 4. Watch it work
pio device monitor -b 115200
```

The boot banner prints the detected target and the active pins, e.g.
`OpenSlate v2.1 on ESP32` followed by `FC UART RX=16 TX=17`.

Any pin can be overridden without editing `config.h` (every pin there is
`#ifndef`-guarded, so the command line wins):

```bash
# classic ESP32 example: move the FC RX line
pio run -e esp32dev --project-option "build_flags=-D FC_UART_RX_PIN=32"

# C3 example: same, and remember GPIO20/21 are the console
pio run -e esp32c3 --project-option "build_flags=-D FC_UART_RX_PIN=6 -D FC_UART_TX_PIN=7"
```

Dependencies (handled by PlatformIO): `h2zero/NimBLE-Arduino @ ^1.4.1`.
Partition table switched to `min_spiffs.csv` (BLE + Wi-Fi + web UI need the
bigger app slot that both 4 MB boards provide).

**Host unit tests** (no board needed):

```bash
pio test -e native
```

The `native` environment pulls in the MinGW toolchain automatically. It
compiles the protocol codecs, name matching and slot policy against the stubs
in `test/stub/`. The real DJI/GoPro classes and camera manager are also tested
against a deterministic NimBLE fake: independent sessions/telemetry, mixed
brands, disconnect/cancellation, presence filtering and retry after range loss.
This checks software behavior, not two-camera radio reliability on hardware.

### Step 4 - Connect to the Web UI

1. Power the ESP32 (FC 5 V or USB).
2. On your phone or PC, join the Wi-Fi network **`OpenSlate`**
   (password: **`openslate`**).
3. The captive portal opens automatically on most devices; otherwise browse
   to **`http://192.168.4.1`**.
4. You land on the **Live** view with the record button and status cards.

### Step 5 - Pair your camera

1. Power the camera on and put it within a few metres of the ESP32.
2. In the Web UI open the **Camera** tab.
3. Under **Add a camera**, pick **DJI Osmo** or **GoPro** and tap **Scan** -
   a one-shot 5-second BLE scan runs and lists nearby cameras (strongest
   signal first).
   - Camera not in the list? Turn on **Show all nearby devices** and pick yours by signal strength (hold it within 1 m -
     the strongest RSSI is usually yours).
4. Tap **Pair** next to your camera. This is the only moment anything is
   written to flash - devices the radio merely *sees* are never persisted.
5. Watch the camera screen:
   - **GoPro:** the first ever connection asks for a **one-time approval
     tap** on the camera's own screen. After that, reconnection is silent.
   - **DJI:** an approve prompt may appear once - tap approve.
6. The LED goes solid and the Live view shows the camera model + battery.
   From now on the ESP32 discovers saved cameras automatically at boot and
   reconnects assigned cameras directly after a link loss.

Up to **4 cameras** can be saved; switch the active one (or remove entries)
from the **Saved cameras** card in the same tab.

**Two-camera recording control:** up to **two saved cameras**
can stay connected, including two DJI, two GoPro or a mixed pair. A short
one-second scan checks which saved cameras are advertising before assigning
free slots; absent cameras are not tried blindly. The selected camera has
priority when visible, otherwise visible entries follow registry order.
Only saved MACs are eligible; background discovery never writes NVS.
Once assigned, a camera keeps its slot across range loss and retries with
per-camera backoff (normal failures capped at 8 s; DJI pairing failures at
30 s). Connection attempts share one worker and run sequentially, not in
parallel. Selecting an offline camera does not evict a live connection.

START/STOP is sent to **every assigned camera**. Each camera keeps its own
pending command, so a stickcam that dropped out of range during the flight
receives its STOP immediately after reconnecting — no new switch edge needed.
A camera that joins while recording is desired starts recording on connect.
The dashboard's main telemetry still shows the selected primary camera;
`GET /api/status` exposes `connectedCams` (ready camera count, 0..2) and an
independent `cams[].on` value for each saved camera. The **Cam count** OSD
slot shows `CAMS <ready>/<assigned>`, e.g. `CAMS 2/2` or `CAMS 1/2` while the
stickcam is still out of range. Two-camera hardware/radio validation is still
required before flight use.

**One-button auto-bind (no Wi-Fi needed):** press the board's BOOT button
(GPIO9 on the C3, GPIO0 on the ESP32 DevKitC; override with
`-D AUTOBIND_BUTTON_PIN=n`). The LED blinks fast for ~8 s while OpenSlate
scans for devices whose name contains `dji`, `osmo`, `action`, `gopro`,
`hero` (or starts with `GP`) and saves the **nearest** one it does not know
yet (strongest signal). Press again to bind the next one (max. 4 total).
Afterwards the LED stays solid for 1.5 s if a camera was added, or goes
dark if nothing new was found. An already active
camera is never replaced; the first new camera is only activated when none
was active. Don't hold the button during power-up (BOOT = download mode).

### Step 6 - Configure the record switch

1. Open the **Switch** tab.
2. Pick your **switch channel** (CH5-16 / AUX1-12), the **ON threshold**
   (default 1500 us) and **debounce** (default 300 ms). Changes save
   immediately; the orange marker on the threshold slider shows your switch
   position live.
3. Optional: enable **Record on arm** (+ **Stop on disarm**) so recording
   follows the arming state instead of a switch.
4. Flip the switch and watch the Live view: the record-switch card should
   flip from OFF to ON, and the camera starts/stops recording.

### Step 7 - (Optional) Fine-tune OSD & Wi-Fi

- **OSD tab:** assign content (Cam status / Rec time / Battery / Link / FC
  battery / Arm state / Cam count / Off) to Custom Messages 1-4 with live
  previews. **Cam count** shows ready/assigned cameras (`CAMS 2/2`).
- **Switch tab:** change the Wi-Fi SSID/password. **Off while armed** (on by
  default) powers the hotspot down while the FC is armed and back up on
  disarm. You can also assign a spare AUX channel as a **Wi-Fi radio switch**
  - flip it low and the hotspot powers down to save ~60-100 mA (BLE camera
  control keeps running; the AP always boots ON so you can't lock yourself
  out). The hotspot is up only when neither of the two asks for it to be off.

Done - go fly.

---

## Web UI Guide

Connect a phone/PC to the OpenSlate Wi-Fi network - the captive portal
opens automatically on most devices, otherwise browse to
`http://192.168.4.1`. Five views - a tab bar at the bottom on phones, a
sidebar on wider screens. Settings save as soon as you change them; only the
Wi-Fi credentials ask for confirmation, because saving restarts the hotspot.

| Tab | What you can do |
|---|---|
| **Live** | Recording timer and one big record button (start/stop), camera battery, record-switch state, FC arm state & battery, CPU/uptime, and a goggle-style preview of the four OSD strings. |
| **Camera** | Active camera, saved-camera registry (use/forget, up to 4), brand picker + scan with **Pair**, "show all nearby devices" fallback. |
| **Switch** | Record switch channel, ON threshold (with a live marker showing where your switch is right now), debounce, **record-on-arm + stop-on-disarm**, Wi-Fi hotspot power switch and credentials. |
| **OSD** | Assign content to Custom Message slots 1-4 with live previews. |
| **System** | Betaflight identity (API/firmware/board), battery, read-only **MSP console** (hex + ASCII dump), CPU/heap/uptime/clients, **OTA firmware update** (drag & drop .bin), reboot. |

While the FC is armed, everything the firmware refuses in flight (scan, pair,
camera switching, Wi-Fi credentials, OTA, reboot) is greyed out and a banner
says why. If the page loses the connection, a banner appears until it is back.

### Navigating the Camera tab (pairing in detail)

The Camera tab has three cards:

1. **Active camera** - which camera is selected right now and its link state.
2. **Saved cameras** - your NVS registry (max 4). Tap **Use** to make an entry
   primary and prioritise its connection; forget entries you no longer own.
3. **Add a camera** - the pairing workflow:
   - Pick **DJI Osmo** or **GoPro** and press **Scan** (5-second window,
     results sorted by signal strength; the button then waits out the 15 s
     scan cooldown).
   - Tap **Pair** on your device. The ESP32 saves it to flash,
     selects it and prioritises its connection.
   - Only saved cameras auto-connect (up to two at a time); a camera that
     merely appears in a scan is never persisted.
   - If your camera doesn't match the auto-detection filters, use the
     **Show all nearby devices** toggle and identify it by signal strength.

First-connection approvals happen on the **camera's own screen** (one tap
for GoPro, sometimes one for DJI) - after that, reconnection is silent.

The UI is a single-page app embedded in the firmware (PROGMEM, ~55 KB) with
no external dependencies - it works offline in the captive portal. It follows
your system light/dark setting; the toggle in the top bar overrides it and is
remembered in your browser.

### Can it configure Betaflight from the Web UI?

A full Configurator port is not realistic on this microcontroller (the real
Configurator is a multi-megabyte desktop-class app and needs a serial/WebSocket
bridge). But the firmware already speaks MSP in both directions over the FC
UART, so a lightweight config panel - reading/writing selected settings via
MSP passthrough (think "Betaflight Lua scripts in a browser") - is absolutely
feasible as a future subtab. The System tab's MSP console already
demonstrates live MSP data flowing from your FC (read-only allowlist today).

### Updating firmware over Wi-Fi (OTA)

System tab → **Firmware update**: drop or choose a `.bin` from a GitHub
Release and the ESP32 flashes it and reboots automatically. Use a
`*_release` build for flying. Don't close the
page or power off mid-upload; if it fails, re-flash over USB.

> **Security warning:** the `/api/ota` endpoint has **no authentication and no
> signature check** — anyone connected to the OpenSlate AP can flash
> firmware. Only use it on a trusted network, and change the default AP
> Wi-Fi password in the Web UI's network settings.

---

### Flight safety lock

While Betaflight reports **ARMED**, the Web UI refuses everything that could
drop the camera link or the ESP32 itself: reboot, OTA upload, Wi-Fi
SSID/password changes (AP restart), switching the camera brand and all Camera
tab actions (scan, pair, select, remove). The API answers `409 not allowed
while FC is armed`. Manual START/STOP keeps working.

## Camera Support Notes

### DJI Osmo Action (DUML over BLE)

Service `0xFFF0`; DUML frames written without response to `0xFFF5`,
notifications on `0xFFF4`. App-level pairing (`0x07/0x45`), not OS bonding -
approve prompts on the camera screen when they appear.

```
[0x55][len_lo][(ver<<2|len_hi)][crc8][sender][receiver]
[msg_id BE][flags][cmdSet][cmdId][payload...][crc16 LE]
```

Record = cmdSet `0x02`, cmdId `0x02` (see `DjiCamera::sendStartRecord()` /
`sendStopRecord()` in `src/dji_camera.cpp`).

Battery = request `0x0D/0x02` to the battery module (receiver `0x05`) every
5 s. The Action 2 answers with a result byte followed by the same layout
newer models (Action 4+) push on their own: voltage (mV) at payload `@1`, current (mA) at `@5`,
temperature (0.1 °C) at `@17`, charge % at `@20`. Cameras that push the
frame themselves are not polled. The Action 2 does not answer the status,
storage or recording-time queries of newer models.

### Reconnect timing (bench)

Build with `-D BENCH_RECONNECT_MS=25000` (e.g. via the `PLATFORMIO_BUILD_FLAGS`
environment variable) to drop every camera link every 25 s; each reconnect
prints a `DJI: TIMING` line with the time from link loss to a ready session
and the per-step breakdown. Measured on a C3 with an Action 2 lying next to
it (worst case: the camera refuses new links for a few seconds right after a
drop): link loss → ready 3.2–12.4 s (was 7.0–36.8 s), connect → ready
1.0–1.5 s (was 2.9–5.3 s). Never ship a bench build.

### GoPro HERO8+ (Open GoPro BLE)

Official public API: service `0xFEA6`, command characteristic
`b5f90072-aa8d-11e3-9046-0002a5d5c51b`.

- Start:  `03 1A {%230%22shutter%22%3Atrue}`
- Stop:   `03 1B {%230%22shutter%22%3Afalse}`
- Keep-alive every ~3 s: `02 01 42`
- Status registration for battery % + encoding state.

First connection: put the camera into pairing mode and approve the prompt on
its screen once; the ESP32 bonds and reconnects silently afterwards.
HERO5-7 use a different legacy protocol and are not supported.

---

## Configuration

Runtime settings live in NVS and are edited from the Web UI. Compile-time
defaults are in `src/config.h`:

| Define | Default | Purpose |
|---|---|---|
| `FC_UART_RX_PIN` / `FC_UART_TX_PIN` | C3: 5/6 · ESP32: 16/17 | UART pins to the FC (C3: **never** 20/21 = UART0 console) |
| `DEFAULT_CAMERA_TYPE` | DJI | Initial camera backend |
| `DEFAULT_AUX_CHANNEL_INDEX` | 8 | RC channel used as record switch |
| `DEFAULT_RC_THRESHOLD_US` | 1500 | us above = ON |
| `DEFAULT_RC_DEBOUNCE_MS` | 300 | Switch debounce |
| `DEFAULT_RECORD_ON_ARM` | false | Auto-record on arming |
| `DEFAULT_STOP_ON_DISARM` | true | Stop when FC disarms |
| `DEFAULT_SCAN_ALL` | false | Show all BLE advertisers during discovery |
| `DEFAULT_WIFI_SWITCH_CH` | 255 (off) | AUX channel toggling the Wi-Fi AP |
| `DEFAULT_AP_OFF_ARMED` | true | Wi-Fi hotspot off while the FC is armed |
| `BLE_CONNECT_TIMEOUT_MS` / `BLE_CONNECT_SLICE_MS` | 30000 / 4000 | Length of one search attempt / minimum before it is handed to another slot |
| `BLE_SEARCH_SCAN_ITVL` / `_WINDOW` | 96 / 24 (60 / 15 ms) | Search duty while reconnecting (25 %) |
| `BLE_SETUP_ITVL_MIN` / `_MAX` | 6 / 12 (7.5–15 ms) | Connection interval during GATT setup + pairing |
| `WIFI_AP_DEFAULT_SSID` / `_PASS` | OpenSlate / openslate | Web UI hotspot |
| `DEFAULT_OSD_SLOT_1..4` | status/time/batt/link | Custom Message contents |
| `STATUS_LED_PIN` | C3: 8 · ESP32: 2 | Onboard LED (target-selected) |
| `OPENSLATE_TARGET_NAME` | auto | Printed in boot banner, derived from the target |

Pins are chosen per target in the "Board / Target Selection" block at the top of
`src/config.h`, keyed on `CONFIG_IDF_TARGET_ESP32C3` / `CONFIG_IDF_TARGET_ESP32`.
Every pin is wrapped in `#ifndef`, so a `-D` from `build_flags` overrides it.

## Repository Structure

```
OpenSlate/
+-- platformio.ini          # Two ESP32 envs (esp32c3 / esp32dev) + native tests
+-- README.md
+-- LICENSE                 # GNU GPL v3.0
+-- NOTICE                  # Copyright, GPL section 7 terms, third-party notices
+-- src/
    +-- config.h            # Pins, defaults, timings, debug switch
    +-- settings.h/.cpp     # NVS-backed runtime configuration + saved-camera
    |                       #   registry types (OpenSlateSettings)
    +-- main.cpp            # Non-blocking loop orchestration
    +-- msp_protocol.h/.cpp # MSP v1 parser + MSP v2 SET_TEXT (CRC-DVB-S2)
    +-- duml_codec.h/.cpp   # Pure DJI DUML frame encoder (host-testable)
    +-- fc_status.h/.cpp    # Arm detection, FC battery/identity polling
    +-- camera_common.h     # Shared camera types (backend interface)
    +-- camera_backend.h    # Per-connection backend interface
    +-- camera_slot_policy.h/.cpp # Pure saved/visible-camera selection + backoff
    +-- ble_build_config.h  # NimBLE connection/bond limits after SDK defaults
    +-- dji_camera.h/.cpp   # DJI Osmo DUML-over-BLE backend + discovery
    |                       #   filters (name/OUI/service/mfr-data)
    +-- gopro_camera.h/.cpp # GoPro Open BLE backend
    +-- camera_manager.h/.cpp  # Two connection slots, primary dispatch,
    |                          # saved-camera discovery and user scans
    +-- ble_recycler.h/.cpp    # NimBLEClient pool: released clients are parked,
    |                          #   settled and reused - never deleted (deleteClient
    |                          #   is a use-after-free on this path, see CR01)
    +-- scan_results.h/.cpp    # In-RAM BLE scan results collector (Web UI
    |                          #   "Discovered cameras" card, RSSI-sorted,
    |                          #   TTL eviction, max 10)
    +-- cam_registry.h/.cpp   # Two-tier camera registry: in-RAM discovered
    |                          #   list + NVS "saved" list (Pair & Save only;
    |                          #   kills the ghost-device bug; max 4 saved)
    +-- recorder.h/.cpp     # Switch/arm/manual -> record decision engine
    +-- osd_slots.h/.cpp    # Custom Message 1-4 content manager
    +-- wifiswitch.h/.cpp   # AUX-switch power control for the Wi-Fi AP
    +-- web_server.h/.cpp   # SoftAP, captive DNS, REST API, OTA endpoint
    +-- web_assets.h        # Embedded Web UI (PROGMEM)
+-- test/
    +-- stub/Arduino.h      # Host stubs so pure modules compile natively
    +-- test_duml/          # DUML codec unit tests
    +-- test_msp/           # MSP v1 parser unit tests
    +-- test_camera_slots/  # Pure visibility/priority and backoff tests
    +-- test_camera_backends/ # Real manager/backends with NimBLE/RTOS fakes
```

### REST API (used by the Web UI)

| Endpoint | Purpose |
|---|---|
| `GET /api/status` | Live telemetry snapshot (JSON) |
| `POST /api/settings` | Update + persist settings |
| `POST /api/camera` | `{"scan":true}` \| `{"pair":"MAC","type":0\|1}` \| `{"select":i}` \| `{"remove":i}` |
| `POST /api/command` | `{"cmd":"start"\|"stop"\|"reboot"}` |
| `POST /api/msp` | Read-only allowlisted MSP passthrough |
| `GET /api/scan` | Current scan results |
| `POST /api/ota` / `GET /api/ota/status` | OTA firmware update |

## Known Limitations (ESP32-C3 / ESP32)

- Wi-Fi and BLE share one radio; heavy Wi-Fi traffic can slightly delay BLE.
  Normal dashboard usage is no problem.
- On the classic ESP32 BLE and Classic-BT share the controller; the firmware
  runs BLE-only, and Arduino core 3.3+ requires an explicit BLE-memory marker
  (see `camera_manager.cpp`) or `esp_bt_controller_init()` fails.
- GoPro telemetry depth depends on model firmware (battery %, encoding state;
  record timer is counted locally while encoding).
- DJI telemetry parsing beyond link state is still experimental upstream.

## Roadmap

- [x] GoPro profile (Open GoPro BLE)
- [x] Parallel info on all four custom messages
- [x] Mobile-first Web UI with dark/light mode
- [x] Record-on-arm (+ stop-on-disarm)
- [x] Camera registry with discovery scan + Pair & Save
- [x] OTA firmware update from the Web UI
- [x] Wi-Fi power switch on a spare AUX channel
- [ ] Lightweight MSP config panel ("configurator-lite" subtab)
- [ ] Profiles , Camera configuration.
- [x] DJI battery %, voltage and temperature (Action 2, polled)
- [ ] DJI recording time and SD status straight from the camera

## License

OpenSlate is Copyright (C) 2026 FPVRookie and licensed under the
**GNU General Public License v3.0** (GPL-3.0-only) - see [LICENSE](LICENSE).

In short: you may use, study, modify and share OpenSlate, also commercially,
as long as every copy you pass on - including firmware on a device you sell -
comes with its complete source code under the same license, keeps the
copyright notices, and marks changes as yours.

Additional terms under GPL-3.0 section 7 (see [NOTICE](NOTICE)): the
attribution "OpenSlate by FPVRookie" (or "Based on OpenSlate by
FPVRookie" for modified versions) must stay visible in the web UI
footer and in the boot banner, and a modified version must not be presented
as the original OpenSlate. [NOTICE](NOTICE) also carries the notice for parts
derived from MIT-licensed code.
