// ============================================================================
// config.h — Project-wide configuration, pin definitions, and tunables
// ============================================================================
// ESP32 OpenSlate: Betaflight ↔ DJI Osmo / GoPro camera bridge.
//
// Runtime-changeable options (camera brand, switch channel, record-on-arm,
// OSD slot contents, Wi-Fi credentials) live in settings.h / NVS and are
// edited from the built-in Web UI.  Only low-level defaults belong here.
// ============================================================================

#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// Firmware version string (shown in UI and OTA status endpoint)
#define FIRMWARE_VERSION "v2.2"

// ──────────────────────────────────────────────────────────────────────────────
// Board / Target Selection
// ──────────────────────────────────────────────────────────────────────────────
// The firmware supports two ESP32 targets from one tree. Everything else in
// this project is target-agnostic, so only the pin defaults below differ.
//
//   ESP32-C3 (RISC-V)      : GPIO 0-21, any GPIO on UART1, native USB-Serial-JTAG
//   ESP32 "classic"/WROOM  : GPIO 0-39, UART1 is input-only by default
//
// CONFIG_IDF_TARGET_ESP32C3 comes from sdkconfig.h (included via Arduino.h), so
// the correct block is picked automatically for each PlatformIO env. A pin may
// always be overridden from the command line / extra build_flags.
//
// OPENSLATE_HOST_TEST is set by [env:native] — the PC unit tests use a stub
// Arduino.h without sdkconfig.h, so they need an explicit branch.
//
// Every pin below is wrapped in #ifndef so the documented
//
//     build_flags = -D FC_UART_RX_PIN=6 -D FC_UART_TX_PIN=7
//
// override actually works. Without the guard the #define here silently wins
// over the command-line value (only a -Wmacro-redefined warning is emitted).
// ──────────────────────────────────────────────────────────────────────────────

#if defined(OPENSLATE_HOST_TEST)
    // Host build: no pins exist, only the protocol logic is compiled.
    #ifndef OPENSLATE_TARGET_NAME
    #define OPENSLATE_TARGET_NAME  "host"
    #endif
    #ifndef FC_UART_RX_PIN
    #define FC_UART_RX_PIN        20
    #endif
    #ifndef FC_UART_TX_PIN
    #define FC_UART_TX_PIN        21
    #endif
    #ifndef STATUS_LED_PIN
    #define STATUS_LED_PIN        8
    #endif
    #ifndef LED_ACTIVE_LOW
    #define LED_ACTIVE_LOW        1
    #endif
    #ifndef AUTOBIND_BUTTON_PIN
    #define AUTOBIND_BUTTON_PIN   9
    #endif
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
    #ifndef OPENSLATE_TARGET_NAME
    #define OPENSLATE_TARGET_NAME  "ESP32-C3"
    #endif
    // GPIO20/21 are UART0 — the ROM/Arduino console. Mapping UART1 there
    // steals the pins from the debug console, so every DBG() line after
    // mspInit() disappears (the board keeps running, only logging dies).
    // GPIO4/5 are free on DevKitM-1 and on the C3 SuperMini boards, so the
    // FC UART goes there instead and the console stays alive.
    // Connect FC TX → ESP32 RX_PIN, FC RX → ESP32 TX_PIN.
    #ifndef FC_UART_RX_PIN
    #define FC_UART_RX_PIN        5    // GPIO5 — receives data FROM the FC
    #endif
    #ifndef FC_UART_TX_PIN
    #define FC_UART_TX_PIN        6    // GPIO6 — sends data TO the FC
    #endif
    // Built-in LED on most C3 dev boards is GPIO8, active LOW on DevKitM-1.
    #ifndef STATUS_LED_PIN
    #define STATUS_LED_PIN        8
    #endif
    #ifndef LED_ACTIVE_LOW
    #define LED_ACTIVE_LOW        1   // 1 = active LOW, 0 = active HIGH
    #endif
    // BOOT button (GPIO9, strapping pin: held LOW at reset = download mode,
    // a normal input afterwards). Press = auto-bind action cams.
    #ifndef AUTOBIND_BUTTON_PIN
    #define AUTOBIND_BUTTON_PIN   9
    #endif
