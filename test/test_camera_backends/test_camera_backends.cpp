// ============================================================================
// test_camera_backends.cpp — Real backend instances, deterministic BLE events
// ============================================================================

#include <unity.h>
#include <NimBLEDevice.h>
#include "dji_camera.h"
#include "gopro_camera.h"
#include "ble_worker.h"
#include "ble_log.h"
#include "settings.h"
#include "camera_manager.h"

static BleWorkFn pending = nullptr;
static void *pendingContext = nullptr;
static std::vector<NimBLEClient*> recycled;
static OpenSlateSettings settings = {};
static const char *kDjiAuth = "0000fff4-0000-1000-8000-00805f9b34fb";
static const char *kDjiControl = "0000fff5-0000-1000-8000-00805f9b34fb";
static const char *kGpQuery = "b5f90077-aa8d-11e3-9046-0002a5d5c51b";
static const char *kGpCommand = "b5f90072-aa8d-11e3-9046-0002a5d5c51b";

bool bleWorkerSubmit(BleWorkFn fn, void *context) {
    if (pending) return false;
    pending = fn;
    pendingContext = context;
    return true;
}
bool bleWorkerBusy() { return pending != nullptr; }
bool bleRecycleClient(NimBLEClient *client) { recycled.push_back(client); return true; }
void bleWorkerInit() {}
void bleRecycleProcess() {}
NimBLEClient *bleRecycleAcquire() { return NimBLEDevice::createClient(); }
void bleLogInit() {}
void bleLogProcess() {}
void cpuStatsInit() {}
void cpuStatsUpdate() {}
OpenSlateSettings& settingsGet() { return settings; }
void settingsSave() {}
const char* cameraTypeName(CameraType type) { return type == CAMERA_DJI ? "DJI" : "GoPro"; }
bool bleLogPush(uint8_t, const uint8_t*, uint8_t, uint8_t, uint8_t, uint8_t, uint8_t, int16_t) {
    return true;
}
void scanResultsStart() {}
void scanResultsMarkComplete() {}
void scanResultsUpdateSavedStatus() {}
bool scanResultsIsShowAll() { return false; }
void scanResultsAdd(uint8_t, const char*, const char*, int8_t, bool) {}
void camRegistryRemember(uint8_t, const char*, const char*) {}
void camRegistryClearDiscovered() {}

static void runWorker() {
    TEST_ASSERT_NOT_NULL(pending);
    pending(pendingContext);
    pending = nullptr;
    pendingContext = nullptr;
}

void setUp() {
    stubMillis() += 100000;
    settings = OpenSlateSettings();
    camInit();
    camDisconnect();
    camSetOtaQuiet(false);
    for (uint8_t i = 0; i < CAMERA_CONNECTION_SLOTS; i++) {
        djiCamera(i).init();
        gpCamera(i).init();
    }
}

void tearDown() {
    camDisconnect();
    for (uint8_t i = 0; i < CAMERA_CONNECTION_SLOTS; i++) {
        djiCamera(i).release();
        gpCamera(i).release();
    }
    if (pending) runWorker();
    for (NimBLEClient *client : NimBLEDevice::clients()) delete client;
    NimBLEDevice::clients().clear();
    recycled.clear();
}

static NimBLEClient* connectDji(uint8_t slot, const char *mac) {
    CameraBackend &camera = djiCamera(slot);
    camera.targetMac(mac, 1);
    camera.update(true);
    runWorker();
    NimBLEClient *client = NimBLEDevice::clients().back();
    client->characteristic(kDjiAuth)->emit(
        {0x55, 0, 0, 0, 0, 0, 0, 0, 0xc0, 0x07, 0x45, 0, 1});
    TEST_ASSERT_TRUE(camera.isReady());
    return client;
}

static NimBLEClient* connectGp(uint8_t slot, const char *mac) {
    CameraBackend &camera = gpCamera(slot);
    camera.targetMac(mac);
    camera.update(true);
    runWorker();
    TEST_ASSERT_EQUAL(BLE_AUTHENTICATING, camera.getState());
    TEST_ASSERT_FALSE(bleWorkerBusy());
    NimBLEClient *client = NimBLEDevice::clients().back();
    client->authenticate();
    camera.update(true);
    runWorker();
    TEST_ASSERT_TRUE(camera.isReady());
    return client;
}

