// ============================================================================
// auto_bind.cpp — One-button camera auto-binding
// ============================================================================

#include "auto_bind.h"
#include "autobind_match.h"
#include "config.h"
#include "settings.h"
#include "camera_manager.h"
#include "cam_registry.h"
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <string>
#include <cstring>

static const uint8_t kMaxCandidates = 8;

struct Candidate {
    uint8_t type;
    char    mac[18];
    char    name[24];
    int8_t  rssi;
};

enum AbState { AB_IDLE, AB_SCANNING, AB_FEEDBACK };

static AbState  _state = AB_IDLE;
static uint32_t _stateSince = 0;
static bool     _lastAdded = false;

// Shared with the NimBLE host task (scan callback) — guarded by _candMux.
static Candidate _cands[kMaxCandidates];
static uint8_t   _candCount = 0;
static volatile bool _scanDone = false;
static portMUX_TYPE _candMux = portMUX_INITIALIZER_UNLOCKED;

// Button state
static bool     _armed = false;        // false until the pin was seen released
static bool     _rawLow = false;
static uint32_t _rawSince = 0;
static bool     _pressed = false;

// GoPros advertise this service; used as a brand hint when the name is silent.
static NimBLEUUID kGoProAdvService((uint16_t)0xFEA6);

// ──────────────────────────────────────────────────────────────────────────────
// BLE callbacks — host task: no NVS, no DBG, only small RAM copies.
// ──────────────────────────────────────────────────────────────────────────────

class AutoBindScanCallbacks : public NimBLEAdvertisedDeviceCallbacks {
    void onResult(NimBLEAdvertisedDevice *dev) override {
        std::string name = dev->haveName() ? dev->getName() : "";
        AutoBindBrand brand = autobindClassifyName(name.c_str());
        if (brand == AUTOBIND_NONE && dev->isAdvertisingService(kGoProAdvService)) {
            brand = AUTOBIND_GOPRO;
        }
        if (brand == AUTOBIND_NONE) return;

        std::string mac = dev->getAddress().toString();
        uint8_t type = (brand == AUTOBIND_GOPRO) ? CAMERA_GOPRO : CAMERA_DJI;

        portENTER_CRITICAL(&_candMux);
        bool dup = false;
        int8_t rssi = dev->getRSSI();
        for (uint8_t i = 0; i < _candCount; i++) {
            if (_cands[i].type == type &&
                strcasecmp(_cands[i].mac, mac.c_str()) == 0) {
                if (rssi > _cands[i].rssi) _cands[i].rssi = rssi;
                dup = true;
                break;
            }
        }
        if (!dup && _candCount < kMaxCandidates) {
            Candidate &c = _cands[_candCount++];
            c.type = type;
            c.rssi = rssi;
            strlcpy(c.mac, mac.c_str(), sizeof(c.mac));
            strlcpy(c.name, name.c_str(), sizeof(c.name));
        }
        portEXIT_CRITICAL(&_candMux);
    }
};
static AutoBindScanCallbacks _scanCb;

static void scanDoneCb(NimBLEScanResults) { _scanDone = true; }

// ──────────────────────────────────────────────────────────────────────────────
// Run control
// ──────────────────────────────────────────────────────────────────────────────

bool autoBindStart() {
    if (_state != AB_IDLE) return false;
    if (camIsOtaQuiet()) { DBG("BIND: refused — OTA owns the radio"); return false; }
    if (!camPrepareUserScan()) {
        DBG("BIND: refused — camera backend busy");
        return false;
    }
    NimBLEScan *pScan = NimBLEDevice::getScan();
    if (!pScan || pScan->isScanning()) return false;

    portENTER_CRITICAL(&_candMux);
    _candCount = 0;
    portEXIT_CRITICAL(&_candMux);
    _scanDone = false;

    pScan->setAdvertisedDeviceCallbacks(&_scanCb, false);
    pScan->setActiveScan(true);      // Names often live in the scan response
    pScan->setInterval(100);
    pScan->setWindow(40);            // 40 % duty: leaves airtime for the SoftAP
    pScan->setMaxResults(0);
    if (!pScan->start(AUTOBIND_SCAN_SECONDS, scanDoneCb, false)) {
        DBG("BIND: scan start failed");
        return false;
    }
    _state = AB_SCANNING;
    _stateSince = millis();
    DBG("BIND: scanning %d s for DJI/GoPro cameras", AUTOBIND_SCAN_SECONDS);
    return true;
}