#elif defined(CONFIG_IDF_TARGET_ESP32)
    #ifndef OPENSLATE_TARGET_NAME
    #define OPENSLATE_TARGET_NAME  "ESP32"
    #endif
    // ESP32 DevKitC / WROOM-32: GPIO20 does not exist and GPIO21 is the usual
    // I2C SDA. GPIO16/17 are free on every 30/38-pin dev board and are plain
    // bidirectional GPIOs, so they are the safe default for the FC UART.
    // GPIO32/33 are an equally valid alternative (no strapping function).
    #ifndef FC_UART_RX_PIN
    #define FC_UART_RX_PIN        16   // GPIO16 — receives data FROM the FC
    #endif
    #ifndef FC_UART_TX_PIN
    #define FC_UART_TX_PIN        17   // GPIO17 — sends data TO the FC
    #endif
    // GPIO8 is a flash pin on the classic ESP32 and must not be driven.
    // GPIO2 is the on-board LED on essentially all DevKitC / DevKit V1 boards.
    #ifndef STATUS_LED_PIN
    #define STATUS_LED_PIN        2
    #endif
    #ifndef LED_ACTIVE_LOW
    #define LED_ACTIVE_LOW        0   // 1 = active LOW, 0 = active HIGH
    #endif
    // BOOT button on DevKitC (GPIO0, strapping pin, normal input after reset).
    #ifndef AUTOBIND_BUTTON_PIN
    #define AUTOBIND_BUTTON_PIN   0
    #endif
#else
    #error "Unsupported target: only ESP32 (classic) and ESP32-C3 are wired up in config.h"
#endif

// Auto-bind button (active LOW, internal pull-up): one BLE scan of
// AUTOBIND_SCAN_SECONDS adds every unknown DJI/GoPro camera to the registry.
#define AUTOBIND_DEBOUNCE_MS      50
#define AUTOBIND_SCAN_SECONDS     8
#define AUTOBIND_FEEDBACK_MS      1500   // Result LED hold after the scan
#define AUTOBIND_LED_BLINK_MS     100    // Fast blink while auto-binding

// Hardware UART for Serial1 (connected to Betaflight FC).
// UART baud rate — must match Betaflight serial port config.
// Betaflight default MSP baud is 115200.
#define FC_UART_BAUD          115200

// ──────────────────────────────────────────────────────────────────────────────
// Default User Settings (editable at runtime via Web UI / stored in NVS)
// ──────────────────────────────────────────────────────────────────────────────

// Active camera backend: 0 = DJI Osmo Action, 1 = GoPro HERO8 and newer.
// NOTE: kept as a plain integer (not CAMERA_DJI) on purpose — settings.h
// defines CameraType and includes THIS file, so referencing the enum here
// would create a config.h <-> settings.h include cycle.
#define DEFAULT_CAMERA_TYPE       0

// Zero-based index of the RC channel used as the "Record" switch.
// In Betaflight, AUX1 = channel 5 (index 4), AUX4 = channel 8 (index 7), etc.
#define DEFAULT_AUX_CHANNEL_INDEX 8    // AUX5 — change to match your setup

// Threshold (µs) above which the switch is considered ON (record).
#define DEFAULT_RC_THRESHOLD_US   1500

// Debounce duration in milliseconds for the RC switch.
#define DEFAULT_RC_DEBOUNCE_MS    300

// Automatically start recording when the flight controller arms.
#define DEFAULT_RECORD_ON_ARM     false

// When record-on-arm is active, stop recording when the FC disarms.
#define DEFAULT_STOP_ON_DISARM    true

// "Show all nearby devices" in the Camera tab.  When false, the BLE
// discovery filter accepts only known DJI Osmo / GoPro signatures
// (rename-proof: MAC OUI, advertised service UUID, mfr data).  When true,
// every advertiser with a valid address is shown so the user can identify
// their camera manually.  Either way the saved-camera registry is only
// written when the user explicitly taps "Pair & Save".
#define DEFAULT_SCAN_ALL          false

// RC channel used as a Wi-Fi on/off switch. 255 = disabled (AP always on).
#define DEFAULT_WIFI_SWITCH_CH    255

// Power the Wi-Fi hotspot down while the FC is armed and back up on disarm.
// Frees the shared 2.4 GHz radio for the camera links in flight (and saves
// ~60-100 mA); nobody uses the Web UI while flying.
#define DEFAULT_AP_OFF_ARMED      true

// SoftAP credentials for the Web UI.
// SECURITY NOTE: these are public defaults (also committed to git). They are
// only a first-boot fallback — change them from the Web UI on first use.
// Empty password = open network (strongly discouraged; the firmware rejects
// empty passwords on POST and only keeps this path for recovery).
#define WIFI_AP_DEFAULT_SSID      "OpenSlate"
#define WIFI_AP_DEFAULT_PASS      "openslate"

