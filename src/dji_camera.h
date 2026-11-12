// ============================================================================
// dji_camera.h — Independent DJI Osmo connection slots
// ============================================================================

#ifndef DJI_CAMERA_H
#define DJI_CAMERA_H

#include "camera_backend.h"

CameraBackend& djiCamera(uint8_t slot);
void djiStartScan();

#endif // DJI_CAMERA_H
