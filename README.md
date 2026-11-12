<h1 align="center">OpenSlate</h1>

<p align="center">
  <b>Your arm switch is your record button.</b><br>
  Start and stop your DJI Osmo Action or GoPro from your FPV radio,<br>
  and see camera status right in your goggles.
</p>

<p align="center">
  <img alt="License: GPL-3.0" src="https://img.shields.io/badge/license-GPL--3.0-blue">
  <img alt="ESP32-C3 and ESP32" src="https://img.shields.io/badge/board-ESP32--C3%20%7C%20ESP32-orange">
  <img alt="Betaflight" src="https://img.shields.io/badge/Betaflight-MSP%20%2B%20OSD-informational">
  <img alt="Cameras" src="https://img.shields.io/badge/cameras-DJI%20Osmo%20Action%20%7C%20GoPro-success">
</p>

---

OpenSlate is a tiny ESP32 board that sits next to your flight controller. It
reads your sticks and arm state from Betaflight and controls your action
camera over Bluetooth. Flip a switch, or simply arm, and the camera records.
Disarm, and it stops. Camera status, recording time and battery show up in
your OSD, and a built-in web app on your phone lets you set it all up.

No soldering to the camera, no cables to the camera, no app on your phone.

## Why OpenSlate

| | |
|---|---|
| **Never miss a flight** | Record on arm, stop on disarm. Or use any AUX switch. |
| **Know it is recording** | `REC 85% 02:41` right in your OSD, plus camera battery and link state. |
| **Two cameras at once** | Copter cam and a stickcam (or a second angle) start and stop together. |
| **Fast reconnect** | A stickcam out of range during the flight reconnects within seconds after landing and gets its STOP command right away. |
| **Set up from your phone** | Join the OpenSlate Wi-Fi, the setup page opens by itself. No laptop, no internet. |
| **Flight-safe** | Pairing, Wi-Fi changes and updates are locked while armed. Wi-Fi switches off in flight so the radio belongs to your cameras. |
| **Open source** | GPL-3.0, cheap off-the-shelf hardware, update over Wi-Fi. |

## What you need