void two_dji_sessions_and_disconnects_are_independent() {
    NimBLEClient *first = connectDji(0, "11:22:33:44:55:01");
    connectDji(1, "11:22:33:44:55:02");
    TEST_ASSERT_TRUE(djiCamera(0).sendStartRecord());
    TEST_ASSERT_EQUAL(CAM_STATE_RECORDING, djiCamera(0).getTelemetry().state);
    TEST_ASSERT_EQUAL(CAM_STATE_UNKNOWN, djiCamera(1).getTelemetry().state);
    first->disconnect();
    TEST_ASSERT_FALSE(djiCamera(0).isReady());
    TEST_ASSERT_TRUE(djiCamera(1).isReady());
}

void two_gopro_telemetry_streams_are_independent() {
    NimBLEClient *first = connectGp(0, "11:22:33:44:55:01");
    NimBLEClient *second = connectGp(1, "11:22:33:44:55:02");
    first->characteristic(kGpQuery)->emit({0xb3, 0x46, 75});
    second->characteristic(kGpQuery)->emit({0xb3, 0x46, 25});
    TEST_ASSERT_EQUAL_UINT8(75, gpCamera(0).getTelemetry().batteryPercent);
    TEST_ASSERT_EQUAL_UINT8(25, gpCamera(1).getTelemetry().batteryPercent);
    second->disconnect();
    TEST_ASSERT_TRUE(gpCamera(0).isReady());
    TEST_ASSERT_FALSE(gpCamera(1).isReady());
}

void mixed_brands_and_failed_writes_stay_independent() {
    NimBLEClient *dji = connectDji(0, "11:22:33:44:55:01");
    NimBLEClient *gp = connectGp(1, "11:22:33:44:55:02");
    dji->characteristic(kDjiControl)->writeOk = false;
    gp->characteristic(kGpCommand)->writeOk = false;
    TEST_ASSERT_FALSE(djiCamera(0).sendStartRecord());
    TEST_ASSERT_FALSE(gpCamera(1).sendStopRecord());
    TEST_ASSERT_TRUE(djiCamera(0).isReady());
    TEST_ASSERT_TRUE(gpCamera(1).isReady());
}

void releasing_other_slot_does_not_orphan_its_client_when_worker_busy() {
    NimBLEClient *second = connectDji(1, "11:22:33:44:55:02");
    gpCamera(0).targetMac("11:22:33:44:55:01");
    gpCamera(0).update(true);
    TEST_ASSERT_TRUE(bleWorkerBusy());
    djiCamera(1).release();
    TEST_ASSERT_EQUAL_UINT(1, recycled.size());
    TEST_ASSERT_EQUAL_PTR(second, recycled[0]);
    runWorker();
}

void cancelled_connect_cannot_restore_a_released_slot() {
    djiCamera(0).targetMac("11:22:33:44:55:01");
    djiCamera(0).update(true);
    djiCamera(0).release();
    runWorker();
    TEST_ASSERT_FALSE(djiCamera(0).isReady());
    TEST_ASSERT_EQUAL_UINT(1, recycled.size());
}

void reconnect_retains_own_address_and_respects_ota_quiet() {
    NimBLEClient *client = connectDji(1, "11:22:33:44:55:02");
    client->disconnect();
    stubMillis() += BLE_RECONNECT_INTERVAL_MS;
    camSetOtaQuiet(true);
    djiCamera(1).update(true);
    TEST_ASSERT_FALSE(bleWorkerBusy());
    camSetOtaQuiet(false);
    djiCamera(1).update(true);
    runWorker();
    TEST_ASSERT_EQUAL_STRING("11:22:33:44:55:02", client->peer.toString().c_str());
    TEST_ASSERT_EQUAL_UINT8(1, client->peer.getType());
}

static void saveCameras(uint8_t count) {
    settings.camCount = count;
    for (uint8_t i = 0; i < count; i++) {
        settings.cams[i].type = CAMERA_DJI;
        const char *macs[] = {"11:22:33:44:55:01", "11:22:33:44:55:02",
                             "11:22:33:44:55:03", "11:22:33:44:55:04"};
        strlcpy(settings.cams[i].mac, macs[i], sizeof(settings.cams[i].mac));
    }
}

static void scanSaved(const char *first, const char *second = nullptr) {
    camKick(); // No primary selected: enable automatic discovery only.
    stubMillis() += BLE_RECONNECT_MAX_MS;
    camUpdate();
    TEST_ASSERT_TRUE(NimBLEDevice::getScan()->isScanning());
    NimBLEDevice::getScan()->emit(first, 1);
    if (second) NimBLEDevice::getScan()->emit(second, 1);
    NimBLEDevice::getScan()->stop();
    camUpdate();
}

static void approveLastDji() {
    runWorker();
    NimBLEDevice::clients().back()->characteristic(kDjiAuth)->emit(
        {0x55, 0, 0, 0, 0, 0, 0, 0, 0xc0, 0x07, 0x45, 0, 1});
}