// NVS namespace for settings + saved cameras (max. 15 chars). Changing it
// starts every device with defaults and an empty camera list.
#define NVS_NAMESPACE             "openslate"

// WPA2-PSK limits enforced by the firmware (not just the UI hint).
#define WIFI_AP_PASS_MIN_LEN      8
#define WIFI_AP_PASS_MAX_LEN      63   // WPA2 limit (64 = hex key, unsupported)
#define WIFI_AP_SSID_MAX_LEN      32

// HTTP hardening: max JSON body accepted on POST /api/* (bytes).
// Anything larger is rejected with 413 before heap-hungry String copies.
#define WEB_MAX_JSON_BODY_LEN     2048
// Minimum gap between two user discovery scans (ms) — enforced server-side
// so a script can't hog the shared 2.4 GHz radio. UI cooldown is longer.
#define WEB_SCAN_COOLDOWN_MS      15000

// Default content of Betaflight Custom Message slots 1..4 (OsdSlotContent).
// Fresh installs get the compact two-line layout: all-camera summary plus the
// alert line that stays empty while everything is fine.
#define DEFAULT_OSD_SLOT_1        OSD_SLOT_SUMMARY
#define DEFAULT_OSD_SLOT_2        OSD_SLOT_ALERTS
#define DEFAULT_OSD_SLOT_3        OSD_SLOT_OFF
#define DEFAULT_OSD_SLOT_4        OSD_SLOT_OFF

// Maximum characters per custom message (Betaflight MAX_NAME_LENGTH).
#define OSD_MAX_TEXT_LEN          16

// ──────────────────────────────────────────────────────────────────────────────
// MSP Timing
// ──────────────────────────────────────────────────────────────────────────────

// How often to poll the FC for RC channel data (milliseconds).
#define MSP_RC_POLL_INTERVAL_MS     200

// How often to poll MSP_STATUS for arming state (milliseconds).
#define MSP_STATUS_POLL_INTERVAL_MS 250

// How often to poll MSP_ANALOG for battery voltage (milliseconds).
#define MSP_ANALOG_POLL_INTERVAL_MS 1000

// How often to poll MSP_BOXIDS (ARM bit index can change with config).
#define MSP_BOXIDS_POLL_INTERVAL_MS 5000

// Timeout waiting for an MSP response (milliseconds).
#define MSP_RESPONSE_TIMEOUT_MS     500

// ──────────────────────────────────────────────────────────────────────────────
// OSD Configuration
// ──────────────────────────────────────────────────────────────────────────────

// How often to push custom-message text to the FC (milliseconds).
#define OSD_UPDATE_INTERVAL_MS      500

// Alert line: each message stays this long before the next one is shown.
#define OSD_ALERT_ROTATE_MS         1500
// Alert line: "ALL STOPPED" is shown this long after recording ends.
#define OSD_ALL_STOPPED_MS          5000
// Alert line: camera battery at or below this percentage raises "<TAG> BAT n".
#define OSD_LOW_CAM_BATT_PCT        15

// Text types for MSP2_SET_TEXT (0x3007) — verified against Betaflight master:
//   1 = PILOT_NAME   2 = CRAFT_NAME   3 = PID_PROFILE   4 = RATE_PROFILE
//   7..10 = CUSTOM_MSG_0..3  (i.e. "Custom Message 1..4" in the OSD tab)
#define MSP2TEXT_CUSTOM_MSG_0   7

// ──────────────────────────────────────────────────────────────────────────────
// BLE Configuration (DJI)
// ──────────────────────────────────────────────────────────────────────────────

// Per-slot retry base and failure cap (milliseconds). A previously connected
// flight camera keeps its slot across range loss. These only apply when the
// camera answered but the link failed (right after a drop it rejects with 0x3E
// for a few seconds), so the cap stays short to catch it as soon as it is
// ready. A search that simply found nothing, or was cancelled to hand the
// worker over, retries after BLE_RECONNECT_FAST_MS (see ble_link.h).
// DJI pairing failures have a separate backoff capped at 30 s.
#define BLE_RECONNECT_INTERVAL_MS 500
#define BLE_RECONNECT_MAX_MS      2000
#define BLE_RECONNECT_FAST_MS     100
#define CAMERA_CONNECTION_SLOTS   2
#define BLE_KNOWN_SCAN_SECONDS    1

// Keep-alive ping interval to the DJI camera (milliseconds).
#define BLE_KEEPALIVE_INTERVAL_MS 15000