static void finishRun() {
    Candidate local[kMaxCandidates];
    uint8_t n;
    portENTER_CRITICAL(&_candMux);
    n = _candCount;
    memcpy(local, _cands, sizeof(Candidate) * n);
    portEXIT_CRITICAL(&_candMux);

    OpenSlateSettings &s = settingsGet();
    bool hadActive = false;
    for (uint8_t i = 0; i < s.camCount; i++) if (s.cams[i].active) hadActive = true;

    // Bind only the strongest (nearest) camera that is not saved yet; every
    // further press binds the next one.
    int8_t best = -1;
    for (uint8_t i = 0; i < n; i++) {
        bool known = false;
        for (uint8_t j = 0; j < s.camCount; j++) {
            if (s.cams[j].type == local[i].type &&
                strcasecmp(s.cams[j].mac, local[i].mac) == 0) { known = true; break; }
        }
        if (known) continue;
        if (best < 0 || local[i].rssi > local[best].rssi) best = (int8_t)i;
    }

    uint8_t added = 0, skippedFull = 0;
    if (best >= 0) {
        char nameBuf[24];
        sanitizeDeviceName(nameBuf, local[best].name, sizeof(nameBuf));
        if (camRegistrySave(local[best].type, local[best].mac,
                            nameBuf[0] ? nameBuf : local[best].mac)) {
            added = 1;
            // Only take over when nothing is active; never steal a link.
            if (!hadActive) {
                camRegistrySelect((uint8_t)(s.camCount - 1));
                camKick();
            }
        } else {
            skippedFull = 1;
        }
    }
    DBG("BIND: found=%u added=%u full-skipped=%u (saved %u/%d)",
        n, added, skippedFull, s.camCount, MAX_SAVED_CAMERAS);
    _lastAdded = added > 0;
    _state = AB_FEEDBACK;
    _stateSince = millis();
}

// ──────────────────────────────────────────────────────────────────────────────
// Public API
// ──────────────────────────────────────────────────────────────────────────────

void autoBindInit() {
    pinMode(AUTOBIND_BUTTON_PIN, INPUT_PULLUP);
    _armed = false;
}

void autoBindUpdate() {
    uint32_t now = millis();

    // Debounced edge detection. Not armed until the pin was seen released, so
    // a BOOT button still held after a download-mode reset cannot trigger.
    bool low = (digitalRead(AUTOBIND_BUTTON_PIN) == LOW);
    if (low != _rawLow) { _rawLow = low; _rawSince = now; }
    if ((uint32_t)(now - _rawSince) >= AUTOBIND_DEBOUNCE_MS) {
        if (!_rawLow) {
            _armed = true;
            _pressed = false;
        } else if (_armed && !_pressed) {
            _pressed = true;
            autoBindStart();
        }
    }

    if (_state == AB_SCANNING) {
        now = millis();   // autoBindStart() above may have just set _stateSince
        // Guard: if the completion callback never fires, give up after the
        // scan window plus slack instead of staying "busy" forever.
        bool timedOut = (uint32_t)(now - _stateSince) >
                        (uint32_t)(AUTOBIND_SCAN_SECONDS + 3) * 1000UL;
        if (_scanDone || timedOut) {
            NimBLEScan *pScan = NimBLEDevice::getScan();
            if (pScan && pScan->isScanning()) pScan->stop();
            finishRun();
        }
    } else if (_state == AB_FEEDBACK &&
               (uint32_t)(now - _stateSince) >= AUTOBIND_FEEDBACK_MS) {
        _state = AB_IDLE;
    }
}

AutoBindLed autoBindLedMode() {
    switch (_state) {
        case AB_SCANNING: return AUTOBIND_LED_FAST;
        case AB_FEEDBACK: return _lastAdded ? AUTOBIND_LED_OK : AUTOBIND_LED_NOTHING;
        default:          return AUTOBIND_LED_NONE;
    }
}