void manager_skips_absent_saved_cameras_and_connects_two_visible_ones() {
    saveCameras(4);
    scanSaved(settings.cams[1].mac, settings.cams[2].mac);
    approveLastDji();
    camUpdate();
    approveLastDji();
    TEST_ASSERT_EQUAL_UINT8(2, camConnectedCount());
    TEST_ASSERT_FALSE(camSavedIsReady(0));
    TEST_ASSERT_TRUE(camSavedIsReady(1));
    TEST_ASSERT_TRUE(camSavedIsReady(2));
    TEST_ASSERT_FALSE(camSavedIsReady(3));
    TEST_ASSERT_EQUAL_UINT(2, NimBLEDevice::clients().size());
    stubMillis() += BLE_RECONNECT_MAX_MS;
    camUpdate();
    TEST_ASSERT_FALSE(NimBLEDevice::getScan()->isScanning());
}

void selecting_connected_secondary_preserves_both_links() {
    saveCameras(2);
    scanSaved(settings.cams[0].mac, settings.cams[1].mac);
    approveLastDji();
    camUpdate();
    approveLastDji();
    settings.cams[1].active = true;
    camKick();
    TEST_ASSERT_EQUAL_UINT8(2, camConnectedCount());
    camSetRecordingDesired(true);
    TEST_ASSERT_EQUAL(CAM_STATE_RECORDING, djiCamera(1).getTelemetry().state);
    TEST_ASSERT_EQUAL(CAM_STATE_RECORDING, djiCamera(0).getTelemetry().state);
}

void removing_secondary_does_not_disconnect_primary_or_rescan_it() {
    saveCameras(2);
    scanSaved(settings.cams[0].mac, settings.cams[1].mac);
    approveLastDji();
    camUpdate();
    approveLastDji();
    settings.camCount = 1;
    camUpdate();
    TEST_ASSERT_EQUAL_UINT8(1, camConnectedCount());
    TEST_ASSERT_TRUE(camSavedIsReady(0));
    stubMillis() += BLE_RECONNECT_MAX_MS;
    camUpdate();
    TEST_ASSERT_FALSE(NimBLEDevice::getScan()->isScanning());
}

void unseen_cameras_never_trigger_direct_connection_attempts() {
    saveCameras(2);
    scanSaved("99:99:99:99:99:99");
    TEST_ASSERT_FALSE(bleWorkerBusy());
    TEST_ASSERT_EQUAL_UINT(0, NimBLEDevice::clients().size());
    TEST_ASSERT_EQUAL_UINT8(0, camConnectedCount());
}

void disconnect_cancels_one_slot_without_leaking_the_other() {
    saveCameras(2);
    scanSaved(settings.cams[0].mac, settings.cams[1].mac);
    approveLastDji();
    camUpdate(); // Second slot has a queued connect.
    camDisconnect();
    runWorker();
    TEST_ASSERT_EQUAL_UINT8(0, camConnectedCount());
    TEST_ASSERT_EQUAL_UINT(2, recycled.size());
    camUpdate();
    TEST_ASSERT_FALSE(bleWorkerBusy());
    TEST_ASSERT_FALSE(NimBLEDevice::getScan()->isScanning());
}

void gopro_pairing_approval_does_not_occupy_connect_worker() {
    gpCamera(0).targetMac("11:22:33:44:55:01");
    gpCamera(0).update(true);
    runWorker();
    TEST_ASSERT_EQUAL(BLE_AUTHENTICATING, gpCamera(0).getState());
    NimBLEClient *second = connectGp(1, "11:22:33:44:55:02");
    stubMillis() += 30000;
    gpCamera(0).update(true);
    TEST_ASSERT_EQUAL(BLE_DISCONNECTED, gpCamera(0).getState());
    TEST_ASSERT_TRUE(second->isConnected());
    TEST_ASSERT_TRUE(gpCamera(1).isReady());
}

void repeated_range_loss_failures_do_not_abandon_stickcam() {
    NimBLEClient *client = connectGp(1, "11:22:33:44:55:02");
    client->disconnect();
    client->connectOk = false;
    for (uint8_t i = 0; i < 6; i++) {
        stubMillis() += BLE_RECONNECT_MAX_MS;
        gpCamera(1).update(true);
        runWorker();
        TEST_ASSERT_FALSE(gpCamera(1).isReady());
    }
    client->connectOk = true;
    stubMillis() += BLE_RECONNECT_MAX_MS;
    gpCamera(1).update(true);
    runWorker();
    client->authenticate();
    gpCamera(1).update(true);
    runWorker();
    TEST_ASSERT_TRUE(gpCamera(1).isReady());
    TEST_ASSERT_EQUAL_STRING("11:22:33:44:55:02", client->peer.toString().c_str());
}