| | |
|---|---|
| **Board** | An **ESP32-C3** ("Super Mini" / DevKitM-1) or a classic **ESP32** (DevKitC / WROOM-32, 4 MB flash) |
| **Flight controller** | Betaflight with **Custom Message OSD elements** and one free UART |
| **Camera** | **DJI Osmo Action** or **GoPro HERO8 – HERO13** |
| **Wiring** | 4 wires: 5 V, GND, TX, RX |
| **To flash once** | A computer with [PlatformIO](https://platformio.org/) and a USB cable |

> [!IMPORTANT]
> Your Betaflight version needs **Custom Message OSD elements**
> (`MSP2_SET_TEXT`). Betaflight 4.x and 4.5 do not have them.

## Quick start

### 1. Wire it

Connect the board to a free UART on your flight controller. TX goes to RX,
RX goes to TX.

| Board | ESP32 RX ← FC TX | ESP32 TX → FC RX | Power |
|---|---|---|---|
| **ESP32-C3** | GPIO5 | GPIO6 | 5 V + GND |
| **ESP32** | GPIO16 | GPIO17 | 5 V + GND |

> [!WARNING]
> On the ESP32-C3, never use GPIO20/21 for the flight controller. Those pins
> are the USB console, and you would lose all log output.

### 2. Set up Betaflight

1. **Ports:** turn on **MSP** for that UART (115200 baud).
2. **OSD:** place **Custom Message 1–4** where you want them. OpenSlate fills
   in the text.
3. **Radio:** if you want a record switch, put it on any AUX channel. No
   Betaflight mode needed.

### 3. Flash the firmware

```bash
git clone https://github.com/myaicloud/OpenSlate.git
cd OpenSlate
pio run -e esp32c3_release -t upload     # ESP32-C3
pio run -e esp32dev_release -t upload    # classic ESP32
```

Later updates work over Wi-Fi from the web app (**System → Firmware update**).

### 4. Pair your camera

1. Power everything up and join the Wi-Fi **`OpenSlate`**
   (password **`openslate`**). The setup page opens by itself. If not, go to
   **`http://192.168.4.1`**.
2. Open **Camera**, pick **DJI Osmo** or **GoPro**, tap **Scan**.
3. Tap **Pair** next to your camera. Approve the prompt on the camera screen
   if one appears. That is needed only once.

Tip: no Wi-Fi at hand? Press the **BOOT** button on the board. OpenSlate pairs
the nearest camera it does not know yet.

### 5. Choose how to record

Open **Switch** and either pick your **record switch channel** or turn on
**Record on arm**. Flip the switch or arm, and watch the record button in the
**Live** view light up.

**Done. Go fly.** For the flight itself, change the Wi-Fi password in
**Switch → Wi-Fi hotspot**.

## What you see in your OSD

Each of the four Custom Message elements shows what you choose in the
**OSD** tab:

| Choice | Example |
|---|---|
| Camera status | `REC 85% 02:41` · `STBY 85%` · `CAM SCAN` |
| Recording time | `REC 02:41` |
| Camera battery | `BAT 85%` |
| Link state | `LINK READY` · `LINK SCAN` · `LINK OFF` |
| FC battery | `FC 15.8V` |
| Arm state | `ARMED` · `DISARMED` |
| Camera count | `CAMS 2/2` (ready / assigned) |

## Supported cameras

| Camera | Start / stop | Battery in OSD | Status |
|---|---|---|---|
| DJI Osmo Action 2 | ✅ | ✅ % (plus voltage and temperature in the web app) | tested |
| DJI Osmo Action 3 / 4 / 5 Pro / 6 | should work | should work | same protocol family, not tested yet: reports welcome |
| GoPro HERO8 – HERO13 | ✅ | ✅ | official Open GoPro Bluetooth API |
| GoPro HERO5 – HERO7 | ❌ | ❌ | older protocol, not supported |

Up to **4 cameras** can be saved, and **2** stay connected at the same time,
in any mix of DJI and GoPro.

## The web app

Join the **OpenSlate** Wi-Fi with your phone. Everything saves as you change
it.

| Tab | What you do there |
|---|---|
| **Live** | Big record button, recording timer, camera battery, switch and arm state, OSD preview |
| **Camera** | Pair, switch and forget cameras |
| **Switch** | Record channel, threshold with a live marker of your switch, record on arm, Wi-Fi settings |
| **OSD** | Choose what the four Custom Messages show |
| **System** | Flight controller info, MSP console, firmware update over Wi-Fi, reboot |

Works offline, follows your phone's light or dark mode, and greys out
everything that is unsafe while the quad is armed.

## Troubleshooting

<details>
<summary><b>My camera does not show up when I scan</b></summary>

Hold it within about 1 m of the board and make sure it is switched on. In
**Camera**, turn on **Show all nearby devices** and pick the entry with the
strongest signal.
</details>

<details>
<summary><b>The OSD stays empty</b></summary>

Check that MSP is enabled on the right UART (115200 baud), that TX and RX are
crossed, and that your Betaflight version has Custom Message OSD elements. The
**System** tab shows whether OpenSlate talks to your flight controller.
</details>

<details>
<summary><b>I cannot find the OpenSlate Wi-Fi</b></summary>

By default the hotspot is off while the quad is armed. Disarm, and it comes
back after a few seconds. If you set up a Wi-Fi switch channel, flip it on.
</details>

<details>
<summary><b>What does the LED mean?</b></summary>

| LED | Meaning |
|---|---|
| Slow blink (1 s) | No camera / searching |
| Fast blink | Connecting or pairing |
| Solid | Camera connected, standby |
| Medium blink (0.5 s) | Recording |

</details>

## How it works

```
 Radio ──> Flight controller <── MSP (UART) ──> OpenSlate (ESP32) ── Bluetooth ──> Camera(s)
              (Betaflight)                          │
                    ^                                └── Wi-Fi hotspot ──> your phone
                    └──── OSD text (Custom Messages 1-4) ────┘
```

OpenSlate polls your RC channels and arm state over MSP, decides whether you
want to record, and sends an absolute start or stop to every assigned camera.
Commands are never toggles, so repeating one after a reconnect is always safe.

---

## For developers

<details>
<summary><b>Build variants and pin overrides</b></summary>

| Environment | Use |
|---|---|
| `esp32c3_release` / `esp32dev_release` | For flying: no serial debug output |
| `esp32c3` / `esp32dev` | Bench builds with full serial logging |
| `native` | Host unit tests (`pio test -e native`) |

Every pin in `src/config.h` is `#ifndef`-guarded, so a build flag overrides it:

```bash
pio run -e esp32dev --project-option "build_flags=-D FC_UART_RX_PIN=32"
```

The BOOT button pin for one-button pairing is `AUTOBIND_BUTTON_PIN`
(GPIO9 on the C3, GPIO0 on the ESP32). Watch the boot log with
`pio device monitor -b 115200`.
</details>

<details>
<summary><b>Default settings (<code>src/config.h</code>)</b></summary>

All of these can be changed in the web app; the values below are only the
defaults on first boot.

| Define | Default | Purpose |
|---|---|---|
| `FC_UART_RX_PIN` / `FC_UART_TX_PIN` | C3: 5/6 · ESP32: 16/17 | UART pins to the flight controller |
| `DEFAULT_AUX_CHANNEL_INDEX` | 8 | Record switch channel |
| `DEFAULT_RC_THRESHOLD_US` | 1500 | Above this value the switch is ON |
| `DEFAULT_RC_DEBOUNCE_MS` | 300 | Switch debounce |
| `DEFAULT_RECORD_ON_ARM` | false | Start recording when armed |
| `DEFAULT_STOP_ON_DISARM` | true | Stop recording when disarmed |
| `DEFAULT_AP_OFF_ARMED` | true | Wi-Fi hotspot off while armed |
| `DEFAULT_WIFI_SWITCH_CH` | 255 (off) | AUX channel that switches the hotspot |
| `WIFI_AP_DEFAULT_SSID` / `_PASS` | OpenSlate / openslate | Hotspot name and password |
| `BLE_CONNECT_TIMEOUT_MS` / `BLE_CONNECT_SLICE_MS` | 30000 / 4000 | One reconnect search / slice before it is handed to the other camera |
| `BLE_SEARCH_SCAN_ITVL` / `_WINDOW` | 96 / 24 | Search duty while reconnecting (25 %) |
| `BLE_SETUP_ITVL_MIN` / `_MAX` | 6 / 12 | Fast connection interval during setup (7.5–15 ms) |

</details>

<details>
<summary><b>Two cameras and reconnect behaviour</b></summary>

- Up to two saved cameras stay connected, in any brand mix. A camera keeps its
  slot while it is out of range.
- A missing camera is searched for continuously at low radio duty and is
  caught as soon as it advertises again. Setup after the link comes up takes
  about 1–1.5 s.
- One Bluetooth worker serves both cameras; a search hands it over every 4 s,
  so a missing stickcam never delays the copter camera.
- START and STOP go to every assigned camera. A camera that was away gets the
  pending command the moment it is back.
- Building with `-D BENCH_RECONNECT_MS=25000` drops all camera links every
  25 s and prints a `DJI: TIMING` line per reconnect. Bench use only.

Two-camera behaviour is covered by host tests; validate it on your own hardware
before relying on it in flight.
</details>

<details>
<summary><b>Camera protocols</b></summary>

**DJI Osmo Action (DUML over Bluetooth LE).** Service `0xFFF0`; frames are
written to `0xFFF5`, notifications arrive on `0xFFF4`. Pairing is app-level
(`0x07/0x45`), approved once on the camera.

```
[0x55][len_lo][(ver<<2|len_hi)][crc8][sender][receiver]
[msg_id BE][flags][cmdSet][cmdId][payload...][crc16 LE]
```

Recording is `0x02/0x02` (payload `01` start, `00` stop). Battery is requested
with `0x0D/0x02` from the battery module (`0x05`) every 5 s: voltage (mV) at
payload `@1`, current (mA) at `@5`, temperature (0.1 °C) at `@17`, charge % at
`@20`, after a leading result byte. Cameras that push this frame on their own
are not polled.

**GoPro HERO8+ (Open GoPro BLE).** Service `0xFEA6`, command characteristic
`b5f90072-aa8d-11e3-9046-0002a5d5c51b`. Start `03 1A {%230%22shutter%22%3Atrue}`,
stop `03 1B {%230%22shutter%22%3Afalse}`, keep-alive `02 01 42` every ~3 s,
status registration for battery and encoding state.
</details>

<details>
<summary><b>REST API</b></summary>

| Endpoint | Purpose |
|---|---|
| `GET /api/status` | Live status (JSON) |
| `POST /api/settings` | Change and save settings |
| `POST /api/camera` | `{"scan":true}` · `{"pair":{"mac":"…","type":0\|1}}` · `{"select":i}` · `{"remove":i}` |
| `POST /api/command` | `{"cmd":"start"\|"stop"\|"reboot"}` |
| `POST /api/msp` | Read-only MSP passthrough (allowlisted commands) |
| `GET /api/scan` | Current scan results |
| `POST /api/ota` · `GET /api/ota/status` | Firmware update |

While the flight controller reports ARMED, every action that could drop a
camera link or the board itself answers `409 not allowed while FC is armed`.
Manual start and stop keep working.
</details>

<details>
<summary><b>Repository layout</b></summary>

```
OpenSlate/
├── platformio.ini          ESP32-C3 / ESP32 builds, release builds, host tests
├── LICENSE, NOTICE         GPL-3.0 and attribution terms
├── src/
│   ├── main.cpp            Main loop
│   ├── config.h            Pins, defaults, timings
│   ├── settings.*          Settings and saved cameras (flash)
│   ├── msp_protocol.*      MSP v1 parser, MSP v2 SET_TEXT
│   ├── fc_status.*         Arm state, FC battery and identity
│   ├── recorder.*          Switch / arm / manual → record decision
│   ├── osd_slots.*         Custom Message 1–4 content
│   ├── camera_manager.*    Two camera slots, scans, command dispatch
│   ├── ble_link.*          Reconnect search and connection tuning
│   ├── dji_camera.*        DJI Osmo Action backend
│   ├── duml_codec.*        DJI frame encoder
│   ├── gopro_camera.*      GoPro backend
│   ├── cam_registry.*      Saved-camera registry
│   ├── wifiswitch.*        Hotspot on/off (switch, armed)
│   ├── web_server.*        Hotspot, setup page, REST API, OTA
│   └── web_assets.h        The web app
└── test/                   Host unit tests (DUML, MSP, slots, backends)
```
</details>

## Known limitations

- Wi-Fi and Bluetooth share one radio. Normal use of the web app is fine;
  that is also why the hotspot switches off in flight.
- The firmware update over Wi-Fi has no password or signature check. Change
  the hotspot password and only update on a network you trust.
- GoPro recording time is counted by OpenSlate from the moment the camera
  confirms recording.
- On the DJI Osmo Action 2, recording time and SD card status are not
  available from the camera yet; the timer is counted by OpenSlate.

## Roadmap

- [x] DJI Osmo Action and GoPro HERO8+
- [x] Record on arm, stop on disarm
- [x] All four Custom Messages in parallel
- [x] Two cameras at once with fast reconnect
- [x] Mobile web app, firmware update over Wi-Fi
- [x] DJI battery in the OSD
- [ ] Recording time and SD status straight from DJI cameras
- [ ] Camera settings (resolution, mode) from the web app
- [ ] Lightweight Betaflight settings panel

## License

OpenSlate is Copyright (C) 2026 FPVRookie and licensed under the
**GNU General Public License v3.0** (GPL-3.0-only), see [LICENSE](LICENSE).

You may use, study, modify and share OpenSlate, also commercially, as long as
every copy you pass on, including firmware on a device you sell, comes with
its complete source code under the same license, keeps the copyright notices
and marks changes as yours.

Additional terms under GPL-3.0 section 7 (see [NOTICE](NOTICE)): the
attribution "OpenSlate by FPVRookie" (or "Based on OpenSlate by FPVRookie" for
modified versions) must stay visible in the web app footer and in the boot
banner, and a modified version must not be presented as the original
OpenSlate. [NOTICE](NOTICE) also carries the notice for parts derived from
MIT-licensed code.

> Not affiliated with or endorsed by DJI or GoPro. All protocols are based on
> public documentation and our own reverse engineering.