// DJI link-liveness watchdog: the camera pushes DUML telemetry constantly;
// if no notification arrives within this window the link is considered
// wedged and a reconnect is forced (milliseconds).
#define DJI_LINK_STALE_MS         15000
// DJI battery poll interval. The Action 2 never pushes battery data, so it is
// requested (0x0D/0x02 -> battery module) this often; cameras that push it
// themselves (Action 4+) stop being polled once the first push arrives.
#define DJI_BATTERY_POLL_MS       5000

// Length of one connect (search) attempt (milliseconds, whole seconds). Long
// on purpose: the controller keeps listening at low duty and connects the
// moment the camera advertises. Cancelled early when the worker is needed.
#define BLE_CONNECT_TIMEOUT_MS    30000
// Minimum time a search attempt keeps the worker before another slot or a
// known-camera scan may cancel it (milliseconds). Two absent cameras thus
// alternate in slices of this length.
#define BLE_CONNECT_SLICE_MS      4000
// Search duty while connecting, 0.625 ms units: listen 15 ms every 60 ms (25 %).
// Leaves airtime for the copter camera link (and Wi-Fi) during a flight-long
// search; a camera advertising every <=100 ms is still caught within ~0.5 s.
#define BLE_SEARCH_SCAN_ITVL      96
#define BLE_SEARCH_SCAN_WINDOW    24
// Connection interval while GATT discovery and pairing run (1.25 ms units):
// 7.5-15 ms instead of the 50 ms the camera asks for makes each round trip
// about 4x faster. The camera's own update request is refused until the
// session is up, then the link is relaxed to the run interval below.
#define BLE_SETUP_ITVL_MIN        6
#define BLE_SETUP_ITVL_MAX        12
// Relaxed interval once the session is up (1.25 ms units): 45-60 ms.
#define BLE_RUN_ITVL_MIN          36
#define BLE_RUN_ITVL_MAX          48
// Supervision timeout (10 ms units): link loss is detected after 3 s.
#define BLE_SUPERVISION_TIMEOUT   300

// Grace period before a released NimBLEClient is handed out again
// (milliseconds). NimBLE dispatches BLE_GAP_EVENT_DISCONNECT asynchronously;
// reusing the object before that event has been consumed makes the stale
// handler run against a client that is already connecting elsewhere. See
// ble_recycler.h — the client is never deleted, only parked this long.
#define BLE_CLIENT_SETTLE_MS      2000

// GoPro keep-alive interval — Open GoPro spec recommends every ~3 s.
#define GOPRO_KEEPALIVE_INTERVAL_MS 3000

// GoPro status re-query fallback (notifications usually keep values fresh).
#define GOPRO_STATUS_POLL_MS      10000

// ──────────────────────────────────────────────────────────────────────────────
// Diagnostics
// ──────────────────────────────────────────────────────────────────────────────

// How often to sample CPU utilisation for the Web UI (milliseconds).
// A sample costs one uxTaskGetSystemState() over a small fixed array, so it is
// throttled rather than run every tick. 1 s would be fine too; 2 s keeps the
// figure readable without adding load to the shared radio/core budget.
#define CPU_SAMPLE_INTERVAL_MS    2000

// Deferred BLE log ring depth is fixed in ble_log.cpp (8 slots) — the ring is
// drained every loop() tick, so it only has to absorb a notification burst.

// ──────────────────────────────────────────────────────────────────────────────
// Status LED (optional, for visual feedback)
// ──────────────────────────────────────────────────────────────────────────────
// STATUS_LED_PIN / LED_ACTIVE_LOW are target-specific and set in the board
// selection block at the top of this file. LED_ACTIVE_LOW is deliberately an
// int (not bool) so it stays usable in #if and in digitalWrite ternaries.

// ──────────────────────────────────────────────────────────────────────────────
// Debug
// ──────────────────────────────────────────────────────────────────────────────

// Verbose serial debug output on Serial (USB CDC).
// Release guidance: build with -DDEBUG_DISABLED (or comment this out) to
// silence MAC/SSID/DUML hex logs. CORE_DEBUG_LEVEL in platformio.ini should
// be 1 (warnings) for flight builds, 3 only on the bench.
#ifndef DEBUG_DISABLED
#define DEBUG_ENABLED
#endif

#ifdef DEBUG_ENABLED
    #define DBG(fmt, ...)   log_printf("[%lu] " fmt "\n", millis(), ##__VA_ARGS__)
#else
    #define DBG(fmt, ...)   ((void)0)
#endif

#endif // CONFIG_H