void offline_selection_does_not_evict_either_live_camera() {
    saveCameras(3);
    scanSaved(settings.cams[0].mac, settings.cams[1].mac);
    approveLastDji();
    camUpdate();
    approveLastDji();
    settings.cams[2].active = true;
    camKick();
    camUpdate();
    TEST_ASSERT_TRUE(NimBLEDevice::getScan()->isScanning());
    NimBLEDevice::getScan()->stop(); // Selected camera is not advertising.
    camUpdate();
    TEST_ASSERT_EQUAL_UINT8(2, camConnectedCount());
    TEST_ASSERT_TRUE(camSavedIsReady(0));
    TEST_ASSERT_TRUE(camSavedIsReady(1));
    TEST_ASSERT_FALSE(camSavedIsReady(2));
}

void manager_dispatches_a_mixed_pair_to_separate_backends() {
    saveCameras(2);
    settings.cams[1].type = CAMERA_GOPRO;
    scanSaved(settings.cams[0].mac, settings.cams[1].mac);
    approveLastDji();
    camUpdate();
    runWorker();
    NimBLEDevice::clients().back()->authenticate();
    camUpdate();
    runWorker();
    TEST_ASSERT_TRUE(djiCamera(0).isReady());
    TEST_ASSERT_TRUE(gpCamera(1).isReady());
    TEST_ASSERT_EQUAL_UINT8(2, camConnectedCount());
}

void removed_slot_is_released_even_while_other_slot_connects() {
    saveCameras(2);
    settings.cams[1].type = CAMERA_GOPRO;
    scanSaved(settings.cams[0].mac, settings.cams[1].mac);
    approveLastDji();
    camUpdate();
    TEST_ASSERT_TRUE(bleWorkerBusy());
    settings.cams[0] = settings.cams[1];
    settings.camCount = 1;
    camUpdate();
    TEST_ASSERT_FALSE(djiCamera(0).isReady());
    TEST_ASSERT_EQUAL_UINT(1, recycled.size());
    runWorker();
    NimBLEDevice::clients().back()->authenticate();
    camUpdate();
    runWorker();
    TEST_ASSERT_TRUE(camSavedIsReady(0));
    TEST_ASSERT_EQUAL_UINT8(1, camConnectedCount());
}

void recording_intent_reaches_both_cameras() {
    saveCameras(2);
    scanSaved(settings.cams[0].mac, settings.cams[1].mac);
    approveLastDji();
    camUpdate();
    approveLastDji();
    TEST_ASSERT_EQUAL_UINT8(2, camConnectedCount());
    camSetRecordingDesired(true);
    TEST_ASSERT_TRUE(camRecordingDesired());
    TEST_ASSERT_EQUAL(CAM_STATE_RECORDING, djiCamera(0).getTelemetry().state);
    TEST_ASSERT_EQUAL(CAM_STATE_RECORDING, djiCamera(1).getTelemetry().state);
    camSetRecordingDesired(false);
    TEST_ASSERT_EQUAL(CAM_STATE_STANDBY, djiCamera(0).getTelemetry().state);
    TEST_ASSERT_EQUAL(CAM_STATE_STANDBY, djiCamera(1).getTelemetry().state);
}

void stop_stays_pending_for_a_camera_that_is_out_of_range() {
    saveCameras(2);
    scanSaved(settings.cams[0].mac, settings.cams[1].mac);
    approveLastDji();
    camUpdate();
    approveLastDji();
    camSetRecordingDesired(true);
    TEST_ASSERT_EQUAL(CAM_STATE_RECORDING, djiCamera(1).getTelemetry().state);

    // Stickcam drops out of range while the copter camera stays connected.
    NimBLEClient *stickcam = NimBLEDevice::clients().back();
    stickcam->disconnect();
    camSetRecordingDesired(false);
    TEST_ASSERT_EQUAL(CAM_STATE_STANDBY, djiCamera(0).getTelemetry().state);
    TEST_ASSERT_FALSE(djiCamera(1).isReady());

    // Back in range: the deferred STOP must be delivered without a new edge.
    stubMillis() += BLE_RECONNECT_MAX_MS;
    camUpdate();
    runWorker();
    stickcam->characteristic(kDjiAuth)->emit(
        {0x55, 0, 0, 0, 0, 0, 0, 0, 0xc0, 0x07, 0x45, 0, 1});
    camUpdate();
    TEST_ASSERT_TRUE(djiCamera(1).isReady());
    TEST_ASSERT_EQUAL(CAM_STATE_STANDBY, djiCamera(1).getTelemetry().state);
}

