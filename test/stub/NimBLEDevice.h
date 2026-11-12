// ============================================================================
// NimBLEDevice.h — Deterministic host fake for camera backend isolation tests
// ============================================================================

#ifndef NIMBLE_DEVICE_STUB_H
#define NIMBLE_DEVICE_STUB_H

#include <Arduino.h>
#include <assert.h>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#define configASSERT(condition) assert(condition)
#define BLE_HS_EALREADY 2
#define NIMBLE_MAX_CONNECTIONS 2
#define ESP_PWR_LVL_P9 9
#define BLE_HS_IO_NO_INPUT_OUTPUT 0

typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(mux) ((void)(mux))
#define portEXIT_CRITICAL(mux) ((void)(mux))

struct ble_gap_conn_desc {
    uint16_t conn_handle;
    struct { bool encrypted; } sec_state;
};

class NimBLEUUID {
public:
    std::string value;
    NimBLEUUID(const char *text) : value(text) {}
    NimBLEUUID(uint16_t number) : value(std::to_string(number)) {}
};

class NimBLEAddress {
    std::string _mac;
    uint8_t _type = 0;
public:
    NimBLEAddress() {}
    NimBLEAddress(const std::string &mac, uint8_t type = 0) : _mac(mac), _type(type) {}
    std::string toString() const { return _mac; }
    uint8_t getType() const { return _type; }
};

class NimBLEClient;
class NimBLERemoteService;
class NimBLERemoteCharacteristic {
    NimBLERemoteService *_service;
public:
    typedef std::function<void(NimBLERemoteCharacteristic*, uint8_t*, size_t, bool)> Notify;
    Notify callback;
    bool writeOk = true;
    unsigned writes = 0;
    explicit NimBLERemoteCharacteristic(NimBLERemoteService *service) : _service(service) {}
    bool canWrite() const { return true; }
    bool canWriteNoResponse() const { return true; }
    bool canNotify() const { return true; }
    bool writeValue(const uint8_t*, size_t, bool) { writes++; return writeOk; }
    bool subscribe(bool, Notify fn) { callback = fn; return true; }
    NimBLERemoteService* getRemoteService() { return _service; }
    void emit(std::vector<uint8_t> data) {
        if (callback) callback(this, data.data(), data.size(), true);
    }
};

class NimBLERemoteService {
    NimBLEClient *_client;
    std::map<std::string, std::unique_ptr<NimBLERemoteCharacteristic>> _chars;
public:
    explicit NimBLERemoteService(NimBLEClient *client) : _client(client) {}
    NimBLEClient* getClient() { return _client; }
    NimBLERemoteCharacteristic* getCharacteristic(const NimBLEUUID &uuid) {
        std::unique_ptr<NimBLERemoteCharacteristic> &ch = _chars[uuid.value];
        if (!ch) ch.reset(new NimBLERemoteCharacteristic(this));
        return ch.get();
    }
};

class NimBLEClientCallbacks {
public:
    virtual ~NimBLEClientCallbacks() {}
    virtual void onConnect(NimBLEClient*) {}
    virtual void onDisconnect(NimBLEClient*) {}
    virtual void onAuthenticationComplete(ble_gap_conn_desc*) {}
};

class NimBLEClient {
    NimBLEClientCallbacks *_callbacks = nullptr;
    bool _connected = false;
    NimBLERemoteService _service;
    uint16_t _id;
public:
    bool connectOk = true;
    NimBLEAddress peer;
    explicit NimBLEClient(uint16_t id) : _service(this), _id(id) {}
    void setClientCallbacks(NimBLEClientCallbacks *callbacks, bool) { _callbacks = callbacks; }
    void setConnectTimeout(unsigned) {}
    bool connect(const NimBLEAddress &address) {
        peer = address;
        _connected = connectOk;
        if (_connected && _callbacks) _callbacks->onConnect(this);
        return _connected;
    }
    bool isConnected() const { return _connected; }
    void disconnect() {
        _connected = false;
        if (_callbacks) _callbacks->onDisconnect(this);
    }
    uint16_t getConnId() const { return _id; }
    NimBLERemoteService* getService(const NimBLEUUID&) { return &_service; }
    NimBLERemoteCharacteristic* characteristic(const char *uuid) {
        return _service.getCharacteristic(NimBLEUUID(uuid));
    }
    void authenticate(bool encrypted = true) {
        ble_gap_conn_desc desc = {};
        desc.conn_handle = _id;
        desc.sec_state.encrypted = encrypted;
        if (_callbacks) _callbacks->onAuthenticationComplete(&desc);
    }
};

class NimBLEAdvertisedDevice {
    NimBLEAddress _address;
public:
    NimBLEAdvertisedDevice(const char *mac = "", uint8_t type = 0) : _address(std::string(mac), type) {}
    bool haveName() const { return false; }
    std::string getName() const { return ""; }
    NimBLEAddress getAddress() const { return _address; }
    uint8_t getAddressType() const { return _address.getType(); }
    int8_t getRSSI() const { return -50; }
    bool isAdvertisingService(const NimBLEUUID&) const { return false; }
    std::string getManufacturerData() const { return ""; }
};

class NimBLEAdvertisedDeviceCallbacks {
public:
    virtual ~NimBLEAdvertisedDeviceCallbacks() {}
    virtual void onResult(NimBLEAdvertisedDevice*) {}
};

class NimBLEScanResults {
public:
    int getCount() const { return 0; }
};

class NimBLEScan {
    bool _scanning = false;
    NimBLEAdvertisedDeviceCallbacks *_callbacks = nullptr;
    void (*_complete)(NimBLEScanResults) = nullptr;
public:
    bool isScanning() const { return _scanning; }
    void stop() {
        _scanning = false;
        if (_complete) _complete(NimBLEScanResults());
    }
    void emit(const char *mac, uint8_t type = 0) {
        NimBLEAdvertisedDevice device(mac, type);
        if (_scanning && _callbacks) _callbacks->onResult(&device);
    }
    void setAdvertisedDeviceCallbacks(NimBLEAdvertisedDeviceCallbacks *callbacks, bool) {
        _callbacks = callbacks;
    }
    void setActiveScan(bool) {}
    void setInterval(unsigned) {}
    void setWindow(unsigned) {}
    void setMaxResults(unsigned) {}
    bool start(unsigned, void (*complete)(NimBLEScanResults), bool) {
        _scanning = true;
        _complete = complete;
        return true;
    }
};

class NimBLEDevice {
public:
    static void init(const char*) {}
    static void setPower(int) {}
    static void setSecurityAuth(bool, bool, bool) {}
    static void setSecurityIOCap(int) {}
    static std::vector<NimBLEClient*>& clients() {
        static std::vector<NimBLEClient*> all;
        return all;
    }
    static NimBLEClient* createClient() {
        NimBLEClient *client = new NimBLEClient((uint16_t)clients().size());
        clients().push_back(client);
        return client;
    }
    static NimBLEScan* getScan() { static NimBLEScan scan; return &scan; }
    static int startSecurity(uint16_t) { return 0; }
};

#endif // NIMBLE_DEVICE_STUB_H