void camera_joining_while_recording_starts_recording() {
    saveCameras(2);
    scanSaved(settings.cams[0].mac);
    approveLastDji();
    camUpdate();
    TEST_ASSERT_EQUAL_UINT8(1, camConnectedCount());
    camSetRecordingDesired(true);
    TEST_ASSERT_EQUAL(CAM_STATE_RECORDING, djiCamera(0).getTelemetry().state);

    // Second camera appears later (e.g. powered on after the flight started).
    stubMillis() += BLE_RECONNECT_MAX_MS;
    camUpdate();
    NimBLEDevice::getScan()->emit(settings.cams[1].mac, 1);
    NimBLEDevice::getScan()->stop();
    camUpdate();
    approveLastDji();
    camUpdate();
    TEST_ASSERT_EQUAL_UINT8(2, camConnectedCount());
    TEST_ASSERT_EQUAL(CAM_STATE_RECORDING, djiCamera(1).getTelemetry().state);
}

void assigned_count_tracks_slots_for_the_osd() {
    saveCameras(2);
    scanSaved(settings.cams[0].mac, settings.cams[1].mac);
    approveLastDji();
    camUpdate();
    approveLastDji();
    TEST_ASSERT_EQUAL_UINT8(2, camAssignedCount());
    TEST_ASSERT_EQUAL_UINT8(2, camConnectedCount());
    NimBLEDevice::clients().back()->disconnect();
    TEST_ASSERT_EQUAL_UINT8(2, camAssignedCount());
    TEST_ASSERT_EQUAL_UINT8(1, camConnectedCount());
}

// E04: the "pairing approved" ACK must not be written from the notify
// callback (NimBLE host task) — it is deferred to the next update() tick.
void dji_approval_ack_is_deferred_to_update() {
    CameraBackend &camera = djiCamera(0);
    camera.targetMac("11:22:33:44:55:0a", 1);
    camera.update(true);
    runWorker();
    NimBLEClient *client = NimBLEDevice::clients().back();
    NimBLERemoteCharacteristic *control = client->characteristic(kDjiControl);
    unsigned before = control->writes;
    client->characteristic(kDjiAuth)->emit(
        {0x55, 0, 0, 0, 0, 0, 0, 0, 0x40, 0x07, 0x46, 0, 0});
    TEST_ASSERT_TRUE(camera.isReady());
    TEST_ASSERT_EQUAL_UINT(before, control->writes);   // nothing from the callback
    camera.update(true);
    // ACK (plus the first keep-alive of the fresh session) from loop().
    unsigned afterFirstTick = control->writes;
    TEST_ASSERT_TRUE(afterFirstTick > before);
    camera.update(true);   // same millis(): no keep-alive due, ACK not repeated
    TEST_ASSERT_EQUAL_UINT(afterFirstTick, control->writes);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(two_dji_sessions_and_disconnects_are_independent);
    RUN_TEST(two_gopro_telemetry_streams_are_independent);
    RUN_TEST(mixed_brands_and_failed_writes_stay_independent);
    RUN_TEST(releasing_other_slot_does_not_orphan_its_client_when_worker_busy);
    RUN_TEST(cancelled_connect_cannot_restore_a_released_slot);
    RUN_TEST(reconnect_retains_own_address_and_respects_ota_quiet);
    RUN_TEST(manager_skips_absent_saved_cameras_and_connects_two_visible_ones);
    RUN_TEST(selecting_connected_secondary_preserves_both_links);
    RUN_TEST(removing_secondary_does_not_disconnect_primary_or_rescan_it);
    RUN_TEST(unseen_cameras_never_trigger_direct_connection_attempts);
    RUN_TEST(disconnect_cancels_one_slot_without_leaking_the_other);
    RUN_TEST(gopro_pairing_approval_does_not_occupy_connect_worker);
    RUN_TEST(repeated_range_loss_failures_do_not_abandon_stickcam);
    RUN_TEST(offline_selection_does_not_evict_either_live_camera);
    RUN_TEST(manager_dispatches_a_mixed_pair_to_separate_backends);
    RUN_TEST(removed_slot_is_released_even_while_other_slot_connects);
    RUN_TEST(recording_intent_reaches_both_cameras);
    RUN_TEST(stop_stays_pending_for_a_camera_that_is_out_of_range);
    RUN_TEST(camera_joining_while_recording_starts_recording);
    RUN_TEST(assigned_count_tracks_slots_for_the_osd);
    RUN_TEST(dji_approval_ack_is_deferred_to_update);
    return UNITY_END();
}
